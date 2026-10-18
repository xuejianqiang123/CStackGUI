/**
 * @file    en.c
 * @brief   envsnap —— 套件模块（原 examples\envsnap 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\suite-merge.mjs（.cgui 侧）+ build\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 en_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 en_bindings、原 on_ready 变成 en_init、
 *          窗口事件改成直连转发 en_app_<event>（套件只有一个窗口，
 *          按名派发只能接一个，其余工具的窗口职责会被静默吞掉）。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 */

/**
 * @file    main.c
 * @brief   环境快照：INI 清单驱动的懒加载树 + 环境变量与用户数据目录的快照
 * @details 采集哪些环境变量、各挂哪个分组，不再写死在代码里，而是放在清单文件
 *          envsnap.ini（UTF-8，默认在 exe 同目录，第一次运行时按内置清单生成）：
 *
 *              [身份]
 *              COMPUTERNAME=计算机名
 *              USERNAME=用户名
 *
 *          方括号是分组（树里的一支），键是变量名，值是备注（可空）。
 *          工具栏第二排就是对这份清单的增删改查：
 *              导入INI —— 选任意一份 ini 重新载入清单，此后的增删改都存回它；
 *              添加 —— 打开一张表单（分组 / 变量名 / 备注 一次填完、一次提交）；
 *              删除 —— 删选中的变量，选中分组则整组连带删除；
 *              修改 —— 同一张表单预填现值，可改名 / 挪分组 / 改备注；
 *              查询 —— 按关键字过滤清单（变量名 / 备注 / 分组），结果摆进右侧表格。
 *          添加与修改共用 .cgui 里的 dlg_edit 表单（cg_dialog_open 模态打开）：
 *          校验不过就只改框内提示行、**不关框**，改完直接再点确定 —— 而不是连着弹三个框。
 *          每次改动立即存回当前 ini —— 文件是清单的唯一事实源，
 *          记事本手工改完点「导入INI」一样生效。
 *
 *          左侧树依旧是**懒加载**：点开哪个才枚举哪个 —— 用户数据目录可能有几百上千
 *          个文件，一次全展开会把消息循环堵住、界面假死（与资源管理器"展开才读"同理）。
 *          右侧表格显示当前节点的条目，底部终端记日志、状态栏报状态。
 *
 *          为什么不做"注册表浏览器"：运行时只提供 `cg_reg_read` / `cg_reg_write`
 *          （**只有 HKCU、只有字符串值**），没有枚举子键 / 列举值的接口，
 *          浏览树、值列表、导出 .reg 全都无从实现 —— 所以这里换成同方向的"环境与目录快照"。
 *          示例里发现的能力边界要照实说，而不是自己 include Windows 头绕过去。
 *
 *          采集全部来自运行时：环境变量按名读、目录按索引逐条列、条目再问大小与是否目录。
 *          界面在 envsnap.cgui 里；本文件只有行为。
 *          编译：在项目根跑 build-app.bat envsnap
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 内置清单与限额 */

/* 内置清单（种子）：exe 旁没有 envsnap.ini 时按它生成一份；自检（无窗口）也用它，
 * 保证自检零磁盘副作用。分组名决定它挂在树里哪一支，备注一律空。 */
static const struct {
    const char *name;    /**< 变量名 */
    const char *group;   /**< 树里的分组名 */
} k_seed_envs[] = {
    { "COMPUTERNAME", "身份" },         { "USERNAME", "身份" },           { "USERDOMAIN", "身份" },
    { "USERPROFILE", "路径" },          { "HOMEDRIVE", "路径" },          { "HOMEPATH", "路径" },
    { "APPDATA", "路径" },              { "LOCALAPPDATA", "路径" },       { "ProgramData", "路径" },
    { "ProgramFiles", "路径" },         { "ProgramFiles(x86)", "路径" },  { "OneDrive", "路径" },
    { "TEMP", "路径" },                 { "TMP", "路径" },                { "SystemRoot", "路径" },
    { "windir", "路径" },               { "SystemDrive", "路径" },        { "ComSpec", "路径" },
    { "PATHEXT", "路径" },              { "PATH", "路径" },
    { "NUMBER_OF_PROCESSORS", "系统" }, { "PROCESSOR_ARCHITECTURE", "系统" },
    { "PROCESSOR_IDENTIFIER", "系统" }, { "OS", "系统" },
    { "LANG", "区域" },                 { "LC_ALL", "区域" },
    { "SESSIONNAME", "会话" },          { "LOGONSERVER", "会话" }
};

/** 种子条目数 */
#define K_SEED_COUNT ((int)(sizeof k_seed_envs / sizeof k_seed_envs[0]))
/** 清单最多多少个变量（INI 里超出的不载入，日志提示） */
#define K_ENV_MAX    256
/** 清单最多多少个分组 */
#define K_GROUP_MAX  32
/** 用户数据目录最多列多少个条目（超出的只计数、不进树，免得界面被塞爆） */
#define K_DIR_MAX    200
/** 扩展名分类最多几类 */
#define K_EXT_MAX    48
/** 文本缓冲大小：业务不 malloc（工程铁律），固定预留这么多，写满即截断 */
#define K_SNAP_MAX   (96 * 1024)

/* ------------------------------------------------------------ 数据模型 */

/** 树节点类别 */
enum { NK_NONE = 0, NK_ENV_ROOT, NK_DIR_ROOT, NK_GROUP, NK_ENV, NK_EXT, NK_DIRLIST, NK_FILE };

/** 树节点表的一行；下标与树控件的节点下标一一对应（运行时只追加，下标因此稳定） */
typedef struct {
    int  kind;          /**< 节点类别，见 NK_* */
    int  loaded;        /**< 子节点是否已建过（懒加载标记） */
    char group[48];     /**< 所属分组（NK_ENV 用：选中变量时靠它定位清单行） */
    char data[96];      /**< 承载的数据：组名 / 变量名 / 扩展名 / 文件名 */
} cg_node_t;

/** 清单里的一行：某个分组下的一个环境变量（备注可空） */
typedef struct {
    char group[48];     /**< 分组名 */
    char name[64];      /**< 变量名 */
    char note[96];      /**< 备注（INI 里 = 号后面的内容，可空） */
} envitem_t;

/** 用户数据目录里的一个条目 */
typedef struct {
    char      name[128];   /**< 文件 / 目录名 */
    char      ext[24];     /**< 小写扩展名（含点）；没有扩展名是空串 */
    int       is_dir;      /**< 1 = 目录 */
    long long size;        /**< 文件字节数；目录 / 取不到为 -1 */
} entry_t;

/** 按扩展名归类统计的一格 */
typedef struct {
    char      ext[24];     /**< 扩展名 */
    int       files;       /**< 文件个数 */
    long long bytes;       /**< 累计字节数 */
} extstat_t;

static cg_node_t s_nodes[1024];         /**< 节点表：够 256 变量 + 32 组 + 200 文件条目 + 分类 */
static int       s_node_count;          /**< 已用节点数，也是下一次加节点该拿到的下标 */

static envitem_t s_items[K_ENV_MAX];    /**< 清单：环境变量（启动 / 导入时载入，增删改查都改它） */
static int       s_item_count;          /**< 清单已用行数 */
static char      s_groups[K_GROUP_MAX][48]; /**< 分组名表（对应 INI 的节，空节也保留） */
static int       s_group_count;         /**< 分组数 */
static char      s_ini_path[1024];      /**< 当前清单文件；空串 = 还没有（自检模式不碰盘） */

/** 编辑对话框（dlg_edit）这次是干嘛的 */
enum { DLG_ADD = 0, DLG_EDIT = 1 };
static int  s_dlg_mode;                 /**< DLG_ADD / DLG_EDIT */
static int  s_dlg_idx;                  /**< DLG_EDIT：要改的行号（添加时是 -1） */
static char s_dlg_old[120];             /**< DLG_EDIT：原「组\名」，写日志用 */

static entry_t   s_entries[K_DIR_MAX];  /**< 目录条目快照（刷新时重扫） */
static int       s_entry_count;         /**< 已登记的条目数 */
static int       s_dir_count;           /**< 其中目录个数 */
static int       s_scanned;             /**< 目录扫过没有；0 = 需要重扫 */
static extstat_t s_exts[K_EXT_MAX];     /**< 扩展名分类结果 */
static int       s_ext_count;           /**< 分类数 */

static char s_snap[K_SNAP_MAX];         /**< 文本级缓冲：导出快照与拼 INI 文本共用（见各使用者） */
static int  s_snap_len;                 /**< 已写入字节数 */

/* ------------------------------------------------------------ 公共小工具 */

/**
 * @brief 往终端写一行日志，并镜像到控制台方便自检时看时序
 * @param app 应用；可为 NULL（那就不写终端）
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
static void log_line(cg_app_t *app, const char *fmt, ...)
{
    char    buf[512];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (app) {
        cg_widget_t *term = cg_find(app, "term_log");

        if (term) cg_term_write(term, CG_TERM_SYS, buf, -1);
    }
    printf("[envsnap] %s\n", buf);
}

/**
 * @brief 把一条消息写进状态栏第 0 段
 * @param app 应用；可为 NULL
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
static void status_msg(cg_app_t *app, const char *fmt, ...)
{
    char    buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (app) cg_status_set(cg_find(app, "sb_main"), 0, buf);
}

/**
 * @brief 把字节数格式化成好读的 "1.2 GB" / "345.1 MB" / "12 KB"
 * @param bytes 字节数
 * @param out 输出缓冲
 * @param cap out 的字节数
 * @return void 无返回值
 */
static void fmt_size(unsigned long long bytes, char *out, int cap)
{
    if (bytes >= 1024ull * 1024ull * 1024ull) snprintf(out, (size_t)cap, "%.2f GB", (double)bytes / 1073741824.0);
    else if (bytes >= 1024ull * 1024ull)      snprintf(out, (size_t)cap, "%.1f MB", (double)bytes / 1048576.0);
    else                                      snprintf(out, (size_t)cap, "%llu KB", bytes / 1024ull);
}

/**
 * @brief 把可能很长的环境变量值收成一行摘要（表格与导出都别撑爆）
 * @param v 原始值；NULL = 未设置
 * @param out 输出缓冲
 * @param cap out 的字节数
 * @return void 无返回值
 */
static void value_summary(const char *v, char *out, int cap)
{
    if (!v) { snprintf(out, (size_t)cap, "(未设置)"); return; }
    if ((int)strlen(v) <= 72) { snprintf(out, (size_t)cap, "%s", v); return; }
    snprintf(out, (size_t)cap, "%.72s…", v);
}

/**
 * @brief 去掉字符串首尾的空格 / 制表 / 回车（INI 解析与输入框取值都用）
 * @param s 原地修改的字符串
 * @return void 无返回值
 */
static void str_trim(char *s)
{
    char *p = s;
    char *q;

    while (*p == ' ' || *p == '\t' || *p == '\r') p++;
    q = p + strlen(p);
    while (q > p && (q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\r')) q--;
    *q = '\0';
    if (p != s) memmove(s, p, (size_t)(q - p + 1));
}

/**
 * @brief 把 s 里出现在 bad 中的字符删掉（防把 INI 格式写坏：组名带 ]、键名带 = 等）
 * @param s 原地修改的字符串
 * @param bad 要删掉的字符集合
 * @return void 无返回值
 */
static void str_clean(char *s, const char *bad)
{
    char *w = s;

    for (; *s; s++) {
        if (strchr(bad, *s)) continue;
        *w++ = *s;
    }
    *w = '\0';
}

/**
 * @brief 大小写不敏感的"包含"判断（ASCII；中文按 UTF-8 字节精确匹配）
 * @param hay 被搜索串
 * @param needle 关键字
 * @return int 1 = 包含，0 = 不包含；关键字为空串恒 1
 */
static int icontains(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    size_t i;

    if (nl == 0) return 1;
    for (; *hay; hay++) {
        for (i = 0; i < nl; i++) {
            char a = hay[i];
            char b = needle[i];

            if (a == '\0') return 0;
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (i == nl) return 1;
    }
    return 0;
}

/**
 * @brief 清空文本缓冲，准备重新生成
 * @return void 无返回值
 */
static void snap_reset(void)
{
    s_snap_len = 0;
    s_snap[0]  = '\0';
}

/**
 * @brief 往文本缓冲追加一段文本（写满就停在缓冲末尾，不再增长）
 * @param fmt printf 风格格式串
 * @return void 无返回值
 */
static void snap_addf(const char *fmt, ...)
{
    va_list ap;
    int     room;

    if (s_snap_len >= K_SNAP_MAX - 1) return;
    room = K_SNAP_MAX - s_snap_len;

    va_start(ap, fmt);
    vsnprintf(s_snap + s_snap_len, (size_t)room, fmt, ap);
    va_end(ap);

    s_snap_len += (int)strlen(s_snap + s_snap_len);   /* 追加完再量一次长度，接着往后写 */
}

/* ------------------------------------------------------------ 清单（INI 模型） */

/**
 * @brief 清空清单（变量与分组都清），载入新清单前调用
 * @return void 无返回值
 */
static void model_reset(void)
{
    s_item_count  = 0;
    s_group_count = 0;
}

/**
 * @brief 查分组在分组表里的下标
 * @param g 分组名
 * @return int 下标；没有返回 -1
 */
static int group_index(const char *g)
{
    int i;

    for (i = 0; i < s_group_count; i++)
        if (strcmp(s_groups[i], g) == 0) return i;
    return -1;
}

/**
 * @brief 分组不存在就追加一行（空名 / 表满返回 -1）
 * @param g 分组名
 * @return int 分组下标；-1 = 没建上
 */
static int group_ensure(const char *g)
{
    if (!g[0]) return -1;
    if (group_index(g) >= 0) return group_index(g);
    if (s_group_count >= K_GROUP_MAX) return -1;
    snprintf(s_groups[s_group_count], sizeof s_groups[0], "%s", g);
    return s_group_count++;
}

/**
 * @brief 删掉分组表里的一行（后面的整体前移）
 * @param gi 分组下标（调用方保证合法）
 * @return void 无返回值
 */
static void group_remove(int gi)
{
    memmove(&s_groups[gi], &s_groups[gi + 1],
            (size_t)(s_group_count - gi - 1) * sizeof s_groups[0]);
    s_group_count--;
}

/**
 * @brief 查某个"分组 + 变量名"在清单里的行号
 * @param g 分组名
 * @param name 变量名
 * @return int 行号；没有返回 -1
 */
static int item_find(const char *g, const char *name)
{
    int i;

    for (i = 0; i < s_item_count; i++)
        if (strcmp(s_items[i].group, g) == 0 && strcmp(s_items[i].name, name) == 0) return i;
    return -1;
}

/**
 * @brief 往清单追加一行（表满返回 -1；分组行由 group_ensure 负责）
 * @param g 分组名
 * @param name 变量名
 * @param note 备注（可空）
 * @return int 0 成功、-1 清单满
 */
static int model_add(const char *g, const char *name, const char *note)
{
    envitem_t *it;

    if (s_item_count >= K_ENV_MAX) return -1;
    it = &s_items[s_item_count++];
    snprintf(it->group, sizeof it->group, "%s", g);
    snprintf(it->name,  sizeof it->name,  "%s", name);
    snprintf(it->note,  sizeof it->note,  "%s", note);
    return 0;
}

/**
 * @brief 删掉清单里的一行（后面的整体前移）
 * @param idx 行号（调用方保证合法）
 * @return void 无返回值
 */
static void item_remove(int idx)
{
    memmove(&s_items[idx], &s_items[idx + 1],
            (size_t)(s_item_count - idx - 1) * sizeof s_items[0]);
    s_item_count--;
}

/**
 * @brief 把内置清单装进模型（自检与"生成默认 ini"都用它）
 * @return void 无返回值
 */
static void model_load_seed(void)
{
    int i;

    model_reset();
    for (i = 0; i < K_SEED_COUNT; i++) {
        group_ensure(k_seed_envs[i].group);
        model_add(k_seed_envs[i].group, k_seed_envs[i].name, "");
    }
}

/**
 * @brief 解析一份 INI 文本进清单（先清空模型）
 * @param text 整份 INI 文本（UTF-8）；NULL 视为空
 * @param app 应用；可为 NULL（那就不写日志）
 * @return int 载入的变量条数（0 = 空清单，也算成功）
 * @note 容错口径：BOM / \r\n 都吃得下；`;` `#` 开头是注释；
 *       没有 = 的裸键备注记空；还没进任何节的键丢弃；重复键取先出现的并计数提示
 */
static int ini_parse(const char *text, cg_app_t *app)
{
    char       line[512];
    char       cur[48];
    const char *p = text;
    int        loaded = 0;
    int        dup    = 0;

    model_reset();
    cur[0] = '\0';
    if (!p) return 0;
    if ((unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;

    while (*p) {
        char *eq;
        char *key;
        char *val;
        int   len = 0;

        while (p[len] && p[len] != '\n' && len < (int)sizeof(line) - 1) len++;
        memcpy(line, p, (size_t)len);
        line[len] = '\0';
        p += len;
        if (*p == '\n') p++;

        str_trim(line);
        if (!line[0] || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[') {                          /* [分组]：登记分组，成为后续键的归属 */
            char *end = strchr(line, ']');

            if (end) {
                *end = '\0';
                str_trim(line + 1);
                if (group_ensure(line + 1) < 0) {
                    /* 组满或空名：这组连同名下的键一起丢。cur 若还留着组名，键会进
                     * "不在分组表里"的孤儿行 —— 树里不显示、保存时也丢，与提示口径不一致 */
                    if (s_group_count >= K_GROUP_MAX)
                        log_line(app, "INI 分组太多（上限 %d），[%s] 名下的组与变量被丢弃", K_GROUP_MAX, line + 1);
                    cur[0] = '\0';
                } else {
                    snprintf(cur, sizeof cur, "%.47s", line + 1);   /* 限长与分组表一致，gcc 才证得出装得下 */
                }
            }
            continue;
        }

        eq = strchr(line, '=');
        if (eq) { *eq = '\0'; val = eq + 1; }
        else      val = "";
        key = line;
        str_trim(key);
        str_trim(val);
        if (!cur[0] || !key[0]) continue;              /* 节外的键 / 空键：丢弃 */

        if (item_find(cur, key) >= 0) { dup++; continue; }
        if (model_add(cur, key, val) != 0) {
            log_line(app, "清单已满（上限 %d 个变量），%s\\%s 没载入", K_ENV_MAX, cur, key);
            continue;
        }
        loaded++;
    }

    if (dup) log_line(app, "INI 里有 %d 个重复条目，取先出现的", dup);
    return loaded;
}

/**
 * @brief 从文件载入清单（读整份文本再解析；先清空模型）
 * @param path ini 文件路径（UTF-8）
 * @param app 应用；可为 NULL
 * @return int 载入的变量条数；-1 = 文件读不到 / 读不出
 */
static int ini_load(const char *path, cg_app_t *app)
{
    char *text = cg_file_read(path);
    int   n;

    if (!text) {
        log_line(app, "INI 读不到或读不出：%s", path);
        return -1;
    }
    n = ini_parse(text, app);
    cg_free(text);
    return n;
}

/**
 * @brief 把清单整份写成 INI 文本并落盘（分组按表顺序，变量按清单顺序）
 * @param path 目标路径（UTF-8，整文件覆盖）
 * @param app 应用；可为 NULL
 * @return int 0 成功、-1 写盘失败
 * @note 借用 s_snap 缓冲拼文本：256 行 × 最长约 212 字节 ≈ 54KB，96KB 的缓冲装得下；
 *       导出快照与存清单不会同时发生（都是按钮回调里的同步动作）
 */
static int ini_save(const char *path, cg_app_t *app)
{
    int g, i;

    snap_reset();
    for (g = 0; g < s_group_count; g++) {
        snap_addf("[%s]\r\n", s_groups[g]);
        for (i = 0; i < s_item_count; i++)
            if (strcmp(s_items[i].group, s_groups[g]) == 0)
                snap_addf("%s=%s\r\n", s_items[i].name, s_items[i].note);
    }
    if (cg_file_write(path, s_snap) != 0) {
        log_line(app, "清单写盘失败：%s", path);
        return -1;
    }
    return 0;
}

/**
 * @brief 增删改之后把清单存回当前 ini（还没有路径就不存 —— 自检不会走到这）
 * @param app 应用
 * @return void 无返回值
 */
static void crud_save(cg_app_t *app)
{
    if (!s_ini_path[0]) return;
    if (ini_save(s_ini_path, app) != 0) status_msg(app, "清单存盘失败，详见日志");
}

/* ------------------------------------------------------------ 树与采集 */

/**
 * @brief 往树里加一个节点，同时在节点表里留一行
 * @param app 应用
 * @param parent 父节点下标；-1 = 加在根上
 * @param kind 节点类别，见 NK_*
 * @param grp 所属分组（NK_ENV 用；其余传 NULL）
 * @param data 节点承载的数据（组名 / 变量名 / 扩展名 / 文件名）；可为 NULL
 * @param text 节点显示文字
 * @return int 新节点下标；建不上或对不上号返回 -1
 * @note 运行时是追加式建节点，控件返回的下标必然等于本表下标；
 *       一旦对不上（例如表满了）就只让它显示、不记录 —— 宁可少一点，也别让记录错位
 */
static int node_add(cg_app_t *app, int parent, int kind, const char *grp,
                    const char *data, const char *text)
{
    cg_widget_t *tree = app ? cg_find(app, "tree_main") : NULL;
    int          item;

    if (!tree) return -1;
    item = cg_tree_add(tree, parent, text);
    if (item < 0 || item >= (int)(sizeof s_nodes / sizeof s_nodes[0])) return -1;
    if (item != s_node_count) return -1;                  /* 下标对不上：只显示，不记录 */

    s_nodes[item].kind   = kind;
    s_nodes[item].loaded = 0;
    snprintf(s_nodes[item].group, sizeof s_nodes[item].group, "%s", grp ? grp : "");
    snprintf(s_nodes[item].data, sizeof s_nodes[item].data, "%s", data ? data : "");
    s_node_count = item + 1;
    return item;
}

/**
 * @brief 扫一遍用户数据目录，把条目（名 / 扩展名 / 是否目录 / 大小）收进条目表
 * @param app 应用；可为 NULL（那就不写日志）
 * @param verbose 非 0 时写一行摘要日志
 * @return void 无返回值
 * @note 只扫**一层**，不递归子目录：目录列举本来就是按下标逐条取，要递归得自己搭栈；
 *       快照工具看一层足够，也省得在深目录上把界面拖住
 */
static void scan_data_dir(cg_app_t *app, int verbose)
{
    char *dir = cg_app_data_dir();
    char  name[128];
    int   idx = 0;

    s_entry_count = 0;
    s_dir_count   = 0;
    s_scanned     = 1;

    if (!dir) {
        if (verbose) log_line(app, "用户数据目录：拿不到路径");
        return;
    }

    while (s_entry_count < K_DIR_MAX && cg_dir_list(dir, "*", idx, name, sizeof name)) {
        entry_t    *e    = &s_entries[s_entry_count];
        char       *full = cg_path_join(dir, name);
        const char *ext  = cg_path_ext(name);
        char       *p;

        snprintf(e->name, sizeof e->name, "%s", name);
        snprintf(e->ext, sizeof e->ext, "%s", ext ? ext : "");
        for (p = e->ext; *p; p++) {                        /* .TXT 与 .txt 归一类 */
            if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
        }

        e->is_dir = full ? cg_dir_exists(full) : 0;
        e->size   = e->is_dir ? -1 : (full ? cg_file_size(full) : -1);
        if (e->is_dir) s_dir_count++;

        cg_free(full);
        s_entry_count++;
        idx++;
    }

    if (verbose) log_line(app, "用户数据目录：%s（%d 个条目，目录 %d 个）",
                          dir, s_entry_count, s_dir_count);
    cg_free(dir);
}

/**
 * @brief 按扩展名把条目表里的**文件**归成几类，写进分类表
 * @return void 无返回值
 * @note 只读条目表；换一批条目就得重算一次
 */
static void classify_exts(void)
{
    int i, k;

    s_ext_count = 0;
    for (i = 0; i < s_entry_count; i++) {
        if (s_entries[i].is_dir) continue;
        for (k = 0; k < s_ext_count; k++)
            if (strcmp(s_exts[k].ext, s_entries[i].ext) == 0) break;

        if (k == s_ext_count) {
            if (s_ext_count >= K_EXT_MAX) continue;      /* 分类满：不新建，也就不再计入 */
            snprintf(s_exts[k].ext, sizeof s_exts[k].ext, "%s", s_entries[i].ext);
            s_exts[k].files = 0;
            s_exts[k].bytes = 0;
            s_ext_count++;
        }
        s_exts[k].files++;
        if (s_entries[i].size > 0) s_exts[k].bytes += s_entries[i].size;
    }
}

/**
 * @brief 懒加载：只给一个节点建它的**直接子节点**（已经建过就直接返回）
 * @param app 应用
 * @param item 要展开的节点下标
 * @return void 无返回值
 * @note 这是本示例的核心：整棵树一次递归建完会把消息循环堵住
 *       （几百个文件、每个还要问大小与是否目录），所以一律等用户点开才建一层；
 *       环境变量那侧的分组与变量来自清单模型（INI），不再遍历任何硬编码表
 */
static void load_children(cg_app_t *app, int item)
{
    cg_node_t *n;
    char       label[96];
    int        i, k;

    if (item < 0 || item >= s_node_count) return;
    n = &s_nodes[item];
    if (n->loaded) return;
    n->loaded = 1;

    switch (n->kind) {
    case NK_ENV_ROOT:
        /* 第一层先只放分组（INI 的节，空节也放）；变量本身等分组展开再建 */
        for (i = 0; i < s_group_count; i++)
            node_add(app, item, NK_GROUP, NULL, s_groups[i], s_groups[i]);
        break;

    case NK_GROUP:
        for (i = 0; i < s_item_count; i++)
            if (strcmp(s_items[i].group, n->data) == 0)
                node_add(app, item, NK_ENV, n->data, s_items[i].name, s_items[i].name);
        break;

    case NK_DIR_ROOT:
        if (!s_scanned) scan_data_dir(app, 1);
        classify_exts();
        for (k = 0; k < s_ext_count; k++) {
            /* 扩展名限长 23 字节（数组就 24 字节）：限了 gcc 才证得出装得下，
             * 不限会报 -Wformat-truncation（2026-09-28 实测） */
            snprintf(label, sizeof label, "%.23s  (%d)",
                     s_exts[k].ext[0] ? s_exts[k].ext : "(无扩展名)", s_exts[k].files);
            node_add(app, item, NK_EXT, NULL, s_exts[k].ext, label);
        }
        if (s_dir_count > 0) {
            snprintf(label, sizeof label, "(目录)  (%d)", s_dir_count);
            node_add(app, item, NK_DIRLIST, NULL, "", label);
        }
        break;

    case NK_EXT:
        for (i = 0; i < s_entry_count; i++)
            if (!s_entries[i].is_dir && strcmp(s_entries[i].ext, n->data) == 0)
                node_add(app, item, NK_FILE, NULL, s_entries[i].name, s_entries[i].name);
        break;

    case NK_DIRLIST:
        for (i = 0; i < s_entry_count; i++)
            if (s_entries[i].is_dir)
                node_add(app, item, NK_FILE, NULL, s_entries[i].name, s_entries[i].name);
        break;

    default:
        break;
    }
}

/**
 * @brief 建两个根节点（环境变量 / 用户数据目录），重建场景下也复用
 * @param app 应用
 * @return void 无返回值
 * @note 只挂根节点、不采集；真正的采集交给懒加载在展开时按需做
 */
static void build_roots(cg_app_t *app)
{
    node_add(app, -1, NK_ENV_ROOT, NULL, "环境变量", "环境变量");
    node_add(app, -1, NK_DIR_ROOT, NULL, "用户数据目录", "用户数据目录");
}

/**
 * @brief 把两个根的下一层摆出来（懒加载的"第一次展开"）
 * @param app 应用
 * @return void 无返回值
 * @note 环境变量那侧只读清单模型、不碰磁盘；目录那侧会扫一层（所以只在真窗口下调用）
 */
static void expand_roots(cg_app_t *app)
{
    int i;

    for (i = 0; i < s_node_count; i++)
        if (s_nodes[i].kind == NK_ENV_ROOT || s_nodes[i].kind == NK_DIR_ROOT)
            load_children(app, i);
}

/**
 * @brief 清单变过之后的整树重建：清空树与表格，重建两个根并展开一层
 * @param app 应用
 * @return void 无返回值
 * @note 目录侧沿用内存里已有的条目快照（s_scanned 不动，不重扫盘）；
 *       选中态清空，右侧表格回到「未选中」
 */
static void rebuild_env_tree(cg_app_t *app)
{
    cg_widget_t *tree = cg_find(app, "tree_main");
    cg_widget_t *tbl  = cg_find(app, "tbl_items");

    if (tree) cg_tree_clear(tree);
    if (tbl)  cg_table_clear(tbl);
    s_node_count = 0;
    build_roots(app);
    expand_roots(app);
}

/**
 * @brief 猜「添加」该落在哪个分组：选中的分组 / 变量所在分组，否则第一个分组
 * @param app 应用
 * @param out 输出缓冲
 * @param cap out 的字节数
 * @return void 无返回值（清单为空时 out 是空串，让用户自己填）
 */
static void sel_group(cg_app_t *app, char *out, int cap)
{
    cg_widget_t *tree = cg_find(app, "tree_main");
    int          item = tree ? cg_tree_selected(tree) : -1;

    out[0] = '\0';
    if (item >= 0 && item < s_node_count) {
        if (s_nodes[item].kind == NK_GROUP)      snprintf(out, (size_t)cap, "%s", s_nodes[item].data);
        else if (s_nodes[item].kind == NK_ENV)   snprintf(out, (size_t)cap, "%s", s_nodes[item].group);
    }
    if (!out[0] && s_group_count > 0) snprintf(out, (size_t)cap, "%s", s_groups[0]);
}

/* ------------------------------------------------------------ 右侧表格 */

/**
 * @brief 按当前选中节点，把对应的条目填进右侧表格（填前先清表）
 * @param app 应用
 * @return void 无返回值
 * @note 只读内存里已经采集到的数据：目录 / 扩展名条目来自条目表，变量值按名现读；
 *       自检会合成选中事件，所以这里**不读磁盘、不弹框**
 */
static void fill_table(cg_app_t *app)
{
    cg_widget_t *tree = cg_find(app, "tree_main");
    cg_widget_t *tbl  = cg_find(app, "tbl_items");
    int          item = tree ? cg_tree_selected(tree) : -1;
    cg_node_t   *n;
    int          i;

    if (!tbl) return;
    cg_table_clear(tbl);

    if (item < 0 || item >= s_node_count) {
        const char *cells[3] = { "（未选中）", "", "在上面的树里点一个节点" };

        cg_table_add_row(tbl, cells, 3);
        return;
    }

    n = &s_nodes[item];
    switch (n->kind) {
    case NK_ENV_ROOT: {
        const char *cells[3] = { "环境变量", "", "清单来自 INI：展开看分组，分组下是变量" };

        cg_table_add_row(tbl, cells, 3);
        break;
    }
    case NK_DIR_ROOT: {
        char                sz[32];
        char                note[96];
        const char         *cells[3];
        unsigned long long  total = 0;

        for (i = 0; i < s_entry_count; i++) total += s_entries[i].size;
        fmt_size(total, sz, sizeof(sz));
        snprintf(note, sizeof(note), "条目 %d · 目录 %d · 总大小 %s%s",
                 s_entry_count, s_dir_count, sz, s_scanned ? "" : "（尚未采集）");
        cells[0] = "用户数据目录";
        cells[1] = s_scanned ? sz : "—";
        cells[2] = note;
        cg_table_add_row(tbl, cells, 3);
        break;
    }
    case NK_GROUP: {
        char        note[96];
        const char *cells[3];
        int         vars = 0;

        for (i = 0; i < s_item_count; i++)
            if (strcmp(s_items[i].group, n->data) == 0) vars++;
        snprintf(note, sizeof(note), "分组，含 %d 个变量", vars);
        cells[0] = n->data;
        cells[1] = "分组";
        cells[2] = note;
        cg_table_add_row(tbl, cells, 3);
        break;
    }
    case NK_ENV: {
        char       *v = cg_env(n->data);
        char        sum[80];
        char        note[96];
        const char *cells[3];
        int         idx = item_find(n->group, n->data);

        note[0] = '\0';
        if (idx >= 0) snprintf(note, sizeof(note), "%s", s_items[idx].note);
        value_summary(v, sum, sizeof(sum));
        cells[0] = n->data;
        cells[1] = v ? sum : "（未设置）";
        cells[2] = note[0] ? note : (v ? "已设置" : "未设置");
        cg_table_add_row(tbl, cells, 3);
        cg_free(v);
        break;
    }
    case NK_DIRLIST: {
        const char *cells[3] = { n->data, "", "目录" };

        cg_table_add_row(tbl, cells, 3);
        break;
    }
    case NK_EXT: {
        const char *want = n->data;
        int         hit  = 0;

        for (i = 0; i < s_entry_count; i++) {
            char        sz[32];
            const char *cells[3];

            if (s_entries[i].is_dir) continue;
            if (strcmp(s_entries[i].ext, want) != 0) continue;
            fmt_size(s_entries[i].size > 0 ? (unsigned long long)s_entries[i].size : 0, sz, sizeof(sz));
            cells[0] = s_entries[i].name;
            cells[1] = sz;
            cells[2] = "";
            cg_table_add_row(tbl, cells, 3);
            hit++;
        }
        if (hit == 0) {
            const char *cells[3] = { want[0] ? want : "（无扩展名）", "", "没有匹配到文件" };

            cg_table_add_row(tbl, cells, 3);
        }
        break;
    }
    case NK_FILE: {
        const char *cells[3] = { n->data, "", "文件" };

        cg_table_add_row(tbl, cells, 3);
        break;
    }
    default: {
        const char *cells[3] = { n->data, "", "（未知节点类型）" };

        cg_table_add_row(tbl, cells, 3);
        break;
    }
    }
}

/* ------------------------------------------------------------ 快照 */

/**
 * @brief 把当前已采集到的东西拼成一份文本快照，写进文本缓冲
 * @param app 应用
 * @return void 无返回值
 * @note 段落：时间 / 环境变量清单（按分组、带备注，值超长走摘要）/ 用户数据目录概览 /
 *       扩展名分类表；只拼**已经采集到**的内容，不主动扫盘
 */
static void snapshot_build(cg_app_t *app)
{
    char               *now;
    int                 i, g;
    unsigned long long  total = 0;

    snap_reset();

    now = cg_time_now(NULL);
    snap_addf("==== CStackGUI 环境快照 ====\r\n");
    snap_addf("采集时间：%s\r\n", now ? now : "（读不到）");
    cg_free(now);
    snap_addf("\r\n");

    /* 环境变量：清单（含备注）+ 当前值；清单启动 / 导入时就已整体在模型里，直接遍历 */
    snap_addf("---- 环境变量（清单：%s）----\r\n", s_ini_path[0] ? s_ini_path : "内置清单");
    for (g = 0; g < s_group_count; g++) {
        snap_addf("[%s]\r\n", s_groups[g]);
        for (i = 0; i < s_item_count; i++) {
            char *v;
            char  sum[80];

            if (strcmp(s_items[i].group, s_groups[g]) != 0) continue;
            v = cg_env(s_items[i].name);
            value_summary(v, sum, sizeof(sum));
            if (s_items[i].note[0]) snap_addf("    %s = %s（%s）\r\n", s_items[i].name, sum, s_items[i].note);
            else                    snap_addf("    %s = %s\r\n", s_items[i].name, sum);
            cg_free(v);
        }
    }
    snap_addf("\r\n");

    snap_addf("---- 用户数据目录概览 ----\r\n");
    if (!s_scanned) {
        snap_addf("（还没采集：展开「用户数据目录」或点「重新采集」后再导）\r\n");
    } else {
        char sz[32];

        for (i = 0; i < s_entry_count; i++) total += s_entries[i].size;
        fmt_size(total, sz, sizeof(sz));
        snap_addf("条目数：%d · 目录数：%d · 总大小：%s\r\n", s_entry_count, s_dir_count, sz);
    }
    snap_addf("\r\n");

    snap_addf("---- 扩展名分类（扩展名 / 个数 / 累计大小）----\r\n");
    if (s_ext_count <= 0) {
        snap_addf("（还没有分类结果）\r\n");
    } else {
        classify_exts();                       /* 条目表可能刚被重扫过，这里保证分类是最新的 */
        for (i = 0; i < s_ext_count; i++) {
            char sz[32];

            fmt_size((unsigned long long)s_exts[i].bytes, sz, sizeof(sz));
            snap_addf("  %s  %d 个  %s\r\n",
                      s_exts[i].ext[0] ? s_exts[i].ext : "（无扩展名）", s_exts[i].files, sz);
        }
    }
}

/* ------------------------------------------------------------ 事件回调 */

/**
 * @brief 「重新采集」：清空树与节点表，重建两个根节点并按需重扫
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 运行时提供"清空树"（整树重建、选中态复位），所以这里直接清空重建，不必提示重开窗口；
 *       **清单模型不动**（重采集 ≠ 重导清单）；不开窗时（自检）跳过重扫磁盘
 */
static void on_btn_refresh_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app  = cg_app_of(self);
    cg_widget_t *tree = cg_find(app, "tree_main");
    cg_widget_t *tbl  = cg_find(app, "tbl_items");

    (void)ev;
    (void)user;

    if (tree) cg_tree_clear(tree);
    if (tbl) cg_table_clear(tbl);

    s_node_count  = 0;
    s_entry_count = 0;
    s_dir_count   = 0;
    s_ext_count   = 0;
    s_scanned     = 0;

    build_roots(app);

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：树与节点表已清空重建，跳过重扫目录");
        status_msg(app, "已重建（自检未重扫目录）");
        return;
    }

    /* 立即把两个根的下一层采集出来，免得用户还要手动展开一次 */
    expand_roots(app);

    log_line(app, "重新采集完成：树 %d 个节点，目录条目 %d 个（清单不变）",
             cg_tree_count(tree), s_entry_count);
    status_msg(app, "已重新采集：条目 %d", s_entry_count);
}

/**
 * @brief 「导入INI」：选一份 ini 重新载入清单，并把之后的增删改指向它
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 弹对话框与读盘是副作用：自检（无窗口）时直接跳过
 */
static void on_btn_import_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      path[1024];

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过导入 INI（不弹对话框、不读盘）");
        return;
    }

    path[0] = '\0';
    if (!cg_open_file_dialog(self, "导入清单 INI",
                             "INI 文件(*.ini)|*.ini|所有文件(*.*)|*.*",
                             path, sizeof(path))) {
        log_line(app, "取消导入 INI");
        status_msg(app, "已取消导入");
        return;
    }

    if (ini_load(path, app) < 0) {
        log_line(app, "导入失败：%s 读不出来", path);
        status_msg(app, "导入失败（读不出文件）");
        return;
    }
    snprintf(s_ini_path, sizeof s_ini_path, "%s", path);
    rebuild_env_tree(app);
    log_line(app, "已导入清单 %s：%d 组 %d 个变量（此后增删改存回它）",
             path, s_group_count, s_item_count);
    status_msg(app, "已导入：%d 个变量", s_item_count);
}

/**
 * @brief 把对话框底部的提示行改掉（校验不过时写在框里，不弹消息框、也不关框）
 * @param app 应用
 * @param msg 提示文字；NULL 当空串
 * @return void 无返回值
 */
static void dlg_hint(cg_app_t *app, const char *msg)
{
    cg_widget_set_text(cg_find(app, "lab_dlg_hint"), msg ? msg : "");
}

/**
 * @brief 预填对话框：标题 + 三个字段 + 清提示行
 * @param app 应用
 * @param title 窗口标题（"添加变量" / "修改变量"）
 * @param g 分组（可为 NULL = 空）
 * @param name 变量名（可为 NULL = 空）
 * @param note 备注（可为 NULL = 空）
 * @return void 无返回值
 * @note 每次打开都整体重写一遍：对话框是同一个，上次的值必须清掉
 */
static void dlg_fill(cg_app_t *app, const char *title, const char *g, const char *name, const char *note)
{
    cg_widget_set_text(cg_find(app, "dlg_edit"),       title  ? title  : "");
    cg_widget_set_text(cg_find(app, "edt_dlg_group"),  g      ? g      : "");
    cg_widget_set_text(cg_find(app, "edt_dlg_name"),   name   ? name   : "");
    cg_widget_set_text(cg_find(app, "edt_dlg_note"),   note   ? note   : "");
    dlg_hint(app, "");
}

/**
 * @brief 对话框打开时（原生刚建好）：把光标放到该填的那一格
 * @param self 对话框控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 原生是 cg_dialog_open 里懒建的，open 前 focus 是空操作 —— 所以焦点得在这里给；
 *       分组预填为空（清单里还没有组）就先让人填分组，否则直接落在变量名上
 */
static void on_dlg_show(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *g   = cg_widget_text(cg_find(app, "edt_dlg_group"));

    (void)ev;
    (void)user;

    cg_widget_focus(cg_find(app, (g && g[0]) ? "edt_dlg_name" : "edt_dlg_group"));
}

/**
 * @brief 表单「确定」：三个字段一次校验、一次提交，存回 ini；不合法就留在框里
 * @param self 被点的按钮（对话框里的）
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 校验失败只改框内提示行、**不关框** —— 这是"三次弹框"换成"一次表单"的关键：
 *       用户改完直接再点确定，不用重新走一遍流程
 */
static void on_dlg_ok(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    char        g[48], name[64], note[96];
    const char *s;
    int         other;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过表单确定（不改清单）");
        return;
    }

    s = cg_widget_text(cg_find(app, "edt_dlg_group"));
    snprintf(g, sizeof g, "%s", s ? s : "");
    s = cg_widget_text(cg_find(app, "edt_dlg_name"));
    snprintf(name, sizeof name, "%s", s ? s : "");
    s = cg_widget_text(cg_find(app, "edt_dlg_note"));
    snprintf(note, sizeof note, "%s", s ? s : "");

    str_trim(g);
    str_trim(name);
    str_trim(note);
    str_clean(g, "[]");                            /* 组名带 ] 会把 INI 节写坏 */
    str_clean(name, "=");                          /* 键名带 = 会把键值切开 */

    if (!name[0]) {
        dlg_hint(app, "变量名不能为空。");
        return;
    }
    if (group_ensure(g) < 0) {
        dlg_hint(app, "分组名为空，或分组数已满（32）。");
        return;
    }
    other = item_find(g, name);

    if (s_dlg_mode == DLG_ADD) {
        if (other >= 0) {
            dlg_hint(app, "这个分组下已有同名变量。");
            return;
        }
        if (model_add(g, name, note) != 0) {
            dlg_hint(app, "清单已满（256 个变量）。");
            return;
        }
        crud_save(app);
        rebuild_env_tree(app);
        if (note[0]) log_line(app, "已添加：%s\\%s（%s）——清单 %d 个变量", g, name, note, s_item_count);
        else         log_line(app, "已添加：%s\\%s ——清单 %d 个变量", g, name, s_item_count);
        status_msg(app, "已添加 %s\\%s", g, name);
    } else {
        if (s_dlg_idx < 0 || s_dlg_idx >= s_item_count) {
            dlg_hint(app, "要改的那一行已经不在清单里了，关掉重新选。");
            return;
        }
        if (other >= 0 && other != s_dlg_idx) {
            dlg_hint(app, "目标分组下已有同名变量。");
            return;
        }
        snprintf(s_items[s_dlg_idx].group, sizeof s_items[s_dlg_idx].group, "%s", g);
        snprintf(s_items[s_dlg_idx].name,  sizeof s_items[s_dlg_idx].name,  "%s", name);
        snprintf(s_items[s_dlg_idx].note,  sizeof s_items[s_dlg_idx].note,  "%s", note);
        crud_save(app);
        rebuild_env_tree(app);
        if (note[0]) log_line(app, "已修改：%s → %s\\%s（%s）", s_dlg_old, g, name, note);
        else         log_line(app, "已修改：%s → %s\\%s", s_dlg_old, g, name);
        status_msg(app, "已修改 %s\\%s", g, name);
    }

    cg_dialog_close(self, 1);
}

/**
 * @brief 表单「取消」：什么都不写，直接关框（Esc 同效）
 * @param self 被点的按钮（对话框里的）
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 */
static void on_dlg_cancel(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过表单取消");
        return;
    }
    cg_dialog_close(self, 0);
}

/**
 * @brief 「添加」：打开表单对话框（分组 / 变量名 / 备注 一次填完、一次提交）
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 分组预填当前选中节点所属的组；校验与落盘都在 on_dlg_ok 里；
 *       对话框是模态的：自检（无窗口）时跳过
 */
static void on_btn_add_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *dlg;
    char         g[48];

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过添加（不弹表单）");
        return;
    }

    dlg = cg_find(app, "dlg_edit");
    if (!dlg) return;

    sel_group(app, g, sizeof(g));
    s_dlg_mode = DLG_ADD;
    s_dlg_idx  = -1;
    s_dlg_old[0] = '\0';
    dlg_fill(app, "添加变量", g, "", "");

    if (!cg_dialog_open(dlg)) log_line(app, "取消添加（表单没确定）");
}

/**
 * @brief 「删除」：删选中的变量；选中分组则整组连带删除，存回 ini
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note cg_message_box 只有确定钮，做不了"是否确认"——删除立即生效（README 已写明）；
 *       组删空后分组行也一并删掉；其余分组不受影响
 */
static void on_btn_del_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app  = cg_app_of(self);
    cg_widget_t *tree = cg_find(app, "tree_main");
    int          item = tree ? cg_tree_selected(tree) : -1;
    cg_node_t   *n;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过删除（不弹框、不改清单）");
        return;
    }
    if (item < 0 || item >= s_node_count) {
        status_msg(app, "删除：先在树里选中一个变量或分组");
        return;
    }
    n = &s_nodes[item];

    if (n->kind == NK_ENV) {
        char label[120];
        int  idx = item_find(n->group, n->data);

        if (idx < 0) {
            status_msg(app, "删除：清单里找不到这一行（刚导入过？重新选中再试）");
            return;
        }
        snprintf(label, sizeof label, "%s\\%s", n->group, n->data);
        item_remove(idx);
        crud_save(app);
        rebuild_env_tree(app);
        log_line(app, "已删除：%s ——清单 %d 个变量", label, s_item_count);
        status_msg(app, "已删除 %s", label);
    } else if (n->kind == NK_GROUP) {
        char label[48];
        int  killed = 0;
        int  i, gi;

        snprintf(label, sizeof label, "%s", n->data);
        for (i = s_item_count - 1; i >= 0; i--)
            if (strcmp(s_items[i].group, label) == 0) { item_remove(i); killed++; }
        gi = group_index(label);
        if (gi >= 0) group_remove(gi);
        crud_save(app);
        rebuild_env_tree(app);
        log_line(app, "已删除分组 %s（连带 %d 个变量）——清单 %d 个变量", label, killed, s_item_count);
        status_msg(app, "已删除分组 %s", label);
    } else {
        status_msg(app, "删除：先在树里选中一个变量或分组");
    }
}

/**
 * @brief 「修改」：打开同一张表单（预填现值），改名 / 挪分组 / 改备注，存回 ini
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 分组改了就是挪组（原组空了会保留空节，方便以后再加回去）；
 *       改到同组同名（别的行）会被拒；校验与落盘都在 on_dlg_ok 里
 */
static void on_btn_edit_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app  = cg_app_of(self);
    cg_widget_t *tree = cg_find(app, "tree_main");
    int          item = tree ? cg_tree_selected(tree) : -1;
    cg_widget_t *dlg;
    cg_node_t   *n;
    int          idx;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过修改（不弹表单）");
        return;
    }
    if (item < 0 || item >= s_node_count) {
        status_msg(app, "修改：先在树里选中一个变量");
        return;
    }
    n = &s_nodes[item];
    if (n->kind != NK_ENV) {
        status_msg(app, "修改：只能改变量节点（分组改名请删了重加）");
        return;
    }
    idx = item_find(n->group, n->data);
    if (idx < 0) {
        status_msg(app, "修改：清单里找不到这一行（刚导入过？重新选中再试）");
        return;
    }
    dlg = cg_find(app, "dlg_edit");
    if (!dlg) return;

    s_dlg_mode = DLG_EDIT;
    s_dlg_idx  = idx;
    snprintf(s_dlg_old, sizeof s_dlg_old, "%s\\%s", s_items[idx].group, s_items[idx].name);
    dlg_fill(app, "修改变量", s_items[idx].group, s_items[idx].name, s_items[idx].note);

    if (!cg_dialog_open(dlg)) log_line(app, "取消修改（表单没确定）");
}

/**
 * @brief 「查询」：按关键字过滤清单，把命中行摆进右侧表格
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 命中的是清单行（分组 / 变量名 / 备注），不是变量当前值；
 *       点一下树节点就回到该节点的条目表；空关键字 / 取消不动表格
 */
static void on_btn_find_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *tbl  = cg_find(app, "tbl_items");
    char         kw[96];
    int          i, hit = 0;

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过查询（不弹输入框）");
        return;
    }

    kw[0] = '\0';
    if (!cg_input_box(self, "查询清单", "关键字（分组 / 变量名 / 备注）：", "", kw, sizeof kw)) {
        log_line(app, "取消查询");
        return;
    }
    str_trim(kw);
    if (!kw[0]) {
        status_msg(app, "查询：关键字为空");
        return;
    }

    if (!tbl) return;
    cg_table_clear(tbl);
    for (i = 0; i < s_item_count; i++) {
        const char *cells[3];

        if (!icontains(s_items[i].group, kw) &&
            !icontains(s_items[i].name, kw) &&
            !icontains(s_items[i].note, kw)) continue;
        cells[0] = s_items[i].group;
        cells[1] = s_items[i].name;
        cells[2] = s_items[i].note[0] ? s_items[i].note : "—";
        cg_table_add_row(tbl, cells, 3);
        hit++;
    }
    if (hit == 0) {
        const char *cells[3] = { "（没有匹配）", "", "试试别的关键字" };

        cg_table_add_row(tbl, cells, 3);
    }
    log_line(app, "查询“%s”：命中 %d 条（点树节点回到该节点的条目表）", kw, hit);
    status_msg(app, "查询命中 %d 条", hit);
}

/**
 * @brief 「导出快照」：选路径 → 拼快照 → 写盘，并把字节数写进日志与状态栏
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 弹保存对话框与写盘都是副作用：自检（无窗口）时直接跳过，不弹框、不写盘
 */
static void on_btn_export_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      path[1024];

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过导出快照（不弹保存对话框、不写盘）");
        return;
    }

    path[0] = '\0';
    if (!cg_save_file_dialog(self, "导出环境快照",
                             "文本文件(*.txt)|*.txt|所有文件(*.*)|*.*",
                             "txt", path, sizeof(path))) {
        log_line(app, "取消导出快照");
        status_msg(app, "已取消导出");
        return;
    }

    snapshot_build(app);
    if (cg_file_write(path, s_snap) == 0) {
        log_line(app, "已导出快照：%s（%d 字节）", path, s_snap_len);
        status_msg(app, "导出成功：%d 字节", s_snap_len);
    } else {
        log_line(app, "导出快照失败：%s", path);
        status_msg(app, "导出失败");
    }
}

/**
 * @brief 「复制快照到剪贴板」：拼一份快照文本，送进剪贴板
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 写剪贴板是副作用：自检（无窗口）时跳过，别把机器的剪贴板糊掉
 */
static void on_btn_copy_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;

    if (!cg_app_has_window(app)) {
        log_line(app, "自检模式：跳过写剪贴板");
        return;
    }

    snapshot_build(app);
    if (s_snap_len <= 0) {
        log_line(app, "快照为空，取消复制");
        status_msg(app, "快照为空，没有可复制的内容");
        return;
    }

    if (cg_clip_set_text(s_snap) == 0) {
        log_line(app, "快照已复制到剪贴板（%d 字节）", s_snap_len);
        status_msg(app, "已复制快照到剪贴板：%d 字节", s_snap_len);
    } else {
        log_line(app, "写剪贴板失败");
        status_msg(app, "写剪贴板失败");
    }
}

/**
 * @brief 树选中变化：按当前节点刷新右侧条目表
 * @param self 树控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 只读内存，不读磁盘、不弹框 —— 自检会合成这个事件
 */
static void on_tree_sel(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app  = cg_app_of(self);
    int       item = cg_tree_selected(self);

    (void)ev;
    (void)user;

    fill_table(app);
    if (item >= 0) status_msg(app, "选中节点 #%d", item);
    else           status_msg(app, "没有选中节点");
}

/**
 * @brief 节点展开：对该节点做一次懒加载，并提示当前树的规模
 * @param self 树控件
 * @param ev 事件类型
 * @param user 未使用
 * @return void 无返回值
 * @note 运行时会把"被操作的节点"设成当前选中，所以这里问得到是哪一个；
 *       采不采由懒加载内部按标记判断，这里自己不读磁盘、不弹框
 */
static void on_tree_expand(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app  = cg_app_of(self);
    int       item = cg_tree_selected(self);

    (void)ev;
    (void)user;

    if (item < 0) {                                 /* 自检合成时是空选中，直接放过 */
        log_line(app, "展开事件：没有可用的节点（多半是自检合成的空选中）");
        return;
    }

    load_children(app, item);
    log_line(app, "展开节点 #%d：当前树共 %d 个节点", item, cg_tree_count(self));
    status_msg(app, "已展开：树共 %d 个节点", cg_tree_count(self));
}

/**
 * @brief 界面就绪：先载清单（真窗口读 ini，没有就生成；自检用内置清单），再搭根节点
 * @param app 应用
 * @param user 启动描述里的业务数据，本示例没用
 * @return void 无返回值
 */
void en_init(cg_app_t *app, void *user)
{
    (void)user;

    if (cg_app_has_window(app)) {
        char *dir  = cg_app_dir();
        char *path = dir ? cg_path_join(dir, "envsnap.ini") : NULL;
        int   n;

        if (path) snprintf(s_ini_path, sizeof s_ini_path, "%s", path);
        cg_free(path);
        cg_free(dir);

        n = s_ini_path[0] ? ini_load(s_ini_path, app) : -1;
        if (n < 0) {
            model_load_seed();
            if (s_ini_path[0] && ini_save(s_ini_path, app) == 0)
                log_line(app, "exe 旁没有 envsnap.ini，已按内置清单生成（%d 组 %d 个变量）",
                         s_group_count, s_item_count);
        } else {
            log_line(app, "已载入清单 %s：%d 组 %d 个变量", s_ini_path, s_group_count, s_item_count);
        }
    } else {
        model_load_seed();                 /* 自检：不读盘不写盘，用内置清单撑起树 */
    }

    build_roots(app);

    /* 真窗口下先把两个根展开一层（分组 / 扩展名分类），省得用户一个个点开；
     * 自检不开窗，这一步跳过 —— 也就不会在自检里读盘 */
    if (cg_app_has_window(app)) {
        expand_roots(app);
        log_line(app, "已展开两个根：树共 %d 个节点", cg_tree_count(cg_find(app, "tree_main")));
    }

    log_line(app, "清单可增删改查（改动自动存回 INI）；导出 / 复制对当前已采集的内容做快照");
    status_msg(app, "就绪：清单 %d 组 %d 个变量", s_group_count, s_item_count);
}

/* ------------------------------------------------------------ 绑定表 */

const cg_binding_t en_bindings[] = {
    CG_BIND("en_on_btn_refresh_click", on_btn_refresh_click),
    CG_BIND("en_on_btn_export_click",  on_btn_export_click),
    CG_BIND("en_on_btn_copy_click",    on_btn_copy_click),
    CG_BIND("en_on_btn_import_click",  on_btn_import_click),
    CG_BIND("en_on_btn_add_click",     on_btn_add_click),
    CG_BIND("en_on_btn_del_click",     on_btn_del_click),
    CG_BIND("en_on_btn_edit_click",    on_btn_edit_click),
    CG_BIND("en_on_btn_find_click",    on_btn_find_click),
    CG_BIND("en_on_dlg_show",          on_dlg_show),
    CG_BIND("en_on_dlg_ok",            on_dlg_ok),
    CG_BIND("en_on_dlg_cancel",        on_dlg_cancel),
    CG_BIND("en_on_tree_sel",          on_tree_sel),
    CG_BIND("en_on_tree_expand",       on_tree_expand),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则跑自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
