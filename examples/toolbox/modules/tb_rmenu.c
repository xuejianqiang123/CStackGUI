/**
 * @file    tb_rmenu.c
 * @brief   toolbox 右键菜单管理实现：六位置扫描 + 五路过滤 + 批量禁用/恢复/备份删除/还原
 * @details 扫 HKCU 用户区 + HKLM 系统区六个位置（文件 / 文件夹 / 文件夹空白处 / 桌面 /
 *          磁盘 / 通用对象）的 shell 程序菜单与 shellex DLL 扩展项，逐项读
 *          MUIVerb / command / LegacyDisable，按来源分组、按状态分类；
 *          工具箱自己装的两条菜单用 reg.exe 真装真撤；多选行真操作，每次操作后自动重扫。
 *          破坏性操作（备份并删除）先弹模态确认框列出全部路径，备份成功才敢删。
 */
#include "tb_rmenu.h"

#include <stdio.h>
#include <string.h>

#include "tb_log.h"

/** 注册表根：用户区 = HKCU（自己和用户装的，改得动）；系统区 = HKLM（程序装的，改它要管理员） */
enum { RM_ROOT_USER = 0, RM_ROOT_SYS = 1 };
static const char *k_rm_roots[] = { "HKCU", "HKLM" };

/** 工具箱自建右键菜单：键（相对 HKCU\Software\Classes）+ 菜单标题（默认值）+ command */
static const struct {
    const char *key;   /**< 菜单项键 */
    const char *title; /**< 菜单标题 */
    const char *cmd;   /**< command 默认值 */
} k_rmenu[TB_RMENU_COUNT] = {
    { "*\\shell\\CStackGUI.CopyPath",
      "复制文件路径",
      "cmd.exe /c echo %1 | clip" },
    { "Directory\\Background\\shell\\CStackGUI.Terminal",
      "在此处打开终端",
      "cmd.exe /k cd /d \"%V\"" }
};

/** 自建菜单当前是否已装（就绪钩子里读 command 判定） */
static int s_rm_installed[TB_RMENU_COUNT];

/**
 * 扫描位置（页签，与 .cgui 里 rmtab0..rmtab5 同序）：base 是**类名**，
 * shell（程序装的菜单）与 shellex（DLL 扩展项）两类键都挂在它下面；
 * 每类都要在用户区和系统区各扫一遍 —— 参考实现按"右键点在哪"分页，正是这个口径。
 */
static const struct {
    const char *base;   /**< 类名，如 "*"、"Directory" */
    const char *label;  /**< 页签名 */
    const char *tab;    /**< .cgui 里页签按钮的 id */
} k_rm_areas[] = {
    { "*",                     "文件",        "rmtab0" },
    { "Directory",             "文件夹",      "rmtab1" },
    { "Directory\\Background", "文件夹空白处", "rmtab2" },
    { "DesktopBackground",     "桌面",        "rmtab3" },
    { "Drive",                 "磁盘",        "rmtab4" },
    { "AllFilesystemObjects",  "通用对象",    "rmtab5" }
};

/** 扫描位置数 */
#define K_RM_AREA_COUNT ((int)(sizeof k_rm_areas / sizeof k_rm_areas[0]))

/** 扫描结果里一项的状态分类：在用 / 已隐藏（有 LegacyDisable）/ 无命令（只有容器壳） */
enum { RM_ACTIVE = 0, RM_HIDDEN = 1, RM_NOCOMMAND = 2 };

/** 分组（参考实现的三组）：工具箱自己装的 / 程序装的 / DLL 扩展项（只显示不给动）。
 *  分组名不再单独占一列 —— 列表换装 iconlist 后由行图标三色承担（rm_icon_of），
 *  分组统计在 lab_rm_stat 的文字里。 */
enum { RM_G_OURS = 0, RM_G_PROG = 1, RM_G_EXT = 2 };

/** 过滤按钮（与 .cgui 里 rmf0..rmf4 同序：全部 / 未禁用 / 程序装的 / 已禁用 / 扩展项） */
enum { RM_F_ALL = 0, RM_F_ON, RM_F_PROG, RM_F_OFF, RM_F_EXT, RM_F_COUNT };
static const char *k_rm_filter_ids[RM_F_COUNT] = { "rmf0", "rmf1", "rmf2", "rmf3", "rmf4" };

/** 右键扫描结果行数上限（表行与 s_rm_rows 一一对应，超出的行只显示、不可操作） */
#define K_RM_ROW_MAX 256

/** 本次会话右键改动记录上限（禁用 / 删键各占一条） */
#define K_RM_CHG_MAX 64

/** 一次批量操作（勾选后禁用 / 恢复 / 删除）的目标条数上限 */
#define K_RM_BATCH 128

/**
 * 扫描结果行：一行 = 某个位置里的一条菜单（页签 + 过滤 + 搜索筛完才上表）。
 * 表格的 row-value 存本数组下标 —— 过滤后行号与本数组不再同序，选中行靠它翻回子键。
 */
static struct {
    char sub[620];   /**< 子键路径（含 Software\Classes），与扫描用的 path[600] 等宽，装不下宁可大不截断 */
    char title[300]; /**< 菜单标题（搜索按它匹配） */
    char cmd[512];   /**< 命令行（shell → command 子键；扩展项 → DLL 路径 / CLSID） */
    int  kind;       /**< RM_ACTIVE / RM_HIDDEN / RM_NOCOMMAND */
    int  root;       /**< RM_ROOT_USER / RM_ROOT_SYS */
    int  area;       /**< k_rm_areas 下标 */
    int  group;      /**< RM_G_* */
} s_rm_rows[K_RM_ROW_MAX];

/** 扫描到的全部行数（含没显示在当前页签上的） */
static int s_rm_row_n;

/**
 * 视图行 -> s_rm_rows 下标 的映射（页签/过滤/搜索筛完才上表，两者不再同序）。
 * 原来由 table 的 row-value 字段记；列表换成 iconlist 后它没有这个字段，
 * 改由这里自己记 —— rm_fill 重建时逐行填，读行数据的一律走它。
 */
static int s_rm_view[K_RM_ROW_MAX];

/** 行数到上限被截断（K_RM_ROW_MAX 满了）；日志里要说明"共 N 条"是截断值 */
static int s_rm_row_limit;

/** 每个位置扫到几条（页签按钮上的数字） */
static int s_rm_tab_n[K_RM_AREA_COUNT];

/** 当前页签 / 过滤 / 搜索词 / 是否扫过（没扫过时点页签会先扫一次） */
static int  s_rm_tab;
static int  s_rm_filter = RM_F_ALL;
static char s_rm_search[64];
static int  s_rm_scanned;

/** 本次会话动过的右键项：禁用、或整键删除（记下备份 .reg 路径 + 根） */
static struct {
    char sub[620];   /**< 子键路径（含 Software\Classes，与 s_rm_rows[].sub 同宽） */
    char bak[1024];  /**< 删除前的备份 .reg 全路径；空串 = 只禁用了、没删键 */
    int  root;       /**< RM_ROOT_USER / RM_ROOT_SYS */
} s_rm_changed[K_RM_CHG_MAX];

/** 改动记录条数 */
static int s_rm_changed_n;

/** 备份 .reg 存放目录（数据目录；就绪钩子里填） */
static char s_rm_bakdir[1024];

/** 待确认删除的目标行（s_rm_rows 下标）；0 条 = 没有待确认的（确认框是模态的，弹之前先滤好） */
static int s_rm_pending_rows[K_RM_BATCH];
static int s_rm_pending_n;

/** 备份文件名去重用序号（同一秒内连删两项也不覆盖） */
static int s_rm_bakseq;

/**
 * @brief 自建菜单组数（底栏「本页 X 个」在右键页用它计数）
 * @return int 组数（固定 TB_RMENU_COUNT）
 */
int tb_rmenu_count(void)
{
    return TB_RMENU_COUNT;
}

/**
 * @brief 就绪钩子用：定备份 .reg 存放目录（数据目录；拿不到时留空，删键时用 "."）
 * @param dir 数据目录全路径；NULL 表示拿不到
 * @return void 无返回值
 */
void tb_rmenu_set_bakdir(const char *dir)
{
    snprintf(s_rm_bakdir, sizeof(s_rm_bakdir), "%s", dir ? dir : "");
}

/**
 * @brief 同步自建菜单按钮的可用状态与说明文字
 * @param app 应用
 * @return void 无返回值
 */
void tb_rmenu_sync(cg_app_t *app)
{
    static const char *on_id[TB_RMENU_COUNT]  = { "rm0_on",  "rm1_on"  };
    static const char *off_id[TB_RMENU_COUNT] = { "rm0_off", "rm1_off" };
    static const char *lab_id[TB_RMENU_COUNT] = { "lab_rm0", "lab_rm1" };
    int i;

    for (i = 0; i < TB_RMENU_COUNT; i++) {
        char        buf[128];
        cg_widget_t *on  = cg_find(app, on_id[i]);
        cg_widget_t *off = cg_find(app, off_id[i]);

        if (on)  cg_widget_set_enabled(on,  !s_rm_installed[i]);
        if (off) cg_widget_set_enabled(off,  s_rm_installed[i]);
        snprintf(buf, sizeof(buf), "「%s」—— %s", k_rmenu[i].title,
                 s_rm_installed[i] ? "已装上" : "未装");
        cg_widget_set_text(cg_find(app, lab_id[i]), buf);
    }
}

/**
 * @brief 就绪钩子用：读回自建菜单装撤状态（command 有值 = 已装）
 * @param read_reg 1=真读注册表；0=自检，按未装算
 * @return void 无返回值
 */
void tb_rmenu_load(int read_reg)
{
    int i;

    for (i = 0; i < TB_RMENU_COUNT; i++) {
        s_rm_installed[i] = 0;
        if (!read_reg) continue;
        {
            char key[256];
            char *cmd;
            snprintf(key, sizeof(key), "Software\\Classes\\%s\\command", k_rmenu[i].key);
            cmd = cg_reg_read(key, "", NULL);
            s_rm_installed[i] = (cmd && *cmd) ? 1 : 0;
            cg_free(cmd);
        }
    }
}

/**
 * @brief 装上 / 撤掉本工具箱自己的右键菜单：reg.exe add/delete，删得干净
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 写注册表是副作用：自检下只记日志不执行。id 形如 rm0_on / rm0_off，
 *       方向用 strstr 认 —— 教训：别用固定偏移猜（opt 那次就是偏移错一位）。
 */
static void on_rmenu_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id  = cg_widget_id(self);
    int         idx;
    int         on;
    char        keypath[384];
    char        cmdline[768];

    (void)ev;
    (void)user;

    if (!id || strlen(id) < 6) return;
    idx = id[2] - '0';
    on  = (strstr(id, "_on") != NULL);
    if (idx < 0 || idx >= TB_RMENU_COUNT) return;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过 reg.exe（%s：%s）",
                    k_rmenu[idx].title, on ? "装上" : "撤掉");
        s_rm_installed[idx] = on;
        tb_rmenu_sync(app);
        return;
    }

    snprintf(keypath, sizeof(keypath), "HKCU\\Software\\Classes\\%s", k_rmenu[idx].key);
    if (on) {
        snprintf(cmdline, sizeof(cmdline),
                 "reg add \"%s\" /ve /d \"%s\" /f", keypath, k_rmenu[idx].title);
        if (cg_run_process(cmdline, 1) != 0) {
            tb_status_msg(app, "reg add 失败（标题）");
            tb_log_line(app, "reg.exe 失败：%s", cmdline);
            return;
        }
        snprintf(cmdline, sizeof(cmdline),
                 "reg add \"%s\\command\" /ve /d \"%s\" /f", keypath, k_rmenu[idx].cmd);
        if (cg_run_process(cmdline, 1) != 0) {
            tb_status_msg(app, "reg add 失败（命令）");
            tb_log_line(app, "reg.exe 失败：%s", cmdline);
            return;
        }
    } else {
        snprintf(cmdline, sizeof(cmdline), "reg delete \"%s\" /f", keypath);
        if (cg_run_process(cmdline, 1) != 0) {
            tb_status_msg(app, "reg delete 失败");
            tb_log_line(app, "reg.exe 失败：%s", cmdline);
            return;
        }
    }

    s_rm_installed[idx] = on;
    tb_rmenu_sync(app);
    tb_status_msg(app, "%s：%s", k_rmenu[idx].title, on ? "已装上" : "已撤掉");
    tb_log_line(app, "右键菜单「%s」%s（reg.exe）", k_rmenu[idx].title, on ? "装上" : "撤掉");
}

/**
 * @brief 刷新「曾改动」提示行：列出本次会话动过的项（只列键名末段，太长就截）
 * @param app 应用
 * @return void 无返回值
 */
static void rm_changed_sync(cg_app_t *app)
{
    char buf[768];
    int  i, off = 0, del = 0;

    for (i = 0; i < s_rm_changed_n; i++)
        if (s_rm_changed[i].bak[0]) del++;

    if (s_rm_changed_n == 0) {
        snprintf(buf, sizeof(buf), "曾改动：无");
    } else {
        off += snprintf(buf + off, sizeof(buf) - (size_t)off,
                        "曾改动 %d 项（删键 %d）：", s_rm_changed_n, del);
        for (i = 0; i < s_rm_changed_n && off < (int)sizeof(buf) - 64; i++) {
            const char *p = strrchr(s_rm_changed[i].sub, '\\');
            off += snprintf(buf + off, sizeof(buf) - (size_t)off, "%s%s%s",
                            s_rm_changed[i].root == RM_ROOT_SYS ? "[系]" : "",
                            p ? p + 1 : s_rm_changed[i].sub,
                            (i + 1 < s_rm_changed_n) ? "、" : "");
        }
    }
    cg_widget_set_text(cg_find(app, "lab_rm_changed"), buf);
}

/**
 * @brief rm_fill 的前向声明（rm_scan 里先用后定义，这里先占位）
 * @param app 应用
 * @return void 无返回值
 */
static void rm_fill(cg_app_t *app);

/**
 * @brief 不区分 ASCII 大小写的子串查找（搜索框用；中文按原样比，不做折字）
 * @param s 被搜的串
 * @param needle 要找的子串；空串算命中
 * @return int 1 = 命中
 */
static int rm_has_ci(const char *s, const char *needle)
{
    if (!needle || !*needle) return 1;
    for (; *s; s++) {
        const char *a = s;
        const char *b = needle;
        while (*a && *b) {
            int ca = (unsigned char)*a;
            int cb = (unsigned char)*b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) break;
            a++;
            b++;
        }
        if (!*b) return 1;
    }
    return 0;
}

/**
 * @brief 这条子键是不是本工具箱自己装的那两个（分组「工具箱装的」靠它认）
 * @param sub 子键路径（相对 Software\Classes）
 * @return int k_rmenu 下标 + 1；不是自己装的返回 0
 */
static int rm_is_ours(const char *sub)
{
    int i;
    for (i = 0; i < TB_RMENU_COUNT; i++) {
        char want[600];
        snprintf(want, sizeof(want), "Software\\Classes\\%s", k_rmenu[i].key);
        if (_stricmp(sub, want) == 0) return i + 1;
    }
    return 0;
}

/**
 * @brief 扫一个「菜单集合键」（某位置下的 shell 或 shellex）的直接子键，逐条归类进 s_rm_rows
 * @param root_i RM_ROOT_USER / RM_ROOT_SYS（对应 "HKCU" / "HKLM"）
 * @param sub 集合键路径（相对 Software\Classes），如 *\\shell
 * @param area 位置下标（k_rm_areas）
 * @param base_group 该类键的默认分组：shell → RM_G_PROG，shellex → RM_G_EXT
 * @param ri 下一个可写行号
 * @return int 新的行号
 * @note 纯只读；shellex 那类只取名字与 DLL（参考实现的口径：只显示、不给动）。
 */
static int rm_scan_folder(int root_i, const char *sub, int area, int base_group, int ri)
{
    const char *root = k_rm_roots[root_i];
    char        buf[8192];
    int         n, i = 0;

    n = cg_reg_subkeys_ex(root, sub, buf, sizeof(buf));
    while (i < n && ri < K_RM_ROW_MAX) {
        int         j = i;
        char        path[600];
        char        cmdkey[768];
        char        title[300];
        char        cmd[512];
        char       *s1, *s2;
        int         kind;
        int         head;

        while (j < n && buf[j] != '\n') j++;
        /* 行边界截断：%s 会穿过 '\n' 分隔符把整个剩余 buf 拼进 path，
         * RegOpenKeyExW 打不开带换行的路径，command/标题会全体读空。
         * fallback 的键名同理，且必须取本行（推进 i 之前）而不是下一行。 */
        head = i;
        snprintf(path, sizeof(path), "%s\\%.*s", sub, j - head, &buf[head]);
        i = j + 1;
        if (base_group == RM_G_EXT) {
            char clsid[128];
            char cpath[768];
            int  is_guid;

            /* 扩展项：名字看 MUIVerb / 默认值，命令列给 DLL（能解析就带 CLSID 说明） */
            s1 = cg_reg_read_ex(root, path, "MUIVerb", NULL);
            s2 = cg_reg_read_ex(root, path, "", NULL);
            if (s1 && *s1)      snprintf(title, sizeof(title), "%s", s1);
            else if (s2 && *s2) snprintf(title, sizeof(title), "%s", s2);
            else                snprintf(title, sizeof(title), "%.*s", j - head, &buf[head]);
            cg_free(s1);
            cg_free(s2);
            snprintf(cmd, sizeof(cmd), "%.*s", j - head, &buf[head]);
            /* 默认值是 {GUID} 的（写法很常见）：把 GUID 变成"组件名 + DLL"再显示 */
            s1 = cg_reg_read_ex(root, path, "", NULL);
            is_guid = (s1 && s1[0] == '{');
            snprintf(clsid, sizeof(clsid), "%s", is_guid ? s1 : "");
            cg_free(s1);
            if (is_guid) {
                char *cname;
                char *dll;
                snprintf(cpath, sizeof(cpath), "CLSID\\%s", clsid);
                cname = cg_reg_read_ex("HKCR", cpath, "", NULL);
                if (cname && *cname && strlen(cname) < sizeof(title))
                    snprintf(title, sizeof(title), "%s", cname);
                cg_free(cname);
                snprintf(cpath, sizeof(cpath), "CLSID\\%s\\InprocServer32", clsid);
                dll = cg_reg_read_ex("HKCR", cpath, "", NULL);
                if (dll && *dll) snprintf(cmd, sizeof(cmd), "%s", dll);
                else             snprintf(cmd, sizeof(cmd), "%s", clsid);
                cg_free(dll);
            }
            kind = RM_ACTIVE;
        } else {
            char *legacy;

            /* shell 菜单：MUIVerb / 默认值 → 标题；command → 命令；LegacyDisable → 已隐藏 */
            s1 = cg_reg_read_ex(root, path, "MUIVerb", NULL);
            s2 = cg_reg_read_ex(root, path, "", NULL);
            if (s1 && *s1)      snprintf(title, sizeof(title), "%s", s1);
            else if (s2 && *s2) snprintf(title, sizeof(title), "%s", s2);
            else                snprintf(title, sizeof(title), "%.*s", j - head, &buf[head]);
            cg_free(s1);
            cg_free(s2);
            snprintf(cmdkey, sizeof(cmdkey), "%s\\command", path);
            s1 = cg_reg_read_ex(root, cmdkey, "", NULL);
            if (s1 && *s1) snprintf(cmd, sizeof(cmd), "%s", s1);
            else            cmd[0] = '\0';
            cg_free(s1);
            legacy = cg_reg_read_ex(root, path, "LegacyDisable", NULL);
            if (!cmd[0])      kind = RM_NOCOMMAND;   /* 只有容器壳，没 command */
            else if (legacy)  kind = RM_HIDDEN;      /* 空值存在 = 菜单被隐藏 */
            else              kind = RM_ACTIVE;
            cg_free(legacy);
        }

        snprintf(s_rm_rows[ri].sub,   sizeof(s_rm_rows[0].sub),   "%s", path);
        snprintf(s_rm_rows[ri].title, sizeof(s_rm_rows[0].title), "%s", title);
        snprintf(s_rm_rows[ri].cmd,   sizeof(s_rm_rows[0].cmd),   "%s", cmd);
        s_rm_rows[ri].kind  = kind;
        s_rm_rows[ri].root  = root_i;
        s_rm_rows[ri].area  = area;
        s_rm_rows[ri].group = (base_group != RM_G_EXT && rm_is_ours(path)) ? RM_G_OURS : base_group;
        ri++;
        s_rm_tab_n[area]++;
    }
    return ri;
}

/**
 * @brief 扫描六个位置（用户区 HKCU + 系统区 HKLM，含 shellex 扩展项），重建 s_rm_rows 并刷表
 * @param app 应用
 * @return void 无返回值
 * @note 纯只读；表格整表重建用 begin/end update 围住，不围就是逐行重建，几百条会连续闪。
 *       状态判定：command 有值且没有 LegacyDisable = 在用；有 LegacyDisable = 已禁用；
 *       读不到 command = 无命令。shellex 扩展项不看 LegacyDisable（那是 shell 键的开关）。
 */
static void rm_scan(cg_app_t *app)
{
    char key[600];
    int  a, r, ri = 0;

    s_rm_row_n = 0;
    s_rm_scanned = 1;
    s_rm_row_limit = 0;
    memset(s_rm_tab_n, 0, sizeof(s_rm_tab_n));

    for (a = 0; a < K_RM_AREA_COUNT && !s_rm_row_limit; a++) {
        for (r = 0; r < 2 && !s_rm_row_limit; r++) {
            snprintf(key, sizeof(key), "Software\\Classes\\%s\\shell", k_rm_areas[a].base);
            ri = rm_scan_folder(r, key, a, RM_G_PROG, ri);
            snprintf(key, sizeof(key), "Software\\Classes\\%s\\shellex\\ContextMenuHandlers",
                     k_rm_areas[a].base);
            ri = rm_scan_folder(r, key, a, RM_G_EXT, ri);
            if (ri >= K_RM_ROW_MAX) s_rm_row_limit = 1;
        }
    }
    s_rm_row_n = ri;
    rm_fill(app);
    rm_changed_sync(app);
    tb_status_msg(app, "扫描完成：%d 条 · 位置「%s」%d 条", s_rm_row_n,
                  k_rm_areas[s_rm_tab].label, s_rm_tab_n[s_rm_tab]);
    tb_log_line(app, "扫描右键菜单（HKCU + HKLM，%d 个位置，含 shellex 扩展项）：共 %d 条%s",
                K_RM_AREA_COUNT, s_rm_row_n,
                s_rm_row_limit ? "（到上限被截断）" : "");
}

/**
 * @brief 这一行在当前视图里吗（页签 + 过滤 + 搜索三个条件都过才算）
 * @param i s_rm_rows 下标
 * @return int 1 = 显示
 */
static int rm_match(int i)
{
    if (s_rm_rows[i].area != s_rm_tab) return 0;
    switch (s_rm_filter) {
    case RM_F_ON:   if (s_rm_rows[i].kind == RM_HIDDEN) return 0; break;
    case RM_F_OFF:  if (s_rm_rows[i].kind != RM_HIDDEN) return 0; break;
    case RM_F_PROG: if (s_rm_rows[i].group != RM_G_PROG) return 0; break;
    case RM_F_EXT:  if (s_rm_rows[i].group != RM_G_EXT) return 0; break;
    default: break;
    }
    return rm_has_ci(s_rm_rows[i].title, s_rm_search);
}

/**
 * @brief 按分组选这一行的图标（绿=工具箱装的 / 蓝=程序装的 / 灰=扩展项）
 * @param group RM_G_OURS / RM_G_PROG / RM_G_EXT
 * @return const char* assets 下的图标路径（iconlist 要求文件路径，这里给仓库自带的分类图）
 */
static const char *rm_icon_of(int group)
{
    return group == RM_G_OURS ? "assets/rm_ours.png"
         : group == RM_G_EXT  ? "assets/rm_ext.png"
         :                      "assets/rm_prog.png";
}

/**
 * @brief 刷新页签数字、过滤按钮的「当前」标记、统计行与批量按钮文案（只动文字，不重建表）
 * @param app 应用
 * @return void 无返回值
 */
static void rm_stat_sync(cg_app_t *app)
{
    cg_widget_t  *tbl = cg_find(app, "tbl_regmenu");
    char          buf[420];
    int           rows = tbl ? cg_iconlist_count(tbl) : 0;
    int           i, chk = 0, on = 0, off = 0, noc = 0, ext = 0, sys = 0;
    static const char *flab[RM_F_COUNT] = { "全部", "未禁用", "程序装的", "已禁用", "扩展项" };

    for (i = 0; i < rows; i++) {
        int di = s_rm_view[i];          /* 视图行 -> s_rm_rows（过滤/搜索后不同序） */
        if (cg_iconlist_checked(tbl, i)) chk++;
        if (di < 0 || di >= s_rm_row_n) continue;
        if (s_rm_rows[di].root == RM_ROOT_SYS) sys++;
        if (s_rm_rows[di].group == RM_G_EXT) ext++;
        else if (s_rm_rows[di].kind == RM_HIDDEN) off++;
        else if (s_rm_rows[di].kind == RM_NOCOMMAND) noc++;
        else on++;
    }

    for (i = 0; i < K_RM_AREA_COUNT; i++) {
        cg_widget_t *w = cg_find(app, k_rm_areas[i].tab);
        snprintf(buf, sizeof(buf), "%s %d", k_rm_areas[i].label, s_rm_tab_n[i]);
        if (w) cg_widget_set_text(w, buf);
    }
    for (i = 0; i < RM_F_COUNT; i++) {
        cg_widget_t *w = cg_find(app, k_rm_filter_ids[i]);
        snprintf(buf, sizeof(buf), "%s%s", (i == s_rm_filter) ? "✓ " : "", flab[i]);
        if (w) cg_widget_set_text(w, buf);
    }

    if (!s_rm_scanned) {
        snprintf(buf, sizeof(buf), "（还没有扫描：点上面「扫描右键菜单」才读注册表）");
    } else {
        snprintf(buf, sizeof(buf),
                 "已勾选 %d 项 · 共 %d 条（在用 %d / 已禁用 %d / 无命令 %d / 扩展项 %d；"
                 "其中 %d 条在系统区，改它要管理员）· %s",
                 chk, rows, on, off, noc, ext, sys, k_rm_areas[s_rm_tab].label);
    }
    cg_widget_set_text(cg_find(app, "lab_rm_stat"), buf);

    if (chk > 0) {
        snprintf(buf, sizeof(buf), "禁用选中的 %d 项", chk);
        cg_widget_set_text(cg_find(app, "btn_rm_disable"), buf);
        snprintf(buf, sizeof(buf), "恢复选中的 %d 项", chk);
        cg_widget_set_text(cg_find(app, "btn_rm_enable"), buf);
        snprintf(buf, sizeof(buf), "备份并删除选中的 %d 项…", chk);
        cg_widget_set_text(cg_find(app, "btn_rm_backup_del"), buf);
    } else {
        cg_widget_set_text(cg_find(app, "btn_rm_disable"), "禁用选中项");
        cg_widget_set_text(cg_find(app, "btn_rm_enable"), "恢复选中项");
        cg_widget_set_text(cg_find(app, "btn_rm_backup_del"), "备份并删除选中…");
    }
}

/**
 * @brief 按当前页签 / 过滤 / 搜索重建扫描结果表，然后刷新统计
 * @param app 应用
 * @return void 无返回值
 * @note 表行的 row-value 存 s_rm_rows 下标：过滤后两者不再同序，操作按钮靠它翻回子键。
 *       重建过程不要动勾选状态 —— 用户点勾选框只会触发 rm_stat_sync，不会走进来。
 */
static void rm_fill(cg_app_t *app)
{
    cg_widget_t *tbl = cg_find(app, "tbl_regmenu");
    int          i;

    /* iconlist 没有 begin/end update：逐行 add 只是把重绘失效排队，
     * 整批在同一条消息里加完才画一次，不会像原生 ListView 那样逐行闪。
     * 行形态 = 主标题（菜单标题）+ 副标题（命令）+ 右侧状态（在用·用户区…）；
     * 原来的「分组」列由图标三色承担（rm_icon_of），分组统计在 lab_rm_stat。 */
    if (tbl) cg_iconlist_clear(tbl);
    for (i = 0; i < s_rm_row_n; i++) {
        char        title[300];
        char        cmd[600];
        char        state[64];

        if (!rm_match(i)) continue;
        snprintf(title, sizeof(title), "%s", s_rm_rows[i].title);
        snprintf(cmd, sizeof(cmd), "%s", s_rm_rows[i].cmd[0] ? s_rm_rows[i].cmd : "—");
        if (s_rm_rows[i].group == RM_G_EXT) {
            snprintf(state, sizeof(state), "扩展项·%s", s_rm_rows[i].root == RM_ROOT_SYS
                     ? "系统区" : "用户区");
        } else {
            const char *k = s_rm_rows[i].kind == RM_HIDDEN ? "已禁用"
                          : s_rm_rows[i].kind == RM_NOCOMMAND ? "无命令" : "在用";
            snprintf(state, sizeof(state), "%s·%s", k,
                     s_rm_rows[i].root == RM_ROOT_SYS ? "系统区" : "用户区");
        }
        if (tbl) {
            int r = cg_iconlist_add(tbl, rm_icon_of(s_rm_rows[i].group), title, cmd, state);
            if (r >= 0) s_rm_view[r] = i;   /* 视图行 -> s_rm_rows（读行数据的都靠它） */
        }
    }
    rm_stat_sync(app);
}

/**
 * @brief 「扫描右键菜单」按钮：按自检红线先挡掉无窗（不开窗不碰注册表）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_regmenu_scan_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过扫描右键菜单（不碰注册表）");
        return;
    }
    rm_scan(app);
}

/**
 * @brief 位置页签：切到某个位置（没扫过就先扫一次 —— 点页签就是「我要看这个位置」）
 * @param self 被点的页签按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_rm_tab_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id  = cg_widget_id(self);
    int         i;

    (void)ev;
    (void)user;
    for (i = 0; i < K_RM_AREA_COUNT; i++)
        if (id && strcmp(id, k_rm_areas[i].tab) == 0) break;
    if (i >= K_RM_AREA_COUNT) return;
    s_rm_tab = i;
    if (!s_rm_scanned) {
        if (!cg_app_has_window(app)) {
            tb_log_line(app, "自检模式：跳过扫描右键菜单（不碰注册表）");
            return;
        }
        rm_scan(app);
    } else {
        rm_fill(app);
    }
}

/**
 * @brief 过滤按钮：全部 / 未禁用 / 程序装的 / 已禁用 / 扩展项
 * @param self 被点的过滤按钮（rmf0..rmf4）
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_rm_filter_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id  = cg_widget_id(self);
    int         i;

    (void)ev;
    (void)user;
    for (i = 0; i < RM_F_COUNT; i++)
        if (id && strcmp(id, k_rm_filter_ids[i]) == 0) break;
    if (i >= RM_F_COUNT) return;
    s_rm_filter = i;
    rm_fill(app);
}

/**
 * @brief 搜索框：按标题子串过滤（不区分 ASCII 大小写，中文按原样比）
 * @param self 搜索框
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 没扫过就只记下词，等扫描 —— 不因为打字就去碰注册表。
 */
static void on_rm_search_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    const char  *t   = cg_widget_text(self);

    (void)ev;
    (void)user;
    snprintf(s_rm_search, sizeof(s_rm_search), "%s", t ? t : "");
    if (s_rm_scanned) rm_fill(app);
}

/**
 * @brief 勾选变化：只刷统计行与批量按钮文案，**不**重建表 —— 重建会把勾选清掉
 * @param self 表格
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_rm_check(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    rm_stat_sync(app);
}

/**
 * @brief 取当前选中的扫描结果行
 * @param app 应用
 * @return int 行下标；没选中或超出可操作范围返回 -1
 */
static int rm_sel_row(cg_app_t *app)
{
    cg_widget_t *tbl = cg_find(app, "tbl_regmenu");
    int          r   = tbl ? cg_widget_value(tbl) : -1;   /* iconlist 的选中行 = value */
    int          di;

    if (!tbl || r < 0 || r >= cg_iconlist_count(tbl)) return -1;
    di = s_rm_view[r];                 /* 视图行 → s_rm_rows 下标（页签/过滤/搜索筛过就不同序） */
    return (di >= 0 && di < s_rm_row_n) ? di : -1;
}

/**
 * @brief 收集本次操作的目标行：勾选的行优先，一条没勾就退化成「当前选中的那一行」
 * @param app 应用
 * @param out 出口数组（存 s_rm_rows 下标）
 * @param cap 出口容量
 * @return int 目标条数；0 = 没有可操作的目标
 */
static int rm_target_rows(cg_app_t *app, int *out, int cap)
{
    cg_widget_t *tbl = cg_find(app, "tbl_regmenu");
    int         n = 0;
    int         i, sel;

    if (tbl) {
        for (i = 0; i < cg_iconlist_count(tbl) && n < cap; i++) {
            int di;
            if (!cg_iconlist_checked(tbl, i)) continue;
            di = s_rm_view[i];
            if (di >= 0 && di < s_rm_row_n) out[n++] = di;
        }
    }
    if (n > 0) return n;

    sel = rm_sel_row(app);
    if (sel >= 0 && n < cap) out[n++] = sel;
    return n;
}

/**
 * @brief 这一行动不动得；不能动时给出用户看得懂的理由
 * @param di s_rm_rows 下标
 * @param admin 当前进程是否已提权（调用方查一次 token，别每行都问）
 * @return const char *NULL = 可以动；否则就是拒绝理由（扩展项只显示不给动 / 系统区要管理员）
 */
static const char *rm_block_reason(int di, int admin)
{
    if (di < 0 || di >= s_rm_row_n) return "行号无效（请重新扫描）";
    if (s_rm_rows[di].group == RM_G_EXT)
        return "扩展项（shellex DLL 注册）只显示不给动";
    if (s_rm_rows[di].root == RM_ROOT_SYS && !admin)
        return "系统区（HKLM）要管理员身份：以管理员重新运行本工具再改";
    return NULL;
}

/**
 * @brief 把一条改动记进 s_rm_changed（同一根下同一子键不重复记）
 * @param root RM_ROOT_USER / RM_ROOT_SYS（还原时按根删值 / 导备份）
 * @param sub 子键路径（相对根，含 Software\Classes）
 * @param bak 删除前的备份 .reg 路径；传 NULL 表示只禁用、没删键
 * @return void 无返回值
 */
static void rm_changed_add(int root, const char *sub, const char *bak)
{
    int i;

    for (i = 0; i < s_rm_changed_n; i++)
        if (s_rm_changed[i].root == root && strcmp(s_rm_changed[i].sub, sub) == 0) {
            if (bak && *bak) snprintf(s_rm_changed[i].bak, sizeof(s_rm_changed[0].bak), "%s", bak);
            return;
        }
    if (s_rm_changed_n >= K_RM_CHG_MAX) return;
    snprintf(s_rm_changed[s_rm_changed_n].sub, sizeof(s_rm_changed[0].sub), "%s", sub);
    snprintf(s_rm_changed[s_rm_changed_n].bak, sizeof(s_rm_changed[0].bak), "%s", bak ? bak : "");
    s_rm_changed[s_rm_changed_n].root = root;
    s_rm_changed_n++;
}

/**
 * @brief 把一条改动从 s_rm_changed 去掉（单项恢复成功之后）
 * @param root 记录时的根（同一子键名在 HKCU/HKLM 各算一条，别删串了）
 * @param sub 子键路径（相对根，含 Software\Classes）
 * @return void 无返回值
 */
static void rm_changed_remove(int root, const char *sub)
{
    int i;

    for (i = 0; i < s_rm_changed_n; i++)
        if (s_rm_changed[i].root == root && strcmp(s_rm_changed[i].sub, sub) == 0) {
            s_rm_changed[i] = s_rm_changed[s_rm_changed_n - 1];
            s_rm_changed_n--;
            return;
        }
}

/**
 * @brief 对一个扫描结果行做「导出备份 + 整键删除」
 * @param app 应用
 * @param r 扫描结果行下标
 * @return int 0 = 备份并删除成功；-1 = 中止（备份没成功或删键失败）
 * @note 备份先成功才敢删 —— reg export 没过就中止，绝不出现"删了没备份"。
 *       reg.exe export 落在数据目录，文件名带时间戳 + 序号，同秒连删也不互相覆盖。
 *       根按行自己记的（HKCU/HKLM）走；批量删由调用方在循环后统一重扫一次。
 */
static int rm_delete_row(cg_app_t *app, int r)
{
    const char *root = k_rm_roots[s_rm_rows[r].root];
    char  rel[1200];
    char  name[96];
    char *ts, *bak = NULL;
    char  cmd[2600];
    int   attempt;

    ts = cg_time_now("rmenu-%Y%m%d-%H%M%S");
    for (attempt = 0; attempt < 100; attempt++) {
        snprintf(name, sizeof(name), "%s-%d.reg", ts ? ts : "rmenu-backup", s_rm_bakseq++);
        bak = cg_path_join(s_rm_bakdir[0] ? s_rm_bakdir : ".", name);
        if (bak && !cg_file_exists(bak)) break;
        cg_free(bak);
        bak = NULL;
    }
    cg_free(ts);
    if (!bak) {
        tb_status_msg(app, "备份失败：拿不到可用的备份文件名");
        tb_log_line(app, "备份失败：文件名冲突或路径不可用");
        return -1;
    }

    snprintf(rel, sizeof(rel), "%s\\%s", root, s_rm_rows[r].sub);
    snprintf(cmd, sizeof(cmd), "reg export \"%s\" \"%s\" /y", rel, bak);
    if (cg_run_process(cmd, 1) != 0) {
        tb_status_msg(app, "备份失败：reg export 没成功，已中止删除");
        tb_log_line(app, "reg export 失败：%s", cmd);
        cg_free(bak);
        return -1;
    }

    if (cg_reg_del_key_ex(root, s_rm_rows[r].sub) != 0) {
        tb_status_msg(app, "删除失败（备份仍留在 %s）", cg_path_name(bak));
        tb_log_line(app, "删键失败（备份已留）：%s\\%s", root, s_rm_rows[r].sub);
        rm_changed_add(s_rm_rows[r].root, s_rm_rows[r].sub, bak);
        cg_free(bak);
        rm_changed_sync(app);
        return -1;
    }

    rm_changed_add(s_rm_rows[r].root, s_rm_rows[r].sub, bak);
    tb_log_line(app, "已删除右键菜单项并备份：%s\\%s -> %s", root, s_rm_rows[r].sub, bak);
    tb_status_msg(app, "已删除并备份到 %s", cg_path_name(bak));
    cg_free(bak);
    return 0;
}

/**
 * @brief 「禁用选中（隐藏）」：给目标项写 LegacyDisable 空值
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 写空串 = 值「存在」= 菜单项被隐藏；恢复要删掉这个值（见 on_rm_enable_click）。
 *       目标 = 勾选的行（批量），一条没勾就退化成选中的那一行；
 *       扩展项、系统区未提权的行按条跳过并写日志，绝不静默失败。
 */
static void on_rm_disable_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       tg[K_RM_BATCH];
    int       n, i, admin, ok = 0, skip = 0, fail = 0;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过禁用右键菜单项（不碰注册表）");
        return;
    }
    n = rm_target_rows(app, tg, K_RM_BATCH);
    if (n == 0) { tb_status_msg(app, "先勾选几行（或点一行选中）再操作"); return; }
    admin = cg_reg_is_admin();

    for (i = 0; i < n; i++) {
        int         di = tg[i];
        const char *why = rm_block_reason(di, admin);

        if (why) {
            skip++;
            tb_log_line(app, "跳过禁用 %s\\%s：%s", k_rm_roots[s_rm_rows[di].root],
                        s_rm_rows[di].sub, why);
            continue;
        }
        if (s_rm_rows[di].kind == RM_NOCOMMAND) {
            skip++;
            tb_log_line(app, "跳过禁用 %s\\%s：没有命令（多为 shellex 残键），禁用没有意义",
                        k_rm_roots[s_rm_rows[di].root], s_rm_rows[di].sub);
            continue;
        }
        if (s_rm_rows[di].kind == RM_HIDDEN) { skip++; continue; }   /* 已经是隐藏的，不用重复写 */
        if (cg_reg_write_ex(k_rm_roots[s_rm_rows[di].root], s_rm_rows[di].sub,
                            "LegacyDisable", "") != 0) {
            fail++;
            tb_log_line(app, "禁用失败：%s\\%s", k_rm_roots[s_rm_rows[di].root], s_rm_rows[di].sub);
            continue;
        }
        rm_changed_add(s_rm_rows[di].root, s_rm_rows[di].sub, NULL);
        tb_log_line(app, "已隐藏右键菜单项：%s\\%s", k_rm_roots[s_rm_rows[di].root],
                    s_rm_rows[di].sub);
        ok++;
    }

    if (ok == 0 && fail == 0) {
        tb_status_msg(app, "没有可禁用的项（%d 项跳过：已隐藏 / 扩展项 / 系统区未提权，见日志）", skip);
        return;
    }
    rm_scan(app);
    if (fail) tb_status_msg(app, "禁用完成：%d 项成功，%d 项失败，%d 项跳过（详见日志）", ok, fail, skip);
    else tb_status_msg(app, "已隐藏 %d 项（写了 LegacyDisable，可用「恢复选中」还原；%d 项跳过）",
                       ok, skip);
}

/**
 * @brief 「恢复选中」：删掉目标项的 LegacyDisable 值，菜单项重新出现
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 目标口径与 on_rm_disable_click 相同：勾选优先，没勾选就用选中行。
 */
static void on_rm_enable_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       tg[K_RM_BATCH];
    int       n, i, admin, ok = 0, skip = 0, fail = 0;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过恢复右键菜单项（不碰注册表）");
        return;
    }
    n = rm_target_rows(app, tg, K_RM_BATCH);
    if (n == 0) { tb_status_msg(app, "先勾选几行（或点一行选中）再操作"); return; }
    admin = cg_reg_is_admin();

    for (i = 0; i < n; i++) {
        int         di = tg[i];
        const char *why = rm_block_reason(di, admin);

        if (why) {
            skip++;
            tb_log_line(app, "跳过恢复 %s\\%s：%s", k_rm_roots[s_rm_rows[di].root],
                        s_rm_rows[di].sub, why);
            continue;
        }
        if (s_rm_rows[di].kind != RM_HIDDEN) { skip++; continue; }   /* 没被隐藏的不用恢复 */
        if (cg_reg_del_ex(k_rm_roots[s_rm_rows[di].root], s_rm_rows[di].sub,
                          "LegacyDisable") != 0) {
            fail++;
            tb_log_line(app, "恢复失败：%s\\%s", k_rm_roots[s_rm_rows[di].root], s_rm_rows[di].sub);
            continue;
        }
        rm_changed_remove(s_rm_rows[di].root, s_rm_rows[di].sub);
        tb_log_line(app, "已恢复右键菜单项：%s\\%s", k_rm_roots[s_rm_rows[di].root],
                    s_rm_rows[di].sub);
        ok++;
    }

    if (ok == 0 && fail == 0) {
        tb_status_msg(app, "没有可恢复的项（%d 项跳过：不是隐藏状态 / 扩展项 / 系统区未提权）", skip);
        return;
    }
    rm_scan(app);
    if (fail) tb_status_msg(app, "恢复完成：%d 项成功，%d 项失败，%d 项跳过（详见日志）", ok, fail, skip);
    else tb_status_msg(app, "已恢复 %d 项（删掉了 LegacyDisable；%d 项跳过）", ok, skip);
}

/**
 * @brief 「备份并删除选中」：先弹模态确认框，确认后才导出备份 + 整键删除
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 破坏性操作用 cg_dialog_open 做真确认（cg_message_box 没有返回值，
 *       拿不到"用户点了哪个"）。待删行放 s_rm_pending_rows，确认框回调只负责关框。
 *       不能动的行（扩展项 / 系统区未提权）在**弹框之前**就滤掉并写日志 ——
 *       弹了框才跳过，用户会以为全删了。
 */
static void on_rm_backup_del_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *dlg;
    char         txt[1600];
    char         list[900];
    char         extra[160];
    int          tg[K_RM_BATCH];
    int          n, i, admin, off = 0, skip = 0, done = 0, fail = 0;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过删除右键菜单项（不碰注册表）");
        return;
    }
    n = rm_target_rows(app, tg, K_RM_BATCH);
    if (n == 0) { tb_status_msg(app, "先勾选几行（或点一行选中）再操作"); return; }
    admin = cg_reg_is_admin();

    s_rm_pending_n = 0;
    for (i = 0; i < n; i++) {
        const char *why = rm_block_reason(tg[i], admin);
        if (why) {
            skip++;
            tb_log_line(app, "跳过删除 %s\\%s：%s", k_rm_roots[s_rm_rows[tg[i]].root],
                        s_rm_rows[tg[i]].sub, why);
            continue;
        }
        s_rm_pending_rows[s_rm_pending_n++] = tg[i];
    }
    if (s_rm_pending_n == 0) {
        tb_status_msg(app, "没有可删除的项（%d 项跳过：扩展项只显示不给动 / 系统区未提权）", skip);
        return;
    }

    /* 确认文案最多列 4 条，长路径也要写得下（snprintf 超限时返回"本应写入"的长度，别直接加） */
    list[0] = 0;
    for (i = 0; i < s_rm_pending_n && i < 4; i++) {
        int    di = s_rm_pending_rows[i];
        size_t left = sizeof(list) - (size_t)off;
        int    w;

        if (left < 100) break;
        w = snprintf(list + off, left, "%s\\%s\r\n",
                     k_rm_roots[s_rm_rows[di].root], s_rm_rows[di].sub);
        if (w < 0) break;
        off += (w < (int)left) ? w : (int)left - 1;
    }
    if (s_rm_pending_n > 4) {
        size_t left = sizeof(list) - (size_t)off;
        if (left >= 40) snprintf(list + off, left, "……等共 %d 项\r\n", s_rm_pending_n);
    }
    if (skip) snprintf(extra, sizeof(extra),
                       "\r\n\r\n另有 %d 项不能动，已跳过（扩展项只显示不给动 / 系统区未提权，见日志）。", skip);
    else extra[0] = 0;

    snprintf(txt, sizeof(txt),
             "将整键删除这 %d 个右键菜单项（逐个先导出 .reg 备份到数据目录）：\r\n\r\n%s\r\n"
             "之后可用「还原本次改动」或直接双击备份 .reg 文件导回。%s",
             s_rm_pending_n, list, extra);
    dlg = cg_find(app, "dlg_rm_confirm");
    if (dlg) cg_widget_set_text(cg_find(app, "edt_rmc_text"), txt);

    if (cg_dialog_open(dlg) != 1) {
        s_rm_pending_n = 0;
        tb_log_line(app, "删除右键菜单项：用户取消");
        tb_status_msg(app, "已取消删除");
        return;
    }

    for (i = 0; i < s_rm_pending_n; i++)
        if (rm_delete_row(app, s_rm_pending_rows[i]) == 0) done++;
        else fail++;
    s_rm_pending_n = 0;

    rm_scan(app);
    if (fail) tb_status_msg(app, "已删除 %d 项，%d 项失败（备份都留着，详见日志）", done, fail);
    else tb_status_msg(app, "已删除并备份 %d 项（可用「还原本次改动」导回）", done);
}

/**
 * @brief 「还原本次改动」：禁用过的删掉 LegacyDisable、删过的从备份 .reg 导回
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 逐项处理，成功的从改动记录里剔除；失败的留在记录里（提示与「曾改动」都对得上）。
 */
static void on_rm_restore_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       i, keep = 0, done = 0, fail = 0;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        tb_log_line(app, "自检模式：跳过还原右键改动（不碰注册表）");
        return;
    }
    if (s_rm_changed_n == 0) {
        tb_status_msg(app, "本次会话还没有改动过右键菜单");
        return;
    }

    for (i = 0; i < s_rm_changed_n; i++) {
        int         good;
        const char *root = k_rm_roots[s_rm_changed[i].root];

        if (s_rm_changed[i].bak[0]) {
            char cmd[1400];
            snprintf(cmd, sizeof(cmd), "reg import \"%s\"", s_rm_changed[i].bak);
            good = (cg_run_process(cmd, 1) == 0);
            if (!good) tb_log_line(app, "reg import 失败：%s", cmd);
        } else {
            good = (cg_reg_del_ex(root, s_rm_changed[i].sub, "LegacyDisable") == 0);
            if (!good) tb_log_line(app, "取消隐藏失败：%s\\%s", root, s_rm_changed[i].sub);
        }
        if (good) done++;
        else { fail++; s_rm_changed[keep++] = s_rm_changed[i]; }   /* 只留没还原成功的 */
    }
    s_rm_changed_n = keep;

    tb_log_line(app, "还原本次右键改动：成功 %d / 失败 %d", done, fail);
    rm_scan(app);
    /* 两条分支各自带自己的占位符：写成「三目 + 两个实参」时编译器查不了非字面量的格式串，
     * 以后改文案很容易留一个多余的 %d 而没人发现 */
    if (fail == 0) tb_status_msg(app, "已还原本次改动（%d 项）", done);
    else tb_status_msg(app, "还原了 %d 项，%d 项失败（仍记在「曾改动」里）", done, fail);
}

/**
 * @brief 删除确认框的「备份并删除」按钮：关框并带回结果 1
 * @param self 确认框里的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_rmc_ok_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_dialog_close(self, 1);
}

/**
 * @brief 删除确认框的「取消」按钮：关框并带回结果 0
 * @param self 确认框里的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_rmc_cancel_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_dialog_close(self, 0);
}

/** 事件名 -> C 函数 的登记表（右边这些名字写在 toolbox.cgui 里） */
const cg_binding_t tb_rmenu_bindings[] = {
    CG_BIND("on_rmenu_click",            on_rmenu_click),
    CG_BIND("on_btn_regmenu_scan_click", on_btn_regmenu_scan_click),
    CG_BIND("on_rm_tab_click",           on_rm_tab_click),
    CG_BIND("on_rm_filter_click",        on_rm_filter_click),
    CG_BIND("on_rm_search_change",       on_rm_search_change),
    CG_BIND("on_rm_check",               on_rm_check),
    CG_BIND("on_rm_disable_click",       on_rm_disable_click),
    CG_BIND("on_rm_enable_click",        on_rm_enable_click),
    CG_BIND("on_rm_backup_del_click",    on_rm_backup_del_click),
    CG_BIND("on_rm_restore_click",       on_rm_restore_click),
    CG_BIND("on_rmc_ok_click",           on_rmc_ok_click),
    CG_BIND("on_rmc_cancel_click",       on_rmc_cancel_click),
    CG_BIND_END
};
