/**
 * @file    cg_doc.c
 * @brief   .cgui 解析与打印：JSON -> 控件文档树
 * @details 控件类型表（k_kinds）与 cg_kind_* 查询拆到 cg_doc_kind.c；
 *          那张表要和画布端 model.ts 的 KINDS 保持同步。
 */
#include "cg_doc.h"
#include "cg_json.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ 小工具 */

/**
 * @brief 复制字符串到堆内存（调用方负责 free）
 * @param s 源字符串
 * @return char* 堆副本；失败返回 NULL
 */
static char *dup_str(const char *s)
{
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = (char *)malloc(n + 1);
    if (p) memcpy(p, s, n + 1);
    return p;
}

/**
 * @brief 同 dup_str，但 NULL / 空串一律返回 NULL（可选字段的惯例）
 * @param s 源字符串
 * @return char* 堆副本；空输入返回 NULL
 */
static char *dup_or_null(const char *s)
{
    if (!s || !*s) return NULL;
    return dup_str(s);
}

/**
 * @brief 读一个文件到堆内存
 * @param path 文件路径
 * @return char* UTF-8 原文；失败返回 NULL（调用方负责 free）
 */
char *cg_read_file(const char *path)
{
    FILE *f;
    long  len;
    char *buf;

    if (!path) return NULL;
    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    rewind(f);
    buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }
    if (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[len] = '\0';
    fclose(f);
    return buf;
}

/* ------------------------------------------------------------ 样式 */

static const char *const k_align_names[] = { "left", "center", "right" };

/**
 * @brief 恢复成"什么都没写"的默认值
 * @param st 目标样式；整体清零（size 0 = 用系统 UI 字体，fg/bg 无色）
 * @return void 无返回值
 */
static void style_defaults(cg_style_t *st)
{
    memset(st, 0, sizeof(*st));            /* size 0 = 用系统 UI 字体 */
    st->fg = CG_COLOR_NONE;
    st->bg = CG_COLOR_NONE;
}

/**
 * @brief 把 JSON 对象里**写了**的样式字段盖到目标样式上，并记下写了哪几个
 * @param o JSON 对象（NULL / 非对象直接返回）
 * @param st 目标样式（只被"写了的"字段覆盖，其余保持原样）
 * @param mask 可传 NULL（= 不关心写了哪些；解析样式表时就是这么用的）
 * @return void 无返回值
 * @note 与"无条件铺默认值"的区别是**关键**：合并命名样式时必须分得出
 *       "没写某个字段"和"把某个字段写成默认值"，否则后者会把命名样式里的值顶掉
 */
static void style_overlay(const cj_val_t *o, cg_style_t *st, unsigned *mask)
{
    const cj_val_t *v;

    if (!o || o->type != CJ_OBJ) return;

    if ((v = cj_get(o, "font")) != NULL) {
        const char *s = cj_str(v, NULL);

        /* **取不到值的字段 = 没写**（不置掩码），与下面 align 的口径一致。
         * 理由：写错一个字体名/色值不该把命名样式里那个好值悄悄顶掉 ——
         * 契约原先只把 align 写成这个口径、其余"键存在即算写了"，
         * 那是实现顺手带来的，不是设计（2026-09-27 与画布域对齐时改掉）。 */
        if (s) {
            free(st->font);
            st->font = dup_or_null(s);
            if (mask) *mask |= CG_STYLE_HAS_FONT;
        }
    }
    /* size / bold / radius：与 font 一样**取不到值就算没写**。
     * `cj_int` / `cj_bool` 解析失败只返回传进去的默认值、分不出成功与否，所以传一个
     * **不可能是合法值的哨兵**进去再比对：size/radius 用 INT_MIN，bold 用 -1（它只可能是 0/1）。
     * （这样就不必给 cj_* 加带 ok 出参的兄弟函数。） */
    if ((v = cj_get(o, "size")) != NULL) {
        int n = cj_int(v, -2147483647 - 1);

        if (n != -2147483647 - 1) {
            st->size = n;
            if (mask) *mask |= CG_STYLE_HAS_SIZE;
        }
    }
    if ((v = cj_get(o, "bold")) != NULL) {
        int b = cj_bool(v, -1);

        if (b >= 0) {
            st->bold = b;
            if (mask) *mask |= CG_STYLE_HAS_BOLD;
        }
    }
    /* fg / bg：用 CG_COLOR_NONE 当"没读到"的哨兵（它就是本项目"没有颜色"的表示，
     * 而语法里**没有**"强制无色"的写法 —— 见 CGUI-FORMAT 的已知缺口），
     * 所以读不出颜色时就当这个键没写。 */
    if ((v = cj_get(o, "fg")) != NULL) {
        unsigned c = cj_color(v, CG_COLOR_NONE);

        if (c != CG_COLOR_NONE) {
            st->fg = c;
            if (mask) *mask |= CG_STYLE_HAS_FG;
        } else if (v->type == CJ_STR && v->str && v->str[0] == '@' && v->str[1]) {
            /* "@角色名"：值解析不出是正常的（颜色字面量只认 hex），存引用，
             * 值槽留 NONE —— 主题钩子按当前模式求值写回（深色皮肤 G1/T2）。
             * 引用指向的角色不存在时按"没写"处理（与 use 的失效同口径），解析期不拦。 */
            free(st->fg_role);
            st->fg_role = dup_str(v->str + 1);
            st->fg = CG_COLOR_NONE;
            if (mask) *mask |= CG_STYLE_HAS_FG_ROLE;
        }
    }
    if ((v = cj_get(o, "bg")) != NULL) {
        unsigned c = cj_color(v, CG_COLOR_NONE);

        if (c != CG_COLOR_NONE) {
            st->bg = c;
            if (mask) *mask |= CG_STYLE_HAS_BG;
        } else if (v->type == CJ_STR && v->str && v->str[0] == '@' && v->str[1]) {
            free(st->bg_role);
            st->bg_role = dup_str(v->str + 1);
            st->bg = CG_COLOR_NONE;
            if (mask) *mask |= CG_STYLE_HAS_BG_ROLE;
        }
    }
    if ((v = cj_get(o, "radius")) != NULL) {
        int n = cj_int(v, -2147483647 - 1);      /* 哨兵同 size */

        if (n != -2147483647 - 1) {
            st->radius = n;
            if (mask) *mask |= CG_STYLE_HAS_RADIUS;
        }
    }
    v = cj_get(o, "align");
    if (v && v->type == CJ_STR) {
        for (int i = 0; i < 3; i++) {
            if (v->str && strcmp(v->str, k_align_names[i]) == 0) {
                st->align = (cg_align_t)i;
                if (mask) *mask |= CG_STYLE_HAS_ALIGN;
                break;
            }
        }
    }
}

/**
 * @brief 按名字找命名样式
 * @param styles 命名样式表
 * @param n 表里有效条目数
 * @param name 要查的样式名；空串等同找不到
 * @return const cg_named_style_t* 命中的条目（指向表内部，不转移所有权）；没有返回 NULL
 * @note 找不到**不报错**，按"没有 use"处理
 */
static const cg_named_style_t *style_find(const cg_named_style_t *styles, size_t n, const char *name)
{
    if (!styles || !name || !*name) return NULL;
    for (size_t i = n; i-- > 0; ) {        /* 倒着找：重名时后者覆盖（与 actions 一个口径） */
        if (styles[i].name && strcmp(styles[i].name, name) == 0) return &styles[i];
    }
    return NULL;
}

/**
 * @brief 按名字找色彩角色（roles{} 表，结构与查找口径同 style_find）
 * @param roles 角色表
 * @param n 表里有效条目数
 * @param name 要查的角色名（不带 @）；空串等同找不到
 * @return const cg_role_t* 命中的条目；没有返回 NULL
 */
static const cg_role_t *role_find(const cg_role_t *roles, size_t n, const char *name)
{
    if (!roles || !name || !*name) return NULL;
    for (size_t i = n; i-- > 0; ) {
        if (roles[i].name && strcmp(roles[i].name, name) == 0) return &roles[i];
    }
    return NULL;
}

/**
 * @brief 角色求值：按当前模式取 light / dark 槽
 * @param doc 文档（roles 表在里面）
 * @param name 角色名（不带 @）
 * @param dark 1 = 取 dark 槽，0 = 取 light 槽
 * @return unsigned 颜色值；角色不存在或该槽没写返回 CG_COLOR_NONE（= 当没写）
 * @note core 层纯函数：不给 app 也能算（unit test 用它核对求值口径，不开窗）。
 */
unsigned cg_role_lookup(const cg_doc_t *doc, const char *name, int dark)
{
    const cg_role_t *r = role_find(doc ? doc->roles : NULL, doc ? doc->nroles : 0, name);

    if (!r) return CG_COLOR_NONE;
    if (dark) return r->has_dark ? r->dark : CG_COLOR_NONE;
    return r->has_light ? r->light : CG_COLOR_NONE;
}

/**
 * @brief 解析控件的 `style`（三种形态），**只记下**引用的名字与"自己写了哪几个字段"
 * @param o .cgui 里 style 的值（对象 / 字符串 / NULL）
 * @param st 输出：内联字段已就位（命名样式那部分要等合并命名样式时再填）
 * @param name_out 输出：引用的样式名（调用方 free）；没有引用写 NULL
 * @param mask 输出：写了哪几个字段（CG_STYLE_HAS_* 位或）
 * @return void 无返回值
 * @note 形态：`{...}` 内联 / `"名字"` 纯引用 / `{ "use": "名字", ...覆盖 }`
 */
static void parse_style(const cj_val_t *o, cg_style_t *st, char **name_out, unsigned *mask)
{
    const cj_val_t *use;

    style_defaults(st);
    if (mask) *mask = 0;
    if (name_out) *name_out = NULL;
    if (!o) return;

    if (o->type == CJ_STR) {               /* "style": "log" —— 纯命名引用 */
        if (name_out && o->str && *o->str) *name_out = dup_str(o->str);
        return;
    }
    use = cj_get(o, "use");
    if (name_out && use && use->type == CJ_STR && use->str && *use->str) {
        *name_out = dup_str(use->str);
    }
    style_overlay(o, st, mask);            /* 局部覆盖（"use" 本身不是样式字段） */
}

/**
 * @brief 把命名样式填进控件自己**没写**的字段上（内联优先）
 * @param st 目标样式（已经放着内联字段）
 * @param mask 控件自己写了哪几个字段
 * @param base 命名样式；NULL（找不到那个名字）时什么都不做
 * @return void 无返回值
 */
static void style_merge_named(cg_style_t *st, unsigned mask, const cg_named_style_t *base)
{
    if (!base) return;
    if (!(mask & CG_STYLE_HAS_FONT)) {
        free(st->font);
        st->font = dup_or_null(base->st.font);
    }
    if (!(mask & CG_STYLE_HAS_SIZE))   st->size   = base->st.size;
    if (!(mask & CG_STYLE_HAS_BOLD))   st->bold   = base->st.bold;
    /* fg/bg：命名样式里写的可能是**颜色值**，也可能是 **@角色引用**（值槽是 NONE、串在 *_role）。
     * 控件自己两样都没写才继承，且引用要**连串带位**落下来（求值读的是串，不是值）。 */
    if (!(mask & (CG_STYLE_HAS_FG | CG_STYLE_HAS_FG_ROLE))) {
        if (base->st.fg_role) {
            free(st->fg_role);
            st->fg_role = dup_str(base->st.fg_role);
            st->fg = CG_COLOR_NONE;
        } else {
            st->fg = base->st.fg;
        }
    }
    if (!(mask & (CG_STYLE_HAS_BG | CG_STYLE_HAS_BG_ROLE))) {
        if (base->st.bg_role) {
            free(st->bg_role);
            st->bg_role = dup_str(base->st.bg_role);
            st->bg = CG_COLOR_NONE;
        } else {
            st->bg = base->st.bg;
        }
    }
    if (!(mask & CG_STYLE_HAS_RADIUS)) st->radius = base->st.radius;
    if (!(mask & CG_STYLE_HAS_ALIGN))  st->align  = base->st.align;
}

/**
 * @brief 把整棵树上引用了命名样式的控件都合并一遍，并**统计失效引用**
 * @param n 子树根（调用方传 doc->root）
 * @param doc 所属文档（样式表在里面，失效计数也记在它上面）
 * @return void 无返回值
 * @note 必须等 styles{} **读完**再调用：样式表在 .cgui 里的位置不保证在控件之前。
 *       找不到名字**不是错误**（不崩、不中断加载），但**不能一声不吭** ——
 *       外观会悄悄退回缺省。与动作的"未登记"一个口径：这里计数，收尾报一条 WARN，
 *       `--selftest` 再据此判失败。
 */
static void resolve_named_styles(cg_node_t *n, cg_doc_t *doc)
{
    if (!n || !doc) return;
    if (n->style_name) {
        const cg_named_style_t *base = style_find(doc->styles, doc->nstyles, n->style_name);

        if (base) {
            style_merge_named(&n->style, n->style_mask, base);
        } else {
            if (doc->style_missing == 0) doc->style_missing_name = n->style_name;
            doc->style_missing++;
        }
    }
    /* 角色引用失效计数（口径同 style_missing：不崩不中断，但必须报出来——
     * 求值端对不存在的角色返回"没写"，不报的话外观悄悄退回缺省、无从排查） */
    if ((n->style.fg_role && !role_find(doc->roles, doc->nroles, n->style.fg_role)) ||
        (n->style.bg_role && !role_find(doc->roles, doc->nroles, n->style.bg_role))) {
        if (doc->role_missing == 0)
            doc->role_missing_name = n->style.fg_role ? n->style.fg_role : n->style.bg_role;
        doc->role_missing++;
    }
    for (size_t i = 0; i < n->nchildren; i++) resolve_named_styles(n->children[i], doc);
}

/**
 * @brief 解析顶层 `styles{}`：键是样式名，值是字段子集
 * @param o `styles` 的值
 * @param count 输出：条数
 * @return cg_named_style_t* 数组（调用方 free，条目里的 name/font 也要放）；空/类型不对返回 NULL
 * @note 容错：空名跳过、值不是对象跳过 —— 不报错、不中断加载（与 actions 一个口径）。
 */
static cg_named_style_t *parse_named_styles(const cj_val_t *o, size_t *count)
{
    cg_named_style_t *out;
    size_t            n = 0;

    *count = 0;
    if (!o || o->type != CJ_OBJ || o->count == 0 || !o->keys) return NULL;
    out = (cg_named_style_t *)calloc(o->count, sizeof(cg_named_style_t));
    if (!out) return NULL;

    for (size_t i = 0; i < o->count; i++) {
        const char      *name = o->keys[i];
        const cj_val_t  *v    = o->items[i];

        if (!name || !*name) continue;                  /* 空名：跳过 */
        if (!v || v->type != CJ_OBJ) continue;          /* 值不是对象：跳过 */
        out[n].name = dup_str(name);
        style_defaults(&out[n].st);
        style_overlay(v, &out[n].st, NULL);             /* 样式表里写了的字段全算数 */
        n++;
    }
    *count = n;
    return out;
}

/**
 * @brief 解析顶层 `roles{}`：键是角色名，值是 { light, dark } 两份颜色
 * @param o `roles` 的值
 * @param count 输出：条数
 * @return cg_role_t* 数组（调用方 free，条目里的 name 也要放）；空/类型不对返回 NULL
 * @note 容错（与 styles{} 同口径）：空名跳过、值不是对象跳过、颜色值解析不出 = 该槽"没写"；
 *       重名不查重，靠 role_find 倒序查找实现"后者覆盖"。
 */
static cg_role_t *parse_roles(const cj_val_t *o, size_t *count)
{
    cg_role_t *out;
    size_t     n = 0;

    *count = 0;
    if (!o || o->type != CJ_OBJ || o->count == 0 || !o->keys) return NULL;
    out = (cg_role_t *)calloc(o->count, sizeof(cg_role_t));
    if (!out) return NULL;

    for (size_t i = 0; i < o->count; i++) {
        const char     *name = o->keys[i];
        const cj_val_t *v    = o->items[i];
        unsigned        c;

        if (!name || !*name) continue;                  /* 空名：跳过 */
        if (!v || v->type != CJ_OBJ) continue;          /* 值不是对象：跳过 */
        out[n].name = dup_str(name);
        if ((c = cj_color(cj_get(v, "light"), CG_COLOR_NONE)) != CG_COLOR_NONE) {
            out[n].light = c;
            out[n].has_light = 1;
        }
        if ((c = cj_color(cj_get(v, "dark"), CG_COLOR_NONE)) != CG_COLOR_NONE) {
            out[n].dark = c;
            out[n].has_dark = 1;
        }
        if (!out[n].has_light && !out[n].has_dark) {    /* 两个槽都没写：这条没意义，跳过 */
            free(out[n].name);
            continue;
        }
        n++;
    }
    *count = n;
    return out;
}

/**
 * @brief 释放 style 里的堆字段（font）
 * @param st 目标样式
 * @return void 无返回值
 */
static void style_free(cg_style_t *st)
{
    free(st->font);
    st->font = NULL;
    free(st->fg_role);
    st->fg_role = NULL;
    free(st->bg_role);
    st->bg_role = NULL;
}

/* --------------------------------------------------- 结构化内容的解析 */

/**
 * @brief 解析字符串数组（listbox/combo 等的 items）
 * @param arr JSON 数组
 * @param count 输出：元素个数
 * @return char** 字符串数组（调用方 free）；空/类型不对返回 NULL
 */
static char **parse_str_array(const cj_val_t *arr, size_t *count)
{
    char **out;
    size_t n;

    *count = 0;
    if (!arr || arr->type != CJ_ARR || arr->count == 0) return NULL;

    n = arr->count;
    out = (char **)calloc(n, sizeof(char *));
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        out[i] = dup_str(cj_str(arr->items[i], ""));
    }
    *count = n;
    return out;
}

/**
 * @brief 释放字符串数组
 * @param arr 字符串数组；可传 NULL
 * @param n 数组里有效元素个数
 * @return void 无返回值
 */
static void str_array_free(char **arr, size_t n)
{
    if (!arr) return;
    for (size_t i = 0; i < n; i++) free(arr[i]);
    free(arr);
}

/**
 * @brief 把 tree 的 `nodes` 数组解析成节点数组（递归解 children）
 * @param arr JSON 数组
 * @param count 输出：顶层元素个数
 * @return cg_item_t** 节点指针数组（调用方用释放函数回收）；空 / 类型不对返回 NULL
 * @note 元素形如 `{ "text": "根", "expanded": true, "children": [...] }`；
 *       children 是"指针数组"，因为嵌套深度不定，逐层单独分配比连续数组好写
 */
static cg_item_t **parse_tree_items(const cj_val_t *arr, size_t *count)
{
    cg_item_t **out;
    size_t n;

    *count = 0;
    if (!arr || arr->type != CJ_ARR || arr->count == 0) return NULL;

    n = arr->count;
    out = (cg_item_t **)calloc(n, sizeof(cg_item_t *));
    if (!out) return NULL;

    for (size_t i = 0; i < n; i++) {
        const cj_val_t *o = arr->items[i];
        cg_item_t *it = (cg_item_t *)calloc(1, sizeof(cg_item_t));
        if (!it) break;
        if (o && o->type == CJ_OBJ) {
            it->text = dup_str(cj_str(cj_get(o, "text"), ""));
            it->id = dup_or_null(cj_str(cj_get(o, "id"), NULL));
            /* 菜单项引用动作（树节点读它也无害：tree 不会去看这个字段） */
            it->action = dup_or_null(cj_str(cj_get(o, "action"), NULL));
            it->expanded = cj_bool(cj_get(o, "expanded"), 0);
            it->checked = cj_bool(cj_get(o, "checked"), 0);
            it->children = parse_tree_items(cj_get(o, "children"), &it->nchildren);
        } else {
            it->text = dup_str(cj_str(o, ""));
        }
        out[i] = it;
    }
    *count = n;
    return out;
}

/**
 * @brief 递归释放树节点数组（含子节点）
 * @param items 节点指针数组；可传 NULL
 * @param n 数组里节点个数
 * @return void 无返回值
 */
static void tree_items_free(cg_item_t **items, size_t n)
{
    if (!items) return;
    for (size_t i = 0; i < n; i++) {
        if (!items[i]) continue;
        tree_items_free(items[i]->children, items[i]->nchildren);
        free(items[i]->text);
        free(items[i]->id);
        free(items[i]->action);
        free(items[i]);
    }
    free(items);
}

/**
 * @brief 把表格的 `columns` 数组解析成列数组
 * @param arr JSON 数组
 * @param count 输出：列数
 * @return cg_col_t* 列数组（调用方逐个 free title 再 free 本体）；空 / 类型不对返回 NULL
 * @note 对象写法 `{ "title": "姓名", "w": 120 }`，也允许
 *       `"columns": ["姓名","年龄"]` 这样的纯字符串写法（此时宽度 0 = 均分）
 */
static cg_col_t *parse_columns(const cj_val_t *arr, size_t *count)
{
    cg_col_t *out;
    size_t n;

    *count = 0;
    if (!arr || arr->type != CJ_ARR || arr->count == 0) return NULL;

    n = arr->count;
    out = (cg_col_t *)calloc(n, sizeof(cg_col_t));
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        const cj_val_t *o = arr->items[i];
        if (o && o->type == CJ_STR) {                 /* 允许 "columns": ["姓名","年龄"] */
            out[i].title = dup_str(cj_str(o, ""));
            out[i].width = 0;                         /* 0 = 均分 */
        } else {
            out[i].title = dup_str(cj_str(cj_get(o, "title"), ""));
            out[i].width = cj_int(cj_get(o, "w"), 0);
        }
    }
    *count = n;
    return out;
}

/**
 * @brief 把表格的 `rows` 数组解析成二维字符串数组（各行按最宽的一行补齐）
 * @param arr JSON 数组
 * @param nrows 输出：行数
 * @param row_cells 输出：每行的单元格个数（取所有行里最多的；一行都没有时按 1）
 * @return char*** 行数组（调用方用释放函数回收）；空 / 类型不对返回 NULL
 * @note 形如 `"rows": [["张三","18"], ["李四","20"]]`
 */
static char ***parse_rows(const cj_val_t *arr, size_t *nrows, size_t *row_cells)
{
    char ***out;
    size_t  n, cells = 0;

    *nrows = 0;
    *row_cells = 0;
    if (!arr || arr->type != CJ_ARR || arr->count == 0) return NULL;

    n = arr->count;
    out = (char ***)calloc(n, sizeof(char **));
    if (!out) return NULL;

    for (size_t i = 0; i < n; i++) {
        const cj_val_t *row = arr->items[i];
        if (row && row->type == CJ_ARR && row->count > cells) cells = row->count;
    }
    if (cells == 0) cells = 1;

    for (size_t i = 0; i < n; i++) {
        const cj_val_t *row = arr->items[i];
        out[i] = (char **)calloc(cells, sizeof(char *));
        if (!out[i]) continue;
        for (size_t c = 0; c < cells; c++) {
            const cj_val_t *cell = (row && row->type == CJ_ARR && c < row->count) ? row->items[c] : NULL;
            out[i][c] = dup_str(cj_str(cell, ""));
        }
    }
    *nrows = n;
    *row_cells = cells;
    return out;
}

/**
 * @brief 释放表格行二维数组
 * @param rows 行数组；可传 NULL
 * @param nrows 行数
 * @param cells 每行的单元格个数（与解析时拿到的值一致）
 * @return void 无返回值
 */
static void rows_free(char ***rows, size_t nrows, size_t cells)
{
    if (!rows) return;
    for (size_t i = 0; i < nrows; i++) {
        if (!rows[i]) continue;
        for (size_t c = 0; c < cells; c++) free(rows[i][c]);
        free(rows[i]);
    }
    free(rows);
}

/* ------------------------------------------------------------ 节点 */

/**
 * @brief 递归释放文档节点（子节点、结构化内容、事件名、样式全清）
 * @param n 目标节点
 * @return void 无返回值
 */
static void node_free(cg_node_t *n)
{
    if (!n) return;
    for (size_t i = 0; i < n->nchildren; i++) node_free(n->children[i]);
    free(n->children);
    for (int i = 0; i < CG_EVENT_COUNT; i++) free(n->ev[i]);
    str_array_free(n->items, n->nitems);
    tree_items_free(n->nodes, n->nnodes);
    if (n->cols) {
        for (size_t i = 0; i < n->ncols; i++) free(n->cols[i].title);
        free(n->cols);
    }
    rows_free(n->rows, n->nrows, n->row_cells);
    style_free(&n->style);
    free(n->type);
    free(n->id);
    free(n->text);
    free(n->style_name);
    free(n->src);
    free(n->format);
    free(n->hint);
    free(n->sport);
    free(n->bind_src);
    free(n->action);
    free(n);
}

/**
 * @brief 给文档节点挂一个子节点（数组尾插，child->parent 回填）
 * @param parent 父节点
 * @param child 子节点
 * @return void 无返回值（分配失败静默丢弃）
 */
static void node_add_child(cg_node_t *parent, cg_node_t *child)
{
    cg_node_t **nc = (cg_node_t **)realloc(parent->children, (parent->nchildren + 1) * sizeof(cg_node_t *));
    if (!nc) return;
    parent->children = nc;
    parent->children[parent->nchildren++] = child;
    child->parent = parent;
}

static int s_auto_id = 0;

/**
 * @brief 解析单个控件节点（矩形/样式/状态/事件/结构化内容/children 递归）
 * @param o JSON 对象
 * @param err 解析失败时写错误信息
 * @param errsz err 容量
 * @return cg_node_t* 节点；失败返回 NULL
 */
static cg_node_t *parse_node(const cj_val_t *o, char *err, size_t errsz)
{
    cg_node_t *n;
    const cj_val_t *v, *rect;
    const cg_kind_info_t *ki;
    const char *type;
    char auto_id[32];
    int alias_multi, alias_spin;

    if (!o || o->type != CJ_OBJ) {
        snprintf(err, errsz, "widget entry is not an object");
        return NULL;
    }

    type = cj_str(cj_get(o, "type"), "label");
    /* 老写法自动升级：multiedit、spinner 已合并进 edit（2026-09-22）。
     * 读到旧名不报错，按新字段补齐（见下面两处），dump 出去就已经是 edit ——
     * 这份兼容必须留在解析层：未知类型是**整份文档解析失败**，不是降级成标签。 */
    alias_multi = (strcmp(type, "multiedit") == 0);
    alias_spin  = (strcmp(type, "spinner") == 0);
    if (alias_multi || alias_spin) type = "edit";
    ki = cg_kind_lookup(type);
    if (!ki) {
        snprintf(err, errsz, "unknown widget type \"%s\"", type);
        return NULL;
    }

    n = (cg_node_t *)calloc(1, sizeof(cg_node_t));
    if (!n) { snprintf(err, errsz, "out of memory"); return NULL; }

    n->type = dup_str(ki->name);
    v = cj_get(o, "id");
    if (v && v->type == CJ_STR && v->str && *v->str) {
        n->id = dup_str(v->str);
    } else {
        snprintf(auto_id, sizeof(auto_id), "%s_%d", ki->name, ++s_auto_id);
        n->id = dup_str(auto_id);
    }
    /* text 缺失就是**空文字**：.cgui 是唯一真相，别拿类型的默认文字回填。
     * def_text（"按钮"/"标签"/…）只服务于画布调色板新建控件和 cg_widget_add()
     * 程序化创建；在解析时回填会让"画布上清空的文字"被顶回来，
     * 表现出来就是空文字按钮里又长出一个"按钮"（图片按钮最明显）。 */
    n->text = dup_str(cj_str(cj_get(o, "text"), ""));

    /* rect 既支持 {x,y,w,h} 对象，也支持扁平的 x/y/w/h */
    rect = cj_get(o, "rect");
    n->x = cj_int(cj_get(rect, "x"), cj_int(cj_get(o, "x"), 0));
    n->y = cj_int(cj_get(rect, "y"), cj_int(cj_get(o, "y"), 0));
    n->w = cj_int(cj_get(rect, "w"), cj_int(cj_get(o, "w"), ki->def_w));
    n->h = cj_int(cj_get(rect, "h"), cj_int(cj_get(o, "h"), ki->def_h));
    if (n->w < 0) n->w = 0;
    if (n->h < 0) n->h = 0;

    /* grow：余量分配权重。走 cj_int 的常规口径（小数四舍五入、true 当 1、数字串按数字读），
     * 随后**负数与超过 99 的归 0** —— 不报错、不中断加载。
     * 注意别把这里写成"非整数当 0"：契约里那条口径漂移过，源头就是这句注释（2026-09-27 更正）。 */
    n->grow = cj_int(cj_get(o, "grow"), 0);
    if (n->grow < 0 || n->grow > 99) n->grow = 0;

    /* style 三种形态都收（契约 §2）：内联对象 / "名字" / { "use": "名字", ...覆盖 }。
     * 这里**只记下引用的名字与"自己写了哪几个字段"**，真正的合并等 styles{} 解析完
     * 再统一做（resolve_named_styles()）—— 因为样式表在文档里的位置不保证在控件之前。 */
    parse_style(cj_get(o, "style"), &n->style, &n->style_name, &n->style_mask);

    /* state 与顶层字段都读，顶层优先（方便手写小文件） */
    {
        const cj_val_t *st = cj_get(o, "state");
        n->visible = cj_bool(cj_get(o, "visible"), cj_bool(cj_get(st, "visible"), 1));
        n->enabled = cj_bool(cj_get(o, "enabled"), cj_bool(cj_get(st, "enabled"), 1));
        n->checked = cj_bool(cj_get(o, "checked"), cj_bool(cj_get(st, "checked"), 0));
        n->value   = cj_int(cj_get(o, "value"), cj_int(cj_get(st, "value"), 0));
        n->vmin    = cj_int(cj_get(o, "min"), cj_int(cj_get(st, "min"), 0));
        n->vmax    = cj_int(cj_get(o, "max"), cj_int(cj_get(st, "max"), 100));
    }
    if (n->vmax <= n->vmin) n->vmax = n->vmin + 1;

    v = cj_get(o, "events");
    if (v && v->type == CJ_OBJ) {
        for (size_t i = 0; i < v->count; i++) {
            const char *key = (v->keys && v->keys[i]) ? v->keys[i] : NULL;
            const char *val = cj_str(v->items[i], NULL);
            int ev = key ? cg_event_from_name(key) : -1;
            if (ev >= 0 && val && *val) n->ev[ev] = dup_str(val);
        }
    }

    /* 标签页归属、图片路径、细分格式、悬停提示、时钟周期、表格增强 */
    n->page = cj_int(cj_get(o, "page"), 0);
    if (n->page < 0) n->page = 0;
    n->src = dup_or_null(cj_str(cj_get(o, "src"), NULL));
    n->format = dup_or_null(cj_str(cj_get(o, "format"), NULL));
    n->hint = dup_or_null(cj_str(cj_get(o, "hint"), NULL));
    n->period = cj_int(cj_get(o, "period"), 0);
    if (n->period < 0) n->period = 0;

    /* 串口组件（serial）：越界值一律回退缺省，别让手写的 .cgui 造出怪参数 */
    n->sport = dup_or_null(cj_str(cj_get(o, "port"), NULL));
    n->s_baud = cj_int(cj_get(o, "baud"), 9600);
    if (n->s_baud <= 0) n->s_baud = 9600;
    n->s_bits = cj_int(cj_get(o, "bits"), 8);
    if (n->s_bits < 5 || n->s_bits > 8) n->s_bits = 8;
    n->s_stop = cj_int(cj_get(o, "stop"), 1);
    if (n->s_stop < 1 || n->s_stop > 2) n->s_stop = 1;
    n->s_parity = cj_int(cj_get(o, "parity"), 0);
    if (n->s_parity < 0 || n->s_parity > 2) n->s_parity = 0;
    n->s_auto = cj_bool(cj_get(o, "auto"), 0);
    n->multi = cj_bool(cj_get(o, "multi"), 0);
    n->check = cj_bool(cj_get(o, "check"), 0);
    /* dialog 的居中开关：缺省 1（旧文件没这个字段也居中）；显式 false 才按 rect.x/y
       屏幕坐标摆（与 app.x/y 同一口径），见 cg_win_modal.c 的 dialog_create_native */
    n->center = cj_bool(cj_get(o, "center"), 1);

    /* edit（单行输入）的输入框行为。越界值一律退回缺省：.cgui 允许手写，
     * 一个 scroll=9 不该让输入框变成"没有滚动条但也不能输入"的怪状态。
     * 对齐方式不在这里 —— 它复用 style.align（和标签同一个字段）。 */
    n->multiline = cj_bool(cj_get(o, "multiline"), 0);
    n->scroll = cj_int(cj_get(o, "scroll"), 0);
    if (n->scroll < 0 || n->scroll > 3) n->scroll = 0;
    n->maxlen = cj_int(cj_get(o, "maxlen"), 0);
    if (n->maxlen < 0) n->maxlen = 0;
    n->conv = cj_int(cj_get(o, "conv"), 0);
    if (n->conv != 1 && n->conv != 2) n->conv = 0;
    n->input = cj_int(cj_get(o, "input"), 0);
    if (n->input < 0 || n->input > 11) n->input = 0;   /* 见 cg_doc.h 的输入方式档位 */
    n->border = cj_int(cj_get(o, "border"), 0);
    if (n->border < 0 || n->border > 6) n->border = 0;
    n->spin = cj_int(cj_get(o, "spin"), 0);
    if (n->spin < 0 || n->spin > 2) n->spin = 0;

    /* 旧类型名补齐字段（见函数开头 alias_*）。写在前面的字段优先，作者写了的就不动。 */
    if (alias_multi) {                     /* multiedit：以前写死 ES_MULTILINE + 纵向滚动 */
        n->multiline = 1;
        if (n->scroll == 0) n->scroll = 2;
    }
    if (alias_spin) {                      /* spinner：以前是独立的 EDIT + 上下箭头 */
        if (n->spin == 0) n->spin = 1;     /* 自动：±1 并夹到 min/max */
        if (n->input == 0) n->input = 7;   /* 整数：字符只放行数字与负号，失焦夹到范围内 */
        /* 不用管 text：数值框的显示文本由 value 派生（见 cg_widget.c 的 build_node），
         * 这样老文件里留空的 text 不会被写成一个会和 value 漂移的副本。 */
    }

    /* 引用动作：顶层 actions 里的名字。这里只记名字，校验（动作是否存在、该类型
     * 支不支持 click、有没有同时绑 click）在运行时构建时做 —— 解析层不该知道
     * "哪些类型支持 click"（那是 k_kinds 的事，进来一次就查一次反而更贵）。 */
    n->action = dup_or_null(cj_str(cj_get(o, "action"), NULL));

    /* 控件绑定：bind { "src": "源控件id", "two": true }
     * 只认对象写法；src 空串等于没绑（画布没选源时就是这么写的） */
    v = cj_get(o, "bind");
    if (v && v->type == CJ_OBJ) {
        const char *bind_src = cj_str(cj_get(v, "src"), NULL);
        if (bind_src && *bind_src) {
            n->bind_src = dup_str(bind_src);
            n->bind_two = cj_bool(cj_get(v, "two"), 0);
        }
    }

    /* 结构化内容 */
    n->items = parse_str_array(cj_get(o, "items"), &n->nitems);
    n->nodes = parse_tree_items(cj_get(o, "nodes"), &n->nnodes);
    n->cols  = parse_columns(cj_get(o, "columns"), &n->ncols);
    n->rows  = parse_rows(cj_get(o, "rows"), &n->nrows, &n->row_cells);

    v = cj_get(o, "children");
    if (v && v->type == CJ_ARR) {
        if (!ki->container) {
            snprintf(err, errsz, "widget \"%s\" (%s) cannot hold children", n->id, n->type);
            node_free(n);
            return NULL;
        }
        for (size_t i = 0; i < v->count; i++) {
            cg_node_t *c = parse_node(v->items[i], err, errsz);
            if (!c) { node_free(n); return NULL; }
            node_add_child(n, c);
        }
    }
    return n;
}

/* ------------------------------------------------------------ 文档 */

/**
 * @brief 解析整份 .cgui 文本
 * @param json UTF-8 JSON 原文
 * @param err 解析失败时写错误信息
 * @param errsz err 容量
 * @return cg_doc_t* 文档（调用方 cg_doc_free）；失败返回 NULL
 */
cg_doc_t *cg_doc_parse(const char *json, char *err, size_t errsz)
{
    cj_val_t *root;
    cg_doc_t *doc;
    const cj_val_t *v, *app, *ws;
    char errbuf[256] = { 0 };

    errbuf[0] = '\0';
    root = cj_parse(json, errbuf, sizeof(errbuf));
    if (!root) {
        snprintf(err, errsz, "JSON: %s", errbuf[0] ? errbuf : "parse failed");
        return NULL;
    }
    if (root->type != CJ_OBJ) {
        cj_free(root);
        snprintf(err, errsz, "top level of a .cgui must be an object");
        return NULL;
    }

    doc = (cg_doc_t *)calloc(1, sizeof(cg_doc_t));
    if (!doc) { cj_free(root); snprintf(err, errsz, "out of memory"); return NULL; }

    v = cj_get(root, "cgui");
    if (v && cj_int(v, 1) > 1) {
        snprintf(err, errsz, "unsupported .cgui version %d (runtime speaks 1)", cj_int(v, 1));
        cj_free(root);
        free(doc);
        return NULL;
    }

    app = cj_get(root, "app");
    doc->title = dup_str(cj_str(cj_get(app, "title"), "CStackGUI Application"));
    doc->width = cj_int(cj_get(app, "width"), 640);
    doc->height = cj_int(cj_get(app, "height"), 420);
    doc->bg = cj_color(cj_get(app, "bg"), CG_COLOR_NONE);
    if (doc->bg == CG_COLOR_NONE) {
        /* "@角色"：值解析不出是正常的，存引用（求值在主题初始化/切换时） */
        const cj_val_t *bgv = cj_get(app, "bg");

        if (bgv && bgv->type == CJ_STR && bgv->str && bgv->str[0] == '@' && bgv->str[1]) {
            free(doc->bg_role);
            doc->bg_role = dup_str(bgv->str + 1);
        }
    }
    if (doc->width < 80) doc->width = 80;
    if (doc->height < 60) doc->height = 60;

    /* app.dark：true / false / "auto"。auto 只记成 -1，**由后端**读系统主题
     * （core 平台中立，不许碰 HKCU 注册表；后端把 -1 解析成实际状态）。 */
    {
        const cj_val_t *dk = cj_get(app, "dark");

        if (dk && dk->type == CJ_STR && dk->str && strcmp(dk->str, "auto") == 0) doc->dark = -1;
        else doc->dark = cj_bool(dk, 0) ? 1 : 0;
    }

    /* 窗口属性（app 节点，全部可选；不写就保持 Windows 默认观感） */
    {
        const cj_val_t *vx = cj_get(app, "x");
        const cj_val_t *vy = cj_get(app, "y");
        doc->has_pos = (vx != NULL || vy != NULL);
        doc->x = cj_int(vx, 0);
        doc->y = cj_int(vy, 0);
        doc->center = cj_bool(cj_get(app, "center"), 0);
        doc->border = dup_str(cj_str(cj_get(app, "border"), "sizable"));
        doc->control_box = cj_bool(cj_get(app, "controlBox"), 1);
        doc->min_box = cj_bool(cj_get(app, "minimizeBox"), 1);
        doc->max_box = cj_bool(cj_get(app, "maximizeBox"), 1);
        doc->topmost = cj_bool(cj_get(app, "topmost"), 0);
        doc->taskbar = cj_bool(cj_get(app, "taskbar"), 1);
        doc->visible = cj_bool(cj_get(app, "visible"), 1);
        doc->enabled = cj_bool(cj_get(app, "enabled"), 1);
        doc->enter_tab = cj_bool(cj_get(app, "enterTab"), 0);
        doc->esc_close = cj_bool(cj_get(app, "escClose"), 1);
        doc->tray = cj_bool(cj_get(app, "tray"), 0);
        doc->icon = dup_str(cj_str(cj_get(app, "icon"), NULL));
        doc->cursor = dup_str(cj_str(cj_get(app, "cursor"), NULL));
        doc->class_name = dup_str(cj_str(cj_get(app, "class"), NULL));
        doc->note = dup_str(cj_str(cj_get(app, "note"), NULL));
    }

    /* 程序配置（app.info）：编译进 exe 的版本信息 + 运行时能读到的程序信息 */
    {
        const cj_val_t *in = cj_get(app, "info");
        doc->info.product  = dup_str(cj_str(cj_get(in, "product"), NULL));
        doc->info.desc     = dup_str(cj_str(cj_get(in, "desc"), NULL));
        doc->info.comments = dup_str(cj_str(cj_get(in, "comments"), NULL));
        doc->info.version  = dup_str(cj_str(cj_get(in, "version"), NULL));
        doc->info.build    = dup_str(cj_str(cj_get(in, "build"), NULL));
        doc->info.author   = dup_str(cj_str(cj_get(in, "author"), NULL));
        doc->info.company  = dup_str(cj_str(cj_get(in, "company"), NULL));
        doc->info.copyright = dup_str(cj_str(cj_get(in, "copyright"), NULL));
        doc->info.address  = dup_str(cj_str(cj_get(in, "address"), NULL));
        doc->info.postcode = dup_str(cj_str(cj_get(in, "postcode"), NULL));
        doc->info.phone    = dup_str(cj_str(cj_get(in, "phone"), NULL));
        doc->info.fax      = dup_str(cj_str(cj_get(in, "fax"), NULL));
        doc->info.email    = dup_str(cj_str(cj_get(in, "email"), NULL));
        doc->info.homepage = dup_str(cj_str(cj_get(in, "homepage"), NULL));
    }

    /* 顶层 actions：命名动作表。**键就是动作名**（不是数组，所以顺序由书写顺序定）。
     * 两个宽容处，都写在文档里：空名的条目直接跳过（没有引用它的办法）；
     * 重名的后者覆盖（画布不会写出来，只有手写 .cgui 才可能）。
     * 缺 handler 的动作**不在这里报错**：它是 UI 便利设施，不该让整份界面打不开 ——
     * 运行时构建时警告，--selftest 报错（与"绑了不支持的事件"同一套兜底）。 */
    v = cj_get(root, "actions");
    if (v && v->type == CJ_OBJ && v->count > 0) {
        doc->actions = (cg_action_t *)calloc(v->count, sizeof(cg_action_t));
        if (doc->actions) {
            for (size_t i = 0; i < v->count; i++) {
                const cj_val_t *o = v->items[i];
                const char *name = (v->keys && v->keys[i]) ? v->keys[i] : NULL;
                cg_action_t *a;
                size_t j;

                if (!name || !*name) continue;

                for (j = 0; j < doc->nactions; j++) {
                    if (doc->actions[j].name && strcmp(doc->actions[j].name, name) == 0) break;
                }
                if (j < doc->nactions) {
                    a = &doc->actions[j];          /* 重名：后者覆盖，名字保留 */
                    free(a->text);
                    free(a->accel);
                    free(a->handler);
                    a->text = a->accel = a->handler = NULL;
                } else {
                    a = &doc->actions[doc->nactions++];
                    a->name = dup_str(name);
                }
                a->text    = dup_or_null(cj_str(cj_get(o, "text"), NULL));
                a->accel   = dup_or_null(cj_str(cj_get(o, "accel"), NULL));
                a->handler = dup_or_null(cj_str(cj_get(o, "handler"), NULL));
                a->checked = cj_bool(cj_get(o, "checked"), 0);
                a->enabled = cj_bool(cj_get(o, "enabled"), 1);
            }
        }
    }

    doc->root = (cg_node_t *)calloc(1, sizeof(cg_node_t));
    if (!doc->root) {
        cj_free(root);
        cg_doc_free(doc);   /* 窗口属性那些字段也要一起放（别只手写 free(title)） */
        snprintf(err, errsz, "out of memory");
        return NULL;
    }
    doc->root->type = dup_str("window");
    doc->root->id = dup_str("__window");
    doc->root->w = doc->width;
    doc->root->h = doc->height;

    /* 窗口级事件写成 app.events.close，挂在虚拟根上（窗口自身也是个"控件"） */
    {
        const cj_val_t *ae = cj_get(app, "events");
        if (ae && ae->type == CJ_OBJ) {
            for (int i = 0; i < CG_EVENT_COUNT; i++) {
                const char *cb = cj_str(cj_get(ae, cg_event_name((cg_event_t)i)), NULL);
                if (cb && *cb) doc->root->ev[i] = dup_str(cb);
            }
        }
    }

    ws = cj_get(root, "widgets");
    if (ws && ws->type != CJ_ARR) {
        snprintf(err, errsz, "\"widgets\" must be an array");
        cj_free(root);
        cg_doc_free(doc);
        return NULL;
    }
    /* 顶层 styles：命名样式表（与 actions 同级）。控件解析时**只记名字**，
     * 合并统一放到树建完之后 —— 样式表在 .cgui 里的位置不保证在控件之前。 */
    doc->styles = parse_named_styles(cj_get(root, "styles"), &doc->nstyles);
    /* 顶层 roles：色彩角色表（深色皮肤 G1）。引用只记名字，求值统一在主题钩子里 ——
     * 表的位置同样不保证在控件之前，而求值要到建树/切主题才发生，天然没问题。 */
    doc->roles = parse_roles(cj_get(root, "roles"), &doc->nroles);
    /* 顶层 include[]（F3 多文件组装）：这里只把"要引用谁"记下来 —— 真正的展开在
     * app_open 里做（那时才知道主文件在哪个目录）。条目宽容：path 不是非空字符串
     * 就丢掉不报错（与 styles 的读入口径一致）。 */
    v = cj_get(root, "include");
    if (v && v->type == CJ_ARR && cj_count(v) > 0) {
        doc->includes = (cg_include_t *)calloc(cj_count(v), sizeof(cg_include_t));
        if (doc->includes) {
            for (size_t i = 0; i < cj_count(v); i++) {
                const cj_val_t *it = cj_at(v, i);
                const char *p = it ? cj_str(cj_get(it, "path"), NULL) : NULL;

                if (!p || !*p) continue;
                doc->includes[doc->nincludes].path = dup_str(p);
                doc->includes[doc->nincludes].pick = dup_or_null(cj_str(cj_get(it, "pick"), NULL));
                doc->nincludes++;
            }
        }
    }

    for (size_t i = 0; i < cj_count(ws); i++) {
        cg_node_t *n = parse_node(cj_at(ws, i), err, errsz);
        if (!n) {
            cj_free(root);
            cg_doc_free(doc);
            return NULL;
        }
        node_add_child(doc->root, n);
    }

    /* 树建完了，把命名样式合并进各控件（命名打底、内联优先）。
     * **不管有没有 styles{} 都要走一遍** —— 没有样式表时引用才更能看出是失效的
     * （stats 计数与那条 WARN 都在这里产生）。 */
    resolve_named_styles(doc->root, doc);
    if (doc->style_missing) {
        cg_logm(CG_LOG_WARN, "doc", "有 %d 个控件引用了不存在的样式名（第一个是 \"%s\"）：这些控件按「没写 style」处理，外观退回缺省",
               doc->style_missing, doc->style_missing_name ? doc->style_missing_name : "?");
    }
    if (doc->role_missing) {
        cg_logm(CG_LOG_WARN, "doc", "有 %d 个控件引用了不存在的角色名（第一个是 \"@%s\"）：这些引用按「没写」处理，颜色退回缺省",
               doc->role_missing, doc->role_missing_name ? doc->role_missing_name : "?");
    } else if (doc->bg_role && !role_find(doc->roles, doc->nroles, doc->bg_role)) {
        doc->role_missing = 1;
        doc->role_missing_name = doc->bg_role;
        cg_logm(CG_LOG_WARN, "doc", "app.bg 引用了不存在的角色名 \"@%s\"：按「没写」处理", doc->bg_role);
    }

    cj_free(root);
    return doc;
}

/**
 * @brief 释放文档内容（doc 本身不动 —— 它可能内嵌在应用结构里，或是栈变量）
 * @param doc 目标文档；可传 NULL（内容清空后本身仍可复用）
 * @return void 无返回值
 */
void cg_doc_release(cg_doc_t *doc)
{
    if (!doc) return;
    free(doc->title);
    free(doc->border);
    free(doc->icon);
    free(doc->cursor);
    free(doc->class_name);
    free(doc->note);
    free(doc->info.product);  free(doc->info.desc);     free(doc->info.comments);
    free(doc->info.version);  free(doc->info.build);    free(doc->info.author);
    free(doc->info.company);  free(doc->info.copyright); free(doc->info.address);
    free(doc->info.postcode); free(doc->info.phone);    free(doc->info.fax);
    free(doc->info.email);    free(doc->info.homepage);
    if (doc->actions) {
        for (size_t i = 0; i < doc->nactions; i++) {
            free(doc->actions[i].name);
            free(doc->actions[i].text);
            free(doc->actions[i].accel);
            free(doc->actions[i].handler);
        }
        free(doc->actions);
    }
    if (doc->styles) {
        for (size_t i = 0; i < doc->nstyles; i++) {
            free(doc->styles[i].name);
            free(doc->styles[i].st.font);
            free(doc->styles[i].st.fg_role);
            free(doc->styles[i].st.bg_role);
        }
        free(doc->styles);
    }
    if (doc->roles) {
        for (size_t i = 0; i < doc->nroles; i++) free(doc->roles[i].name);
        free(doc->roles);
    }
    if (doc->includes) {
        for (size_t i = 0; i < doc->nincludes; i++) {
            free(doc->includes[i].path);
            free(doc->includes[i].pick);
        }
        free(doc->includes);
    }
    free(doc->bg_role);
    node_free(doc->root);
    memset(doc, 0, sizeof(*doc));
}

/**
 * @brief 释放文档内容并释放 doc 本身（只给解析函数的返回值用）
 * @param doc 解析函数返回的文档；可传 NULL
 * @return void 无返回值
 */
void cg_doc_free(cg_doc_t *doc)
{
    if (!doc) return;
    cg_doc_release(doc);
    free(doc);
}

/* ------------------------------------------------------------ 打印 */

/* ------------------------------------------------------------ 打印 */

/**
 * @brief 以缩进形式递归打印一个节点（含状态、事件绑定、结构化内容摘要）
 * @param n 节点
 * @param out 输出流
 * @param depth 缩进层数（顶层子节点传 1，每下一层加一）
 * @return void 无返回值
 */
static void dump_node(const cg_node_t *n, FILE *out, int depth)
{
    for (int i = 0; i < depth; i++) fputs("  ", out);
    fprintf(out, "%s #%s rect=(%d,%d,%d,%d) text=\"%s\"",
            n->type ? n->type : "?", n->id ? n->id : "-", n->x, n->y, n->w, n->h,
            n->text ? n->text : "");
    if (!n->visible) fputs(" hidden", out);
    if (!n->enabled) fputs(" disabled", out);
    if (n->checked) fputs(" checked", out);
    if (n->page) fprintf(out, " page=%d", n->page);
    if (n->src) fprintf(out, " src=%s", n->src);
    if (n->format) fprintf(out, " format=%s", n->format);
    if (n->hint) fprintf(out, " hint=\"%s\"", n->hint);
    if (n->period) fprintf(out, " period=%d", n->period);
    if (n->sport)
        fprintf(out, " port=%s baud=%d bits=%d stop=%d parity=%d%s",
                n->sport, n->s_baud, n->s_bits, n->s_stop, n->s_parity,
                n->s_auto ? " auto" : "");
    if (n->bind_src) fprintf(out, " bind=%s%s", n->bind_src, n->bind_two ? "+two" : "");
    if (n->action) fprintf(out, " action=%s", n->action);
    if (n->multi) fputs(" multi", out);
    if (n->check) fputs(" check", out);
    if (n->multiline) fputs(" multiline", out);
    if (n->scroll) fprintf(out, " scroll=%d", n->scroll);
    if (n->maxlen) fprintf(out, " maxlen=%d", n->maxlen);
    if (n->conv) fprintf(out, " conv=%d", n->conv);
    if (n->input) fprintf(out, " input=%d", n->input);
    if (n->border) fprintf(out, " border=%d", n->border);
    if (n->spin) fprintf(out, " spin=%d", n->spin);
    if (n->items) fprintf(out, " items=%lu", (unsigned long)n->nitems);
    if (n->nodes) fprintf(out, " tree=%lu", (unsigned long)n->nnodes);
    if (n->cols) fprintf(out, " cols=%lu", (unsigned long)n->ncols);
    if (n->rows) fprintf(out, " rows=%lu", (unsigned long)n->nrows);
    /* grow 也要打出来：示例域的队友指出过——没有它，`selftest OK` 只能证明"文件读进去了"，
     * 证明不了 grow 真的进了控件（门禁对它是盲的）。只在非 0 时打，保持旧输出不变。 */
    if (n->grow) fprintf(out, " grow=%d", n->grow);
    if (n->style.font || n->style.size)
        fprintf(out, " font=%s/%d%s", n->style.font ? n->style.font : "-", n->style.size,
                n->style.bold ? " bold" : "");
    for (int i = 0; i < CG_EVENT_COUNT; i++) {
        if (n->ev[i]) fprintf(out, " %s->%s", cg_event_name((cg_event_t)i), n->ev[i]);
    }
    fputc('\n', out);
    for (size_t i = 0; i < n->nchildren; i++) dump_node(n->children[i], out, depth + 1);
}

/**
 * @brief 把整份文档树打印到 out
 * @param doc 文档
 * @param out 输出流
 * @return void 无返回值（自检 / 排查用）
 */
void cg_doc_dump(const cg_doc_t *doc, FILE *out)
{
    if (!doc || !out) return;
    fprintf(out, "app \"%s\" %dx%d bg=#%06X",
            doc->title ? doc->title : "-", doc->width, doc->height,
            doc->bg == CG_COLOR_NONE ? 0 : doc->bg);
    if (doc->has_pos) fprintf(out, " pos=(%d,%d)", doc->x, doc->y);
    if (doc->center) fputs(" centered", out);
    if (doc->border && strcmp(doc->border, "sizable")) fprintf(out, " border=%s", doc->border);
    if (!doc->control_box) fputs(" no-controlbox", out);
    if (!doc->min_box) fputs(" no-minbox", out);
    if (!doc->max_box) fputs(" no-maxbox", out);
    if (doc->topmost) fputs(" topmost", out);
    if (!doc->taskbar) fputs(" no-taskbar", out);
    if (!doc->visible) fputs(" hidden", out);
    if (!doc->enabled) fputs(" disabled", out);
    if (doc->enter_tab) fputs(" enter-tab", out);
    if (!doc->esc_close) fputs(" no-esc-close", out);
    if (doc->tray) fputs(" tray", out);
    if (doc->icon) fprintf(out, " icon=%s", doc->icon);
    if (doc->cursor) fprintf(out, " cursor=%s", doc->cursor);
    if (doc->class_name) fprintf(out, " class=%s", doc->class_name);
    if (doc->info.product) fprintf(out, " product=\"%s\"", doc->info.product);
    if (doc->info.version) fprintf(out, " version=%s", doc->info.version);
    fputc('\n', out);
    /* 动作表：菜单项的 action 引用只在行表里（dump 不展开 menus），
     * 所以这里把动作本身列全，排查"名字拼错"时一眼能对上。 */
    if (doc->nactions) {
        fprintf(out, "actions %lu:\n", (unsigned long)doc->nactions);
        for (size_t i = 0; i < doc->nactions; i++) {
            const cg_action_t *a = &doc->actions[i];
            fprintf(out, "  %s text=\"%s\" handler=%s", a->name ? a->name : "-",
                    a->text ? a->text : "", a->handler ? a->handler : "-");
            if (a->accel) fprintf(out, " accel=%s", a->accel);
            if (a->checked) fputs(" checked", out);
            if (!a->enabled) fputs(" disabled", out);
            fputc('\n', out);
        }
    }
    if (doc->root) {
        for (size_t i = 0; i < doc->root->nchildren; i++) dump_node(doc->root->children[i], out, 1);
    }
}

/* ============================================================ include 展开（F3）
 * 语义与取舍见 .codebuddy/plans/f3-window-ref-prd.md（D1/D2/D3）。要点：
 *   相对路径按**主文件所在目录**解析；一个文件在一次加载里最多展开一次（第二次起跳过 +
 *   WARN，循环引用因此不会死循环）；控件 id 撞了 = **报错**（带来源文件）；三张表同名 =
 *   主文档优先 + WARN（与失效引用的宽容口径一致）。
 */

typedef struct {
    char **items;
    size_t n;
    size_t cap;
} cg_str_list;

/**
 * @brief 路径表里是否已经有这个路径（大小写不敏感，Windows 口径）
 * @param l 已收集的路径表
 * @param s 要查的路径
 * @return int 1 = 已在表里；0 = 没有
 */
static int str_list_has(const cg_str_list *l, const char *s)
{
    for (size_t i = 0; i < l->n; i++)
        if (_stricmp(l->items[i], s) == 0) return 1;
    return 0;
}

/**
 * @brief 把路径加进表（已在表里就不重复加）
 * @param l 路径表
 * @param s 要加的路径（内部复制一份）
 * @return int 1 = 成功（含"本来就有"）；0 = 内存不足
 */
static int str_list_add(cg_str_list *l, const char *s)
{
    if (str_list_has(l, s)) return 1;
    if (l->n == l->cap) {
        size_t  cap   = l->cap ? l->cap * 2 : 8;
        char ** items = (char **)realloc(l->items, cap * sizeof(char *));
        if (!items) return 0;
        l->items = items;
        l->cap   = cap;
    }
    l->items[l->n] = dup_str(s);
    if (!l->items[l->n]) return 0;
    l->n++;
    return 1;
}

/**
 * @brief 释放路径表
 * @param l 路径表
 * @return void 无返回值
 */
static void str_list_free(cg_str_list *l)
{
    for (size_t i = 0; i < l->n; i++) free(l->items[i]);
    free(l->items);
}

/* 把 "a\.\b\..\c" 收敛成规范形（\ 分隔、消掉 . 与 ..；栈空时的 .. 保留——
 * 相对路径开头的 .. 有语义）。include 的去重（防循环）靠路径**字符串相等**判定，
 * 不规范化的话 ".\frag\..\inctest.cgui" 与 ".\inctest.cgui" 是同一个文件却永不相等，
 * 循环引用就会一路叠 ".." 直到超出 MAX_PATH（2026-10-10 实测抓到）。 */
/**
 * @brief 把路径收敛成规范形（\\ 分隔、消掉 . 与 ..；栈空时的 .. 保留）
 * @param p 原始路径
 * @return char * 规范形（堆串，用完 free）；失败返回 NULL
 */
static char *path_normalize(const char *p)
{
    const char *segs[128];
    size_t      lens[128];
    size_t      nseg = 0;
    size_t      i = 0, n = strlen(p), out = 0;
    char        buf[2048];

    while (i < n && nseg < 128) {
        size_t start, len;
        while (i < n && (p[i] == '\\' || p[i] == '/')) i++;
        if (i >= n) break;
        start = i;
        while (i < n && p[i] != '\\' && p[i] != '/') i++;
        len = i - start;
        if (len == 1 && p[start] == '.') continue;                       /* "." 丢弃 */
        if (len == 2 && p[start] == '.' && p[start + 1] == '.') {
            if (nseg > 0 && !(lens[nseg - 1] == 2 && segs[nseg - 1][0] == '.')) {
                nseg--;                                                   /* ".." 弹栈 */
                continue;
            }
            /* 栈空（或顶上还是 ..）：开头的 .. 有语义，保留 */
        }
        segs[nseg] = p + start;
        lens[nseg] = len;
        nseg++;
    }
    for (size_t k = 0; k < nseg && out + lens[k] + 2 < sizeof(buf); k++) {
        if (k) buf[out++] = '\\';
        memcpy(buf + out, segs[k], lens[k]);
        out += lens[k];
    }
    buf[out] = '\0';
    return dup_str(buf);
}

/* 相对路径 -> 相对 base_dir（规范化后返回）；已经是绝对路径（盘符 / \\ 或 / 开头）就只做规范化 */
/**
 * @brief 解析一条 include 的 path（相对 base_dir），并规范化
 * @param base_dir 基准目录（主 .cgui / 上级被引用文件所在目录）
 * @param rel include 里写的 path（相对或绝对）
 * @return char * 规范化后的路径（堆串，用完 free）；path 为空返回 NULL
 */
static char *include_path_resolve(const char *base_dir, const char *rel)
{
    char buf[1024];

    if (!rel || !*rel) return NULL;
    if (rel[1] == ':' || rel[0] == '\\' || rel[0] == '/') return path_normalize(rel);
    snprintf(buf, sizeof(buf), "%s\\%s", base_dir && *base_dir ? base_dir : ".", rel);
    return path_normalize(buf);
}

/**
 * @brief 取一个文件路径的目录部分（最后一个 \\ 或 / 之前）
 * @param file 文件路径
 * @param out 输出缓冲
 * @param outsz 缓冲容量
 * @return void 无返回值（没有分隔符时写 "."）
 */
static void include_dir_of(const char *file, char *out, size_t outsz)
{
    const char *slash  = strrchr(file, '\\');
    const char *slash2 = strrchr(file, '/');
    size_t      n;

    if (slash2 > slash) slash = slash2;
    if (!slash) { snprintf(out, outsz, "."); return; }
    n = (size_t)(slash - file);
    if (n >= outsz) n = outsz - 1;
    memcpy(out, file, n);
    out[n] = '\0';
}

/**
 * @brief 子树里有没有某个 id
 * @param n 子树根
 * @param id 要查的控件 id
 * @return int 1 = 有；0 = 没有
 */
static int id_exists_in(const cg_node_t *n, const char *id)
{
    if (!n) return 0;
    if (n->id && strcmp(n->id, id) == 0) return 1;
    for (size_t i = 0; i < n->nchildren; i++)
        if (id_exists_in(n->children[i], id)) return 1;
    return 0;
}

/* 引用文件的控件 id 与主文档对一遍：撞了就报错（带来源文件）。
 * 逐个节点对全树查一次是 O(n*m)，但文档就几百个控件、只在加载期跑一次，不值当优化。 */
/**
 * @brief 引用文件要搬进来的节点，id 与主文档撞没撞
 * @param host 主文档
 * @param incoming 要搬进来的节点（连同子树）
 * @param file 来源文件（写进错误信息，便于定位）
 * @param err 冲突时写错误信息
 * @param errsz err 容量
 * @return int 1 = 有冲突（err 已填，加载要失败）；0 = 没有
 */
static int ids_conflict(const cg_doc_t *host, const cg_node_t *incoming,
                        const char *file, char *err, size_t errsz)
{
    if (!incoming) return 0;
    if (incoming->id && id_exists_in(host->root, incoming->id)) {
        snprintf(err, errsz,
                 "include 冲突：id「%s」（来自 %s）在主文档里已经存在。"
                 "id 是事件绑定与 cg_find 的钥匙，不能静默改名 —— 请改掉其中一边。",
                 incoming->id, file);
        return 1;
    }
    for (size_t i = 0; i < incoming->nchildren; i++)
        if (ids_conflict(host, incoming->children[i], file, err, errsz)) return 1;
    return 0;
}

/**
 * @brief 深拷贝一份样式（三块堆串 font / fg_role / bg_role 都要复制）
 * @param dst 目标（调用方保证内存已清零或即将整体覆盖）
 * @param src 源
 * @return void 无返回值
 */
static void style_deep_copy(cg_style_t *dst, const cg_style_t *src)
{
    *dst = *src;
    dst->font    = src->font    ? dup_str(src->font)    : NULL;
    dst->fg_role = src->fg_role ? dup_str(src->fg_role) : NULL;
    dst->bg_role = src->bg_role ? dup_str(src->bg_role) : NULL;
}

/* 三张表合并：同名 = 主文档优先（引用文件的那份忽略 + WARN，与失效引用的宽容口径一致）。
 * 返回 0 = 成功；1 = 内存不足（err 已填）。
 * **每一条都要 realloc**：主文档自己的表是"按需正好装满"的，直接 &[n++] 是越界写
 * （2026-10-10 第一版就崩在这 —— 宿主自己没有 styles 时 host->styles 是 NULL）。 */
/**
 * @brief 三张表（styles / roles / actions）合并进主文档：同名 = 主文档优先 + WARN
 * @param host 主文档
 * @param sub 被引用文件解析出的文档
 * @param file 来源文件（写进 WARN，便于定位）
 * @param err 内存不足时写错误信息
 * @param errsz err 容量
 * @return int 0 = 成功；1 = 内存不足
 */
static int merge_tables(cg_doc_t *host, const cg_doc_t *sub, const char *file, char *err, size_t errsz)
{
    for (size_t i = 0; i < sub->nstyles; i++) {
        const cg_named_style_t *s = &sub->styles[i];
        cg_named_style_t *grown;
        cg_named_style_t *slot;

        if (style_find(host->styles, host->nstyles, s->name)) {
            cg_logm(CG_LOG_WARN, "doc", "include（%s）：命名样式 \"%s\" 主文档里已有，主文档优先", file, s->name);
            continue;
        }
        grown = (cg_named_style_t *)realloc(host->styles, (host->nstyles + 1) * sizeof(*grown));
        if (!grown) { snprintf(err, errsz, "out of memory"); return 1; }
        host->styles = grown;
        slot = &host->styles[host->nstyles++];
        slot->name = dup_str(s->name);
        style_deep_copy(&slot->st, &s->st);
    }
    for (size_t i = 0; i < sub->nroles; i++) {
        const cg_role_t *r = &sub->roles[i];
        cg_role_t *grown;
        cg_role_t *slot;

        if (role_find(host->roles, host->nroles, r->name)) {
            cg_logm(CG_LOG_WARN, "doc", "include（%s）：色彩角色 \"%s\" 主文档里已有，主文档优先", file, r->name);
            continue;
        }
        grown = (cg_role_t *)realloc(host->roles, (host->nroles + 1) * sizeof(*grown));
        if (!grown) { snprintf(err, errsz, "out of memory"); return 1; }
        host->roles = grown;
        slot = &host->roles[host->nroles++];
        slot->name      = dup_str(r->name);
        slot->light     = r->light;
        slot->dark      = r->dark;
        slot->has_light = r->has_light;
        slot->has_dark  = r->has_dark;
    }
    for (size_t i = 0; i < sub->nactions; i++) {
        const cg_action_t *a = &sub->actions[i];
        cg_action_t *grown;
        cg_action_t *slot;
        size_t j;

        if (!a->name || !*a->name) continue;
        for (j = 0; j < host->nactions; j++)
            if (host->actions[j].name && strcmp(host->actions[j].name, a->name) == 0) break;
        if (j < host->nactions) {
            cg_logm(CG_LOG_WARN, "doc", "include（%s）：动作 \"%s\" 主文档里已有，主文档优先", file, a->name);
            continue;
        }
        grown = (cg_action_t *)realloc(host->actions, (host->nactions + 1) * sizeof(*grown));
        if (!grown) { snprintf(err, errsz, "out of memory"); return 1; }
        host->actions = grown;
        slot = &host->actions[host->nactions++];
        slot->name    = dup_str(a->name);
        slot->text    = a->text    ? dup_str(a->text)    : NULL;
        slot->accel   = a->accel   ? dup_str(a->accel)   : NULL;
        slot->handler = a->handler ? dup_str(a->handler) : NULL;
        slot->checked = a->checked;
        slot->enabled = a->enabled;
    }
    return 0;
}

/**
 * @brief 把一个顶层节点（连同子树）挂到主文档的虚拟根下
 * @param host 主文档
 * @param n 节点（所有权转移：之后由主文档释放）
 * @return int 0 = 成功；1 = 内存不足
 */
static int host_append_node(cg_doc_t *host, cg_node_t *n)
{
    cg_node_t **items = (cg_node_t **)realloc(host->root->children,
                        (host->root->nchildren + 1) * sizeof(cg_node_t *));
    if (!items) return 1;
    host->root->children = items;
    host->root->children[host->root->nchildren++] = n;
    return 0;
}

/**
 * @brief 处理一条 include：读文件、解析、（递归展开它自己的 include）、并进 host
 * @param host 主文档
 * @param base_dir 这条相对路径的基准目录
 * @param path include 里写的 path
 * @param pick 只取那个文件里 id 为它的顶层节点；NULL = 全取
 * @param visited 已展开文件表（防循环 / 防重复）
 * @param err 失败时写错误信息
 * @param errsz err 容量
 * @return int 0 = 成功（含"文件已展开过，跳过"）；1 = 失败（err 已填）
 * @note 前向声明 —— 它与 expand_doc_into 互相递归，定义在下面。
 */
static int expand_entry_into(cg_doc_t *host, const char *base_dir, const char *path,
                             const char *pick, cg_str_list *visited, char *err, size_t errsz);

/* 把 sub 的内容并进 host：sub 自己的 include 先递归展开进 sub，然后挑节点、并三张表。
 * 被搬走的节点会从 sub->root 里摘掉（nchildren 收缩），之后 release(sub) 不会双 free。
 * pick 只作用于"从 sub 取哪些顶层节点"，不影响它内部引用的递归展开。 */
/**
 * @brief 把 sub 的内容并进 host：sub 自己的 include 先递归展开进 sub，然后挑节点、并三张表
 * @param host 主文档
 * @param sub 被引用文件解析出的文档
 * @param sub_dir sub 所在目录（它内部引用的相对路径按这里解析）
 * @param sub_file sub 的路径（写进错误 / WARN 信息）
 * @param pick 只取 sub 里 id 为它的顶层节点；NULL = 全取
 * @param visited 已展开文件表（防循环 / 防重复）
 * @param err 失败时写错误信息
 * @param errsz err 容量
 * @return int 0 = 成功；1 = 失败（id 冲突 / pick 没命中 / 内存不足，err 已填）
 * @note 被搬走的节点会从 sub->root 里摘掉（nchildren 收缩），之后 release(sub) 不会双 free。
 */
static int expand_doc_into(cg_doc_t *host, cg_doc_t *sub, const char *sub_dir,
                           const char *sub_file, const char *pick,
                           cg_str_list *visited, char *err, size_t errsz)
{
    int bad = 0;

    /* 子文件自己的 include 先展开进它自己（递归；visited 挡循环与重复）。
     * 注意 pick 只作用于"从子文件取哪些顶层节点"，不影响它内部引用的递归展开。 */
    for (size_t i = 0; i < sub->nincludes && !bad; i++) {
        bad = expand_entry_into(sub, sub_dir, sub->includes[i].path, sub->includes[i].pick,
                                visited, err, errsz);
    }
    if (bad) return 1;

    merge_tables(host, sub, sub_file, err, errsz);

    /* 挑节点：pick = 按 id 只取一个；不写 = 全取。先全部过 id 冲突关，过了才真正搬 ——
     * 这样冲突报错时一个节点都没搬，不用做半途回滚。 */
    {
        size_t keep = 0;
        size_t taken = 0;
        for (size_t i = 0; i < sub->root->nchildren; i++) {
            cg_node_t *c = sub->root->children[i];
            int take = pick ? (c->id && strcmp(c->id, pick) == 0) : 1;

            if (!take) { sub->root->children[keep++] = c; continue; }
            if (ids_conflict(host, c, sub_file, err, errsz)) { bad = 1; break; }
            if (host_append_node(host, c) != 0) {
                snprintf(err, errsz, "out of memory");
                bad = 1;
                break;
            }
            taken++;
            /* 引用进来的节点补一遍命名样式 / 角色合并：它的 style_name 与 @引用
             * 此前是对着**子文件**的表解析的，现在要用主文档（已并进子文件的表）的口径 */
            resolve_named_styles(c, host);
        }
        sub->root->nchildren = keep;   /* 搬走的不再留在 sub 里，release 不会双 free */
        /* pick 写了却没命中 = 静默丢：引用的界面整块消失还一声不吭 —— 必须报错 */
        if (!bad && pick && taken == 0) {
            snprintf(err, errsz, "include：%s 里没有 id 为「%s」的顶层节点（pick 没命中，"
                                 "不静默丢）", sub_file, pick);
            bad = 1;
        }
    }
    return bad ? 1 : 0;
}

/**
 * @brief 处理一条 include：读文件、解析、（递归展开它自己的 include）、并进 host
 * @param host 主文档
 * @param base_dir 这条相对路径的基准目录
 * @param path include 里写的 path
 * @param pick 只取那个文件里 id 为它的顶层节点；NULL = 全取
 * @param visited 已展开文件表（防循环 / 防重复）
 * @param err 失败时写错误信息
 * @param errsz err 容量
 * @return int 0 = 成功（含"文件已展开过，跳过"）；1 = 失败（err 已填）
 */
static int expand_entry_into(cg_doc_t *host, const char *base_dir, const char *path,
                             const char *pick, cg_str_list *visited, char *err, size_t errsz)
{
    char  *file = include_path_resolve(base_dir, path);
    char  *json;
    cg_doc_t *doc;
    cg_doc_t parsed;
    char dir[1024];
    int  rc;

    if (!file) { snprintf(err, errsz, "include 的 path 是空的"); return 1; }
    if (str_list_has(visited, file)) {
        cg_logm(CG_LOG_WARN, "doc", "include（%s）已经在本次加载里展开过，跳过（防循环 / 防重复）", file);
        free(file);
        return 0;
    }
    if (!str_list_add(visited, file)) {
        free(file);
        snprintf(err, errsz, "out of memory");
        return 1;
    }

    json = cg_read_file(file);
    if (!json) {
        snprintf(err, errsz, "include 引用的文件打不开：\n    %s", file);
        free(file);
        return 1;
    }
    doc = cg_doc_parse(json, err, errsz);
    free(json);
    if (!doc) { free(file); return 1; }    /* err 已由解析器填好 */
    parsed = *doc;
    free(doc);                              /* 文档所有权转移到栈上 */

    include_dir_of(file, dir, sizeof(dir));
    rc = expand_doc_into(host, &parsed, dir, file, pick, visited, err, errsz);
    cg_doc_release(&parsed);                /* 搬走的已摘除，剩下的照常释放 */
    free(file);
    return rc;
}

/**
 * @brief 展开 doc->includes（实现见 cg_doc.h 的语义说明）
 * @param doc 主文档
 * @param main_file 主 .cgui 的路径
 * @param err 失败时写错误信息
 * @param errsz err 容量
 * @return int 0 = 成功；1 = 加载失败
 */
int cg_doc_expand_includes(cg_doc_t *doc, const char *main_file, char *err, size_t errsz)
{
    cg_str_list visited = { NULL, 0, 0 };
    char base_dir[1024];
    int  rc = 0;

    if (!doc || doc->nincludes == 0) return 0;
    if (!main_file || !*main_file) {
        snprintf(err, errsz,
                 "这份 .cgui 写了 include，但本次加载没有主文件路径（用 cgui_text 内嵌界面时"
                 "解析不了相对路径）—— 请改用 cgui_path 指定界面文件，或先在编译期展开。");
        return 1;
    }
    /* 主文档自己进"已展开"名单：片段再引用主文档时直接跳过（防循环） */
    if (!str_list_add(&visited, main_file)) {
        snprintf(err, errsz, "out of memory");
        return 1;
    }
    include_dir_of(main_file, base_dir, sizeof(base_dir));
    for (size_t i = 0; i < doc->nincludes && rc == 0; i++) {
        rc = expand_entry_into(doc, base_dir, doc->includes[i].path, doc->includes[i].pick,
                               &visited, err, errsz);
    }
    /* include 表消费完毕置空：留着只会让人以为"再展开一次"还有意义 */
    for (size_t i = 0; i < doc->nincludes; i++) {
        free(doc->includes[i].path);
        free(doc->includes[i].pick);
    }
    free(doc->includes);
    doc->includes  = NULL;
    doc->nincludes = 0;
    str_list_free(&visited);
    return rc;
}
