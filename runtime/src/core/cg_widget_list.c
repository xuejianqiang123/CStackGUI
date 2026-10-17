/**
 * @file    cg_widget_list.c
 * @brief   列表类 + 零散控件 API：listbox/combo 条目、状态栏、图片、菜单、标题/退出/文档转储、提示/时钟、动态追加控件
 * @details 从 cg_widget_data.c 按职责拆分而来；跨模块接口见 cg_internal.h。
 */
#include "cg_internal.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>   /* 运行时改菜单那一段要 memmove / memset / strcmp / strcpy 家族 */

/**
 * @brief 取挂在控件上的业务数据
 * @param w 目标控件
 * @return void* 业务数据指针（所有权仍归调用方，本库不释放）；控件为 NULL 或没挂过返回 NULL
 */
void  *cg_widget_get_user(const cg_widget_t *w) { return w ? w->user : NULL; }
/**
 * @brief 挂业务数据到控件（回调里的 user 参数就是它）
 * @param w 目标控件；NULL 时什么都不做
 * @param user 业务数据指针，所有权仍归调用方（本库不释放）
 * @return void 无返回值
 */
void   cg_widget_set_user(cg_widget_t *w, void *user) { if (w) w->user = user; }

/* ------------------------------------------------------------ 列表类控件 */

/**
 * @brief 给列表类控件追加一个条目（listbox/combo/tabs/statusbar/toolbar）
 * @param w 目标控件，任何带 items 的控件都能用（不做类型校验）
 * @param item 条目文字，内部会拷一份，调用方可以立刻释放
 * @return int 新条目下标（0 起）；控件 / 文字为空或内存不足返回 -1
 */
int cg_widget_list_add(cg_widget_t *w, const char *item)
{
    char **ni;

    if (!w || !item) return -1;
    ni = (char **)realloc(w->items, ((size_t)w->nitems + 1) * sizeof(char *));
    if (!ni) return -1;
    w->items = ni;
    w->items[w->nitems] = cg_str_dup(item);
    if (!w->items[w->nitems]) return -1;
    w->nitems++;
    w->items_dirty = 1;
    w->scroll_bottom = 1;
    cg_widget_sync(w);
    return w->nitems - 1;
}

/**
 * @brief cg_widget_list_add 的 printf 版本
 * @param w 目标控件
 * @param fmt printf 风格格式串，后接可变参数；拼出来超过 512 字节时自动改用堆缓冲
 * @return int 新条目下标（0 起）；控件 / 格式串为空或失败返回 -1
 */
int cg_widget_list_addf(cg_widget_t *w, const char *fmt, ...)
{
    char    stackbuf[512];
    char   *buf = stackbuf;
    int     n;
    va_list ap;
    int     rc;

    if (!w || !fmt) return -1;

    va_start(ap, fmt);
    n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);
    if (n < 0) return -1;

    if ((size_t)n >= sizeof(stackbuf)) {
        buf = (char *)malloc((size_t)n + 1);
        if (!buf) return -1;
        va_start(ap, fmt);
        vsnprintf(buf, (size_t)n + 1, fmt, ap);
        va_end(ap);
    }
    rc = cg_widget_list_add(w, buf);
    if (buf != stackbuf) free(buf);
    return rc;
}

/**
 * @brief 清空列表类控件的全部条目
 * @param w 目标控件；NULL 时什么都不做
 * @return void 无返回值
 */
void cg_widget_list_clear(cg_widget_t *w)
{
    if (!w) return;
    for (int i = 0; i < w->nitems; i++) free(w->items[i]);
    free(w->items);
    w->items = NULL;
    w->nitems = 0;
    w->items_dirty = 1;
    cg_widget_sync(w);
}

/**
 * @brief 取列表类控件的条目数
 * @param w 目标控件
 * @return int 条目个数（不是下标上限）；控件为 NULL 返回 0
 */
int cg_widget_list_count(const cg_widget_t *w) { return w ? w->nitems : 0; }

/**
 * @brief 取第 index 个条目的文字
 * @param w 目标控件
 * @param index 条目下标（0 起）
 * @return const char* 控件内部字符串，别 free；控件为 NULL 或下标越界返回 NULL
 */
const char *cg_widget_list_item(const cg_widget_t *w, int index)
{
    if (!w || index < 0 || index >= w->nitems) return NULL;
    return w->items[index];
}

/**
 * @brief 让列表滚到最后一项（日志类控件常用）
 * @param w 目标控件；NULL 时什么都不做
 * @return void 无返回值
 */
void cg_widget_list_scroll_bottom(cg_widget_t *w)
{
    if (!w) return;
    w->scroll_bottom = 1;
    cg_widget_sync(w);
}

/* ------------------------------------------------------------ 状态栏 */

/**
 * @brief 改状态栏第 part 段的文字（段不够时自动补空段）
 * @param w statusbar 控件；类型不对只记一条错误日志
 * @param part 段号（0 起），负数忽略
 * @param text 新文字；NULL 当空串处理
 * @return void 无返回值
 */
void cg_status_set(cg_widget_t *w, int part, const char *text)
{
    if (!w || !want_type(w, "statusbar", "cg_status_set")) return;
    if (part < 0) return;
    while (w->nitems <= part) {          /* 段不够就补空段 */
        if (cg_widget_list_add(w, "") < 0) return;
    }
    free(w->items[part]);
    w->items[part] = cg_str_dup(text ? text : "");
    w->items_dirty = 1;
    cg_widget_sync(w);
}

/**
 * @brief cg_status_set 的 printf 版本
 * @param w statusbar 控件
 * @param part 段号（0 起），负数忽略
 * @param fmt printf 风格格式串，后接可变参数；拼出来超过 512 字节时自动改用堆缓冲
 * @return void 无返回值
 */
void cg_status_setf(cg_widget_t *w, int part, const char *fmt, ...)
{
    char    stackbuf[512];
    char   *buf = stackbuf;
    va_list ap;
    int     n;

    if (!w || !fmt) return;
    va_start(ap, fmt);
    n = vsnprintf(stackbuf, sizeof(stackbuf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof(stackbuf)) {
        buf = (char *)malloc((size_t)n + 1);
        if (!buf) return;
        va_start(ap, fmt);
        vsnprintf(buf, (size_t)n + 1, fmt, ap);
        va_end(ap);
    }
    cg_status_set(w, part, buf);
    if (buf != stackbuf) free(buf);
}

/* ------------------------------------------------------------ 图片 */

/**
 * @brief 换图片文件（png/jpg/bmp/gif）；路径不存在时显示占位框
 * @param w 目标控件
 * @param path 图片路径，内部会拷一份；NULL 或空串表示清掉图片
 * @return void 无返回值
 */
void cg_widget_set_image(cg_widget_t *w, const char *path)
{
    if (!w) return;
    free(w->src);
    w->src = (path && *path) ? cg_str_dup(path) : NULL;
    w->image_failed = 0;     /* 换成新路径，给后端一次重新加载的机会 */
    cg_widget_sync(w);                 /* 后端的 sync 发现路径变了会先释放旧位图 */
}

/**
 * @brief 取控件当前的图片路径
 * @param w 目标控件
 * @return const char* 控件内部字符串，别 free；控件为 NULL 或没设过图片返回 NULL
 */
const char *cg_widget_image(const cg_widget_t *w)
{
    return w ? w->src : NULL;
}

/* ------------------------------------------------------------ 菜单 */

/**
 * @brief 取最近点中的菜单项 id（.cgui 里写的）
 * @param w menu / menubutton 控件
 * @return const char* 控件内部字符串，别 free；控件为 NULL、还没点过或那一项没写 id 返回 NULL
 */
const char *cg_menu_last_id(const cg_widget_t *w)
{
    return w ? w->menu_last_id : NULL;
}

/**
 * @brief 取最近点中的菜单项文字
 * @param w menu / menubutton 控件
 * @return const char* 控件内部字符串，别 free；控件为 NULL 或还没点过返回 NULL
 */
const char *cg_menu_last_text(const cg_widget_t *w)
{
    return w ? w->menu_last_text : NULL;
}

/**
 * @brief 数菜单里有多少个可点的项（不含分隔线和下拉项）
 * @param w menu / menubutton 控件
 * @return int 可点项数；控件为 NULL 返回 0
 * @note 调试用
 */
int cg_menu_count(const cg_widget_t *w)
{
    int n = 0;
    if (!w) return 0;
    for (int i = 0; i < w->nmenu; i++) {
        if (w->menu[i].cmd != 0) n++;
    }
    return n;
}

/* ------------------------------------------------------------ 运行时改菜单
 *
 * 行表是**唯一的真相**：HMENU 只是它的一次投影（cgw_menu_build 是纯函数），
 * 所以"改完让后端重建一次"就够了 —— 不需要维护两套状态，也不需要逐项同步。
 * 六个 API 的全部实现就是"改 menu[] 数组"，平台相关的事只有最后一行的 rebuild。
 *
 * 最容易错的是**索引平移**：parent 存的是数组下标，插入/删除会把后面所有行挪位，
 * 忘了修正就会出现"加了一项，另一个下拉里的项跑到顶层去了"（单测专门钉这一条）。
 */

/**
 * @brief 菜单类控件的类型守卫（want_type 一次只认一个类型，而 menu/menubutton 是同一套行表）
 * @param w 待检查的控件
 * @param who 调用方函数名，只用于日志前缀
 * @return int 1 = 是 menu / menubutton；0 = 不是（并记一条错误日志）
 */
static int want_menu(const cg_widget_t *w, const char *who)
{
    if (w && (!strcmp(w->type, "menu") || !strcmp(w->type, "menubutton"))) return 1;
    cg_log(CG_LOG_ERROR, "%s：控件 \"%s\" 不是菜单（menu / menubutton）",
           who, (w && w->id) ? w->id : "-");
    return 0;
}

/**
 * @brief 在某个父项的**直接子项**里按 id 找一个菜单项
 * @param w 菜单控件
 * @param want_parent 父项的行下标；-1 = 顶层
 * @param id 项标识，空串按找不到处理
 * @return int 行下标；控件 / 标识为空或找不到返回 -1
 * @details 只在直接子项里找（parent 必须等于 want_parent），这样同一个 id 分别出现在
 *          两个下拉里时也能各自定位。
 */
static int menu_find_child(const cg_widget_t *w, int want_parent, const char *id)
{
    if (!w || !id || !*id) return -1;
    for (int i = 0; i < w->nmenu; i++) {
        if (w->menu[i].parent != want_parent) continue;
        if (w->menu[i].id && strcmp(w->menu[i].id, id) == 0) return i;
    }
    return -1;
}

/**
 * @brief 定位"往哪个下拉里加项"：parent_id 空 = 顶层
 * @param w 菜单控件
 * @param parent_id 父下拉项的 id；NULL 或空串表示往顶层加
 * @param who 调用方函数名，只用于日志前缀
 * @return int 父项行下标；parent_id 为空返回 -1；找不到父项或它不是下拉返回 -2（并记一条 WARN）
 * @details 找不到 / 不是下拉都算失败 —— 不猜、也不退回顶层（否则"加错地方"这种事会静默发生）。
 */
static int menu_parent_of(const cg_widget_t *w, const char *parent_id, const char *who)
{
    int k;
    if (!parent_id || !*parent_id) return -1;
    k = menu_find_child(w, -1, parent_id);
    if (k < 0) {
        /* 下拉也可能是嵌套的（二级菜单），顶层找不到就在所有项里再找一遍 */
        for (int i = 0; i < w->nmenu; i++) {
            if (w->menu[i].id && strcmp(w->menu[i].id, parent_id) == 0) { k = i; break; }
        }
    }
    if (k < 0) {
        cg_log(CG_LOG_WARN, "%s：找不到父项 \"%s\"（要往某个下拉里加项，"
                            "那个下拉必须先在 .cgui 里有 id）", who, parent_id);
        return CG_ERR_NO_ITEM;
    }
    if (!w->menu[k].popup) {
        cg_log(CG_LOG_WARN, "%s：\"%s\" 不是下拉项（它没有子项），不能往里加", who, parent_id);
        return CG_ERR_NO_ITEM;
    }
    return k;
}

/**
 * @brief 第 j 行在不在 root 那一行的子树上
 * @param w 菜单控件
 * @param j 待检查的行下标
 * @param root 子树根的行下标
 * @return int 1 = 在子树上；0 = 不在
 */
static int menu_in_subtree(const cg_widget_t *w, int j, int root)
{
    int p = w->menu[j].parent;
    while (p >= 0) {
        if (p == root) return 1;
        p = w->menu[p].parent;
    }
    return 0;
}

/**
 * @brief 第 root 行那棵子树的后一个位置
 * @param w 菜单控件
 * @param root 子树根的行下标
 * @return int 子树后一行的下标；子树为空时等于 root + 1
 * @details 行表按下标连续存子树，所以子树是 [root + 1, 返回值) 这样一个区间。
 */
static int menu_subtree_end(const cg_widget_t *w, int root)
{
    int j = root + 1;
    while (j < w->nmenu && menu_in_subtree(w, j, root)) j++;
    return j;
}

/**
 * @brief 删掉 [from, to) 这些行，并把后面各行的父下标平移修正
 * @param w 菜单控件
 * @param from 起始行下标（含）
 * @param to 结束行下标（不含）
 * @return int 删掉的行数；区间为空返回 0
 * @details **纯操作**：不重建，调用方最后统一 rebuild 一次。
 */
static int menu_drop_rows(cg_widget_t *w, int from, int to)
{
    int n = to - from;
    if (n <= 0) return 0;
    for (int i = from; i < to; i++) {
        free(w->menu[i].text);
        free(w->menu[i].id);
        free(w->menu[i].action);
    }
    memmove(&w->menu[from], &w->menu[to],
            ((size_t)w->nmenu - (size_t)to) * sizeof(cg_menu_row_t));
    w->nmenu -= n;
    memset(&w->menu[w->nmenu], 0, (size_t)n * sizeof(cg_menu_row_t));
    for (int i = 0; i < w->nmenu; i++) {
        if (w->menu[i].parent >= to) w->menu[i].parent -= n;
    }
    return n;
}

/**
 * @brief 运行时往菜单里加一项
 * @param w 菜单控件（menu / menubutton）
 * @param parent_id 父下拉项的 id；NULL / "" = 顶层
 * @param pos 插入到该父项第几个直接子项之前；-1 或越界 = 追加
 * @param text 显示文字；"-" = 分隔线
 * @param id 项标识，可空（空 = 之后没法用它定位）
 * @return int 新项在行表里的下标；失败 -1
 */
int cg_menu_add_item(cg_widget_t *w, const char *parent_id, int pos,
                     const char *text, const char *id)
{
    cg_menu_row_t *nr;
    cg_app_t      *app;
    const char    *txt = text ? text : "";
    int            parent, at, is_sep;

    if (!want_menu(w, "cg_menu_add_item")) return -1;
    app    = w->app;
    parent = menu_parent_of(w, parent_id, "cg_menu_add_item");
    if (parent == -2) return -1;

    /* 插入位置：该父项的**直接子项**里的第 pos 个之前；-1 / 越界 = 追加到该父项**子树的末尾**。
     *
     * 是"子树末尾"而不是"整张表末尾"：行表是深度优先摊平的，**子树必须连续** ——
     * menu_subtree_end() 就是靠"连续区间"认出一整棵子树的（删项、清空都依赖它）。
     * 把新项扔到表尾其实也能显示对（HMENU 按行序往各自父项上追加），但会悄悄破坏
     * 这个连续性：之后删那个下拉时会漏掉它，而界面上看不出任何异常。
     * （这一条是被单测抓出来的 —— 第一版就是这么写的。） */
    at = -1;
    {
        int seen = 0;
        for (int i = 0; i < w->nmenu; i++) {
            if (w->menu[i].parent != parent) continue;
            if (pos >= 0 && seen == pos) { at = i; break; }
            seen++;
        }
        if (at < 0) at = (parent >= 0) ? menu_subtree_end(w, parent) : w->nmenu;
    }

    nr = (cg_menu_row_t *)realloc(w->menu, ((size_t)w->nmenu + 1) * sizeof(cg_menu_row_t));
    if (!nr) return -1;
    w->menu = nr;
    if (at < w->nmenu) {
        memmove(&w->menu[at + 1], &w->menu[at],
                ((size_t)w->nmenu - (size_t)at) * sizeof(cg_menu_row_t));
    }
    w->nmenu++;
    /* 索引平移：插在下标 at，则所有 parent >= at 的行都要往后挪一格。
     * 新行自己跳过（它的 parent 马上按 parent 参数写死）。 */
    for (int i = 0; i < w->nmenu; i++) {
        if (i != at && w->menu[i].parent >= at) w->menu[i].parent++;
    }

    memset(&w->menu[at], 0, sizeof(cg_menu_row_t));
    w->menu[at].parent  = parent;
    w->menu[at].text    = cg_str_dup(txt);
    w->menu[at].id      = (id && *id) ? cg_str_dup(id) : NULL;
    w->menu[at].enabled = 1;
    is_sep = (strcmp(txt, "-") == 0);
    /* 命令 id 走**菜单号段**：菜单命令从 WM_COMMAND 的 LOWORD 回来，必须与控件 id
     * 分开（否则控件计数器涨到 0x8000 时会与菜单 / 加速键撞号）—— 与静态菜单
     * （cg_widget.c 的 menu_push_doc）和加速键（cg_action.c）同一个发号器。
     * 分隔线没有命令 id。 */
    if (!is_sep) w->menu[at].cmd = cg_alloc_menu_cmd(app);

    cg_win_menu_rebuild(w);
    return at;
}

/**
 * @brief 运行时删掉一个菜单项（连同它的子树）
 * @param w 菜单控件（menu / menubutton）
 * @param id 要删的项标识，不能为空（没 id 的项没法定位）
 * @return int 删掉的行数（含子树）；控件不是菜单、id 为空或找不到返回 -1
 */
int cg_menu_remove_item(cg_widget_t *w, const char *id)
{
    int k, n;

    if (!want_menu(w, "cg_menu_remove_item")) return -1;
    if (!id || !*id) {
        cg_log(CG_LOG_WARN, "cg_menu_remove_item：id 不能为空（没有 id 的项没法定位）");
        return -1;
    }
    for (k = 0; k < w->nmenu; k++) {
        if (w->menu[k].id && strcmp(w->menu[k].id, id) == 0) break;
    }
    if (k >= w->nmenu) {
        cg_log(CG_LOG_WARN, "cg_menu_remove_item：找不到 id 为 \"%s\" 的菜单项", id);
        return -1;
    }
    n = menu_drop_rows(w, k, menu_subtree_end(w, k));
    if (n > 0) cg_win_menu_rebuild(w);
    return n;
}

/**
 * @brief 清空菜单里的项（某个下拉的全部子项，或整个菜单）
 * @param w 菜单控件（menu / menubutton）
 * @param parent_id 下拉项的 id；NULL 或空串 = 整个菜单（含顶层项及其子树）
 * @return int 删掉的行数；控件不是菜单、parent_id 找不到或它不是下拉返回 -1
 */
int cg_menu_clear_items(cg_widget_t *w, const char *parent_id)
{
    int parent, total = 0;

    if (!want_menu(w, "cg_menu_clear_items")) return -1;
    parent = menu_parent_of(w, parent_id, "cg_menu_clear_items");
    if (parent == -2) return -1;

    /* 从后往前删：每次删除都会平移后面的下标，倒着来就不用重算（也不会连带删错子树） */
    for (int i = w->nmenu - 1; i >= 0; i--) {
        if (w->menu[i].parent != parent) continue;
        total += menu_drop_rows(w, i, menu_subtree_end(w, i));
    }
    if (total > 0) cg_win_menu_rebuild(w);
    return total;
}

/**
 * @brief 定位要改的那一项：找得到 + 不是动作项
 * @param w 菜单控件
 * @param id 项标识，不能为空
 * @param who 调用方函数名，只用于日志前缀
 * @return cg_menu_row_t* 指向行表里那一行（控件内部数据，别 free）；不满足条件返回 NULL
 * @details 项的文字 / 启用 / 勾选归动作管，遇到动作项只记一条 WARN，不悄悄改。
 */
static cg_menu_row_t *menu_edit_target(cg_widget_t *w, const char *id, const char *who)
{
    int k;

    if (!want_menu(w, who)) return NULL;
    if (!id || !*id) {
        cg_log(CG_LOG_WARN, "%s：id 不能为空", who);
        return NULL;
    }
    for (k = 0; k < w->nmenu; k++) {
        if (w->menu[k].id && strcmp(w->menu[k].id, id) == 0) break;
    }
    if (k >= w->nmenu) {
        cg_log(CG_LOG_WARN, "%s：找不到 id 为 \"%s\" 的菜单项", who, id);
        return NULL;
    }
    if (w->menu[k].action && *w->menu[k].action) {
        cg_log(CG_LOG_WARN, "%s：菜单项 \"%s\" 引用了动作 \"%s\" —— 它的文字/启用/勾选"
                            "归动作管，本次不生效（要改就 cg_action_set_*）",
               who, id, w->menu[k].action);
        return NULL;
    }
    return &w->menu[k];
}

/**
 * @brief 改某个菜单项的文字
 * @param w 菜单控件（menu / menubutton）
 * @param id 项标识，不能为空
 * @param text 新文字；NULL 当空串处理，写 "-" 时不生效（只记一条 WARN）
 * @return void 无返回值
 * @details 想把一项变成分隔线（文字写成 "-"）**不允许** —— 那会让它的命令 id 与
 *          "分隔线"这个身份不一致（该用 cg_menu_remove_item 删掉再加一条）。
 */
void cg_menu_set_item_text(cg_widget_t *w, const char *id, const char *text)
{
    cg_menu_row_t *r = menu_edit_target(w, id, "cg_menu_set_item_text");
    const char    *txt = text ? text : "";

    if (!r) return;
    if (strcmp(txt, "-") == 0) {
        cg_log(CG_LOG_WARN, "cg_menu_set_item_text：不能把菜单项改成 \"-\""
                            "（那是分隔线的记号，用 cg_menu_remove_item 删掉再加一条）");
        return;
    }
    free(r->text);
    r->text = cg_str_dup(txt);
    cg_win_menu_rebuild(w);
}

/**
 * @brief 启用 / 灰掉某个菜单项
 * @param w 菜单控件（menu / menubutton）
 * @param id 项标识，不能为空
 * @param enabled 非 0 = 可用，0 = 灰掉
 * @return void 无返回值
 */
void cg_menu_set_item_enabled(cg_widget_t *w, const char *id, int enabled)
{
    cg_menu_row_t *r = menu_edit_target(w, id, "cg_menu_set_item_enabled");

    if (!r) return;
    r->enabled = enabled ? 1 : 0;
    cg_win_menu_rebuild(w);
}

/**
 * @brief 给某个菜单项打勾 / 取消勾选
 * @param w 菜单控件（menu / menubutton）
 * @param id 项标识，不能为空
 * @param checked 非 0 = 打勾，0 = 取消勾选
 * @return void 无返回值
 */
void cg_menu_set_item_checked(cg_widget_t *w, const char *id, int checked)
{
    cg_menu_row_t *r = menu_edit_target(w, id, "cg_menu_set_item_checked");

    if (!r) return;
    r->checked = checked ? 1 : 0;
    cg_win_menu_rebuild(w);
}

/**
 * @brief 改窗口标题（同步后端与文档）
 * @param app 目标应用
 * @param title 新标题；NULL 时直接忽略，不做改动
 * @return void 无返回值
 */
void cg_set_title(cg_app_t *app, const char *title)
{
    if (!app || !title) return;
    if (cg_backend_active && cg_backend_active->set_title) {
        cg_backend_active->set_title(app, title);
    }
    if (app->window) cg_widget_set_text(app->window, title);
}

/**
 * @brief 请求退出消息循环（进程随后结束，退出码 0）
 * @param app 目标应用；NULL 时什么都不做
 * @return void 无返回值
 */
void cg_quit(cg_app_t *app) { if (app) app->quit = 1; }

/**
 * @brief 当前有没有真正的窗口（--selftest 不开窗）
 * @param app 目标应用
 * @return int 1 = 窗口已创建；0 = 没有（含 app 为 NULL）
 * @details 回调里想弹对话框之前先问一句，免得自检 / CI 被弹窗卡住。
 */
int cg_app_has_window(const cg_app_t *app)
{
    return (app && app->hwnd) ? 1 : 0;
}

/**
 * @brief 在 close 回调里调用：这次不关了（"还有没保存的内容"那种）
 * @param app 目标应用；NULL 时什么都不做
 * @return void 无返回值
 */
void cg_cancel_close(cg_app_t *app) { if (app) app->close_cancel = 1; }

/**
 * @brief 把 .cgui 文档树打印到 stdout（调试 / 自检用）
 * @param app 目标应用；NULL 时什么都不做
 * @return void 无返回值
 */
void cg_dump_doc(cg_app_t *app)
{
    if (!app) return;
    cg_doc_dump(&app->doc, stdout);
}

/* ------------------------------------------------------------ 悬停提示 / 时钟 */

/**
 * @brief 改控件悬停提示（空串清除），立即生效
 * @param w 目标控件
 * @param text 提示文字，内部会拷一份；NULL 或空串表示清除
 * @return void 无返回值
 */
void cg_widget_set_hint(cg_widget_t *w, const char *text)
{
    if (!w) return;
    free(w->hint);
    w->hint = (text && *text) ? cg_str_dup(text) : NULL;
    if (w->node) {
        free(w->node->hint);
        w->node->hint = w->hint ? cg_str_dup(w->hint) : NULL;
    }
    cg_win_set_hint(w);
}

/**
 * @brief 读控件悬停提示
 * @param w 目标控件
 * @return const char* 控件内部字符串，别 free；控件为 NULL 返回 NULL，未设置返回空串
 */
const char *cg_widget_hint(const cg_widget_t *w)
{
    return w ? (w->hint ? w->hint : "") : NULL;
}

/**
 * @brief 改时钟（timer 控件）的触发周期，毫秒：0 停止，>0 立即按新周期重新计时
 * @param w 目标控件
 * @param period_ms 新周期（毫秒），负数按 0 处理
 * @return void 无返回值
 */
void cg_widget_set_period(cg_widget_t *w, int period_ms)
{
    if (!w) return;
    if (period_ms < 0) period_ms = 0;
    w->period = period_ms;
    if (w->node) w->node->period = period_ms;
    cg_win_restart_timer(w);
}

/**
 * @brief 读时钟当前周期（毫秒，0 = 停）
 * @param w 目标控件
 * @return int 当前周期毫秒数；控件为 NULL 返回 0
 */
int cg_widget_period(const cg_widget_t *w)
{
    return w ? w->period : 0;
}

/* ------------------------------------------------------------ 动态追加控件 */

/**
 * @brief 运行时用代码追加控件（不进 .cgui），新控件的 id 也参与事件名查找
 * @param app 所属应用，不能为 NULL
 * @param parent 父容器；NULL 表示挂到窗口上
 * @param type 控件类型名，必须是库认得的类型
 * @param id 唯一标识；可传 NULL，传了就不能和已有控件重名
 * @param x 相对父容器的横向位置（逻辑像素）
 * @param y 相对父容器的纵向位置（逻辑像素）
 * @param aw 宽度（逻辑像素），<= 0 时用该类型的默认宽度
 * @param ah 高度（逻辑像素），<= 0 时用该类型的默认高度
 * @param text 初始文字；NULL 时用该类型的默认文字
 * @return cg_widget_t* 新控件（归 app 所有，别自己 free）；类型未知 / id 冲突 / 父不是容器返回 NULL
 */
cg_widget_t *cg_widget_add(cg_app_t *app, cg_widget_t *parent,
                           const char *type, const char *id,
                           int x, int y, int aw, int ah, const char *text)
{
    cg_widget_t *nw;
    const cg_kind_info_t *ki;
    const cg_kind_info_t *pk;

    if (!app) return NULL;
    ki = cg_kind_lookup(type);
    if (!ki) {
        fprintf(stderr, "[cstackgui] cg_widget_add: 未知控件类型 \"%s\"\n", type ? type : "(null)");
        return NULL;
    }
    if (id && cg_widget_by_id(app, id)) {
        fprintf(stderr, "[cstackgui] cg_widget_add: id \"%s\" 已存在\n", id);
        return NULL;
    }
    if (!parent) parent = app->window;
    if (parent && !parent->is_window) {
        pk = cg_kind_lookup(parent->type);
        if (!pk || !pk->container) {
            fprintf(stderr, "[cstackgui] cg_widget_add: \"%s\" (%s) 不是容器\n",
                    parent->id ? parent->id : "-", parent->type ? parent->type : "-");
            return NULL;
        }
    }

    if (aw <= 0) aw = ki->def_w;
    if (ah <= 0) ah = ki->def_h;

    nw = cg_widget_new(app, ki->name, id, x, y, aw, ah,
                       text ? text : ki->def_text);
    if (!nw) return NULL;
    if (parent) cg_widget_attach_child(parent, nw);

    if (cg_backend_active && cg_backend_active->attach) cg_backend_active->attach(nw);
    cg_widget_sync(nw);
    return nw;
}
