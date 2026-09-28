/**
 * @file    main.c
 * @brief   环境快照：懒加载的树 + 环境变量与用户数据目录的只读快照
 * @details 与 syspanel 一样"只读为主、不接硬件"，本示例的重点是**懒加载的树**：
 *          左侧树只先建两个根（环境变量 / 用户数据目录），点开哪个才枚举哪个 ——
 *          用户数据目录可能有几百上千个文件，一次全展开会把消息循环堵住、界面假死，
 *          这和资源管理器"展开才读"是同一个道理。
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

/* ------------------------------------------------------------ 采集清单 */

/* 环境变量按**名字**读：框架只提供"按名取一个变量"，没有"枚举环境变量"的接口，
 * 所以这里列一份常用清单；分组名决定它挂在树里哪一支。 */
static const struct {
    const char *name;    /**< 变量名 */
    const char *group;   /**< 树里的分组名 */
} k_envs[] = {
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

/** 采集清单条目数 */
#define K_ENV_COUNT ((int)(sizeof k_envs / sizeof k_envs[0]))
/** 用户数据目录最多列多少个条目（超出的只计数、不进树，免得界面被塞爆） */
#define K_DIR_MAX   200
/** 扩展名分类最多几类 */
#define K_EXT_MAX   48
/** 快照文本缓冲大小：业务不 malloc（工程铁律），固定预留这么多，写满即截断 */
#define K_SNAP_MAX  (96 * 1024)

/* ------------------------------------------------------------ 数据模型 */

/** 树节点类别 */
enum { NK_NONE = 0, NK_ENV_ROOT, NK_DIR_ROOT, NK_GROUP, NK_ENV, NK_EXT, NK_DIRLIST, NK_FILE };

/** 树节点表的一行；下标与树控件的节点下标一一对应（运行时只追加，下标因此稳定） */
typedef struct {
    int  kind;          /**< 节点类别，见 NK_* */
    int  loaded;        /**< 子节点是否已建过（懒加载标记） */
    char data[96];      /**< 承载的数据：组名 / 变量名 / 扩展名 / 文件名 */
} cg_node_t;

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

static cg_node_t s_nodes[768];          /**< 节点表：够 28 个变量 + 200 个文件条目 + 分组与分类 */
static int       s_node_count;          /**< 已用节点数，也是下一次加节点该拿到的下标 */

static entry_t   s_entries[K_DIR_MAX];  /**< 目录条目快照（刷新时重扫） */
static int       s_entry_count;         /**< 已登记的条目数 */
static int       s_dir_count;           /**< 其中目录个数 */
static int       s_scanned;             /**< 目录扫过没有；0 = 需要重扫 */
static extstat_t s_exts[K_EXT_MAX];     /**< 扩展名分类结果 */
static int       s_ext_count;           /**< 分类数 */

static char s_snap[K_SNAP_MAX];         /**< 导出用的快照文本（文件级 static，见上面那个宏） */
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
 * @brief 清空快照缓冲，准备重新生成
 * @return void 无返回值
 */
static void snap_reset(void)
{
    s_snap_len = 0;
    s_snap[0]  = '\0';
}

/**
 * @brief 往快照缓冲追加一段文本（写满就停在缓冲末尾，不再增长）
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

/* ------------------------------------------------------------ 树与采集 */

/**
 * @brief 往树里加一个节点，同时在节点表里留一行
 * @param app 应用
 * @param parent 父节点下标；-1 = 加在根上
 * @param kind 节点类别，见 NK_*
 * @param data 节点承载的数据（组名 / 变量名 / 扩展名 / 文件名）；可为 NULL
 * @param text 节点显示文字
 * @return int 新节点下标；建不上或对不上号返回 -1
 * @note 运行时是追加式建节点，控件返回的下标必然等于本表下标；
 *       一旦对不上（例如表满了）就只让它显示、不记录 —— 宁可少一点，也别让记录错位
 */
static int node_add(cg_app_t *app, int parent, int kind, const char *data, const char *text)
{
    cg_widget_t *tree = app ? cg_find(app, "tree_main") : NULL;
    int          item;

    if (!tree) return -1;
    item = cg_tree_add(tree, parent, text);
    if (item < 0 || item >= (int)(sizeof s_nodes / sizeof s_nodes[0])) return -1;
    if (item != s_node_count) return -1;                  /* 下标对不上：只显示，不记录 */

    s_nodes[item].kind   = kind;
    s_nodes[item].loaded = 0;
    snprintf(s_nodes[item].data, sizeof s_nodes[item].data, "%s", data ? data : "");
    s_node_count = item + 1;
    return item;
}

/**
 * @brief 判断某条环境变量是不是它所在分组里的第一条（用来决定要不要建一个分组节点）
 * @param idx k_envs 里的下标
 * @return int 1 = 是分组首条、0 = 前面已有同组
 */
static int group_first(int idx)
{
    int j;

    for (j = 0; j < idx; j++)
        if (strcmp(k_envs[j].group, k_envs[idx].group) == 0) return 0;
    return 1;
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
 *       （几百个文件、每个还要问大小与是否目录），所以一律等用户点开才建一层
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
        /* 第一层先只放"分组"，变量本身等分组展开再建 */
        for (i = 0; i < K_ENV_COUNT; i++)
            if (group_first(i)) node_add(app, item, NK_GROUP, k_envs[i].group, k_envs[i].group);
        break;

    case NK_GROUP:
        for (i = 0; i < K_ENV_COUNT; i++)
            if (strcmp(k_envs[i].group, n->data) == 0)
                node_add(app, item, NK_ENV, k_envs[i].name, k_envs[i].name);
        break;

    case NK_DIR_ROOT:
        if (!s_scanned) scan_data_dir(app, 1);
        classify_exts();
        for (k = 0; k < s_ext_count; k++) {
            /* 扩展名限长 23 字节（数组就 24 字节）：限了 gcc 才证得出装得下，
             * 不限会报 -Wformat-truncation（2026-09-28 实测） */
            snprintf(label, sizeof label, "%.23s  (%d)",
                     s_exts[k].ext[0] ? s_exts[k].ext : "(无扩展名)", s_exts[k].files);
            node_add(app, item, NK_EXT, s_exts[k].ext, label);
        }
        if (s_dir_count > 0) {
            snprintf(label, sizeof label, "(目录)  (%d)", s_dir_count);
            node_add(app, item, NK_DIRLIST, "", label);
        }
        break;

    case NK_EXT:
        for (i = 0; i < s_entry_count; i++)
            if (!s_entries[i].is_dir && strcmp(s_entries[i].ext, n->data) == 0)
                node_add(app, item, NK_FILE, s_entries[i].name, s_entries[i].name);
        break;

    case NK_DIRLIST:
        for (i = 0; i < s_entry_count; i++)
            if (s_entries[i].is_dir)
                node_add(app, item, NK_FILE, s_entries[i].name, s_entries[i].name);
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
    node_add(app, -1, NK_ENV_ROOT, "环境变量", "环境变量");
    node_add(app, -1, NK_DIR_ROOT, "用户数据目录", "用户数据目录");
}

/**
 * @brief 拼快照前把环境变量这几个懒加载节点补齐
 * @param app 应用
 * @return void 无返回值
 * @note 先补环境变量根的下一层（分组），再补每个分组的下一层（变量）；都只读环境变量、不碰磁盘。
 *       目录那侧**不在这里补**，免得"导出"顺手把盘扫一遍（那是"重新采集"的活）
 */
static void ensure_env_tree(cg_app_t *app)
{
    int i;

    for (i = 0; i < s_node_count; i++) {
        if (s_nodes[i].kind == NK_ENV_ROOT) {
            load_children(app, i);
            break;
        }
    }
    for (i = 0; i < s_node_count; i++) {
        if (s_nodes[i].kind == NK_GROUP) load_children(app, i);
    }
}

/**
 * @brief 把两个根的下一层摆出来（懒加载的"第一次展开"）
 * @param app 应用
 * @return void 无返回值
 * @note 环境变量那侧只读变量、不碰磁盘；目录那侧会扫一层（所以只在真窗口下调用）
 */
static void expand_roots(cg_app_t *app)
{
    int i;

    for (i = 0; i < s_node_count; i++)
        if (s_nodes[i].kind == NK_ENV_ROOT || s_nodes[i].kind == NK_DIR_ROOT)
            load_children(app, i);
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
        const char *cells[3] = { "环境变量", "", "展开后按分组采集，选中分组或变量看详情" };

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

        for (i = 0; i < s_node_count; i++)
            if (s_nodes[i].kind == NK_ENV) vars++;
        snprintf(note, sizeof(note), "环境变量分组，含 %d 个变量", vars);
        cells[0] = n->data;
        cells[1] = "分组";
        cells[2] = note;
        cg_table_add_row(tbl, cells, 3);
        break;
    }
    case NK_ENV: {
        char       *v = cg_env(n->data);
        char        sum[80];
        const char *cells[3];

        value_summary(v, sum, sizeof(sum));
        cells[0] = n->data;
        cells[1] = v ? sum : "（没有这个变量）";
        cells[2] = v ? "已设置" : "未设置";
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
 * @brief 把当前已采集到的东西拼成一份文本快照，写进快照缓冲
 * @param app 应用
 * @return void 无返回值
 * @note 段落：时间 / 环境变量清单（按分组、值超长走摘要）/ 用户数据目录概览 / 扩展名分类表；
 *       只拼**已经采集到**的内容，不主动扫盘
 */
static void snapshot_build(cg_app_t *app)
{
    char               *now;
    int                 i;
    unsigned long long  total = 0;

    snap_reset();

    now = cg_time_now(NULL);
    snap_addf("==== CStackGUI 环境快照 ====\r\n");
    snap_addf("采集时间：%s\r\n", now ? now : "（读不到）");
    cg_free(now);
    snap_addf("\r\n");

    /* 环境变量：先把懒加载的节点补齐（只读环境变量，不碰磁盘） */
    ensure_env_tree(app);
    snap_addf("---- 环境变量（按分组）----\r\n");
    for (i = 0; i < s_node_count; i++) {
        if (s_nodes[i].kind == NK_GROUP) {
            snap_addf("[%s]\r\n", s_nodes[i].data);
        } else if (s_nodes[i].kind == NK_ENV) {
            char *v = cg_env(s_nodes[i].data);
            char  sum[80];

            value_summary(v, sum, sizeof(sum));
            snap_addf("    %s = %s\r\n", s_nodes[i].data, sum);
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
 *       不开窗时（自检）跳过重扫磁盘
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

    log_line(app, "重新采集完成：树 %d 个节点，目录条目 %d 个", cg_tree_count(tree), s_entry_count);
    status_msg(app, "已重新采集：条目 %d", s_entry_count);
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
 * @brief 界面就绪：只搭两个根节点，再写一段开场白（不扫盘）
 * @param app 应用
 * @param user 启动描述里的业务数据，本示例没用
 * @return void 无返回值
 */
static void on_ready(cg_app_t *app, void *user)
{
    (void)user;

    build_roots(app);

    /* 真窗口下先把两个根展开一层（分组 / 扩展名分类），省得用户一个个点开；
     * 自检不开窗，这一步跳过 —— 也就不会在自检里读盘 */
    if (cg_app_has_window(app)) {
        expand_roots(app);
        log_line(app, "已展开两个根：树共 %d 个节点", cg_tree_count(cg_find(app, "tree_main")));
    }

    log_line(app, "环境快照就绪：展开「环境变量」或「用户数据目录」按需采集，点「重新采集」整树重扫");
    log_line(app, "导出 / 复制只写「当前已采集到」的内容；两者都会先拼一次快照");
    status_msg(app, "就绪：展开节点按需采集");
}

/* ------------------------------------------------------------ 绑定表 */

static const cg_binding_t k_bindings[] = {
    CG_BIND("on_btn_refresh_click", on_btn_refresh_click),
    CG_BIND("on_btn_export_click",  on_btn_export_click),
    CG_BIND("on_btn_copy_click",    on_btn_copy_click),
    CG_BIND("on_tree_sel",          on_tree_sel),
    CG_BIND("on_tree_expand",       on_tree_expand),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则跑自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings         = k_bindings;
    desc.auto_locate_cgui = 1;
    desc.on_ready         = on_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "envsnap.cgui";
        return cg_app_selftest(&desc);
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "envsnap.cgui";
    return cg_app_run(&desc);
}
