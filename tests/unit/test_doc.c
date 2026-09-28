/**
 * @file    test_doc.c
 * @brief   cg_doc.c 的单元测试：.cgui -> 文档树
 * @details 为什么盯它：`.cgui` 字段语义的"真相"在 C 侧，画布端 model.ts 跟着它走。
 *          这份测试就是那句真相的可执行版本 —— 以后改解析逻辑，这里的期望值会先响；
 *          反过来，画布改了字段名而 C 侧没跟上，check-format.bat 那边会响。
 *          覆盖：窗口属性 / 控件树 / 样式 / 状态边界修正 / 结构化内容 / 事件名 /
 *          以及"该报错的文档必须报错（并且给出原因）"。
 */
#include "cg_test.h"
#include "cg_doc.h"

#include <stdio.h>
#include <string.h>

/** 测试文档：顶层 2 个控件，其中一个是容器，容器里放了 5 种有代表性的控件 */
static const char *k_doc =
    "{\n"
    "  \"cgui\": 1,\n"
    "  \"app\": { \"title\": \"测试窗口\", \"width\": 500, \"height\": 400,"
    " \"bg\": \"#F3EDF7\", \"topmost\": true },\n"
    "  \"widgets\": [\n"
    "    { \"id\": \"lab_tip\", \"type\": \"label\",\n"
    "      \"rect\": { \"x\": 8, \"y\": 12, \"w\": 200, \"h\": 24 },\n"
    "      \"text\": \"提示文本\",\n"
    "      \"style\": { \"font\": \"Microsoft YaHei UI\", \"size\": 9, \"fg\": \"#49454F\",\n"
    "                 \"bold\": true, \"align\": \"center\", \"radius\": 6 } },\n"
    "    { \"id\": \"grp_main\", \"type\": \"group\",\n"
    "      \"rect\": { \"x\": 8, \"y\": 48, \"w\": 480, \"h\": 300 },\n"
    "      \"children\": [\n"
    "        { \"id\": \"edt_name\", \"type\": \"edit\",\n"
    "          \"rect\": { \"x\": 12, \"y\": 24, \"w\": 180, \"h\": 30 },\n"
    "          \"hint\": \"在这里输入\",\n"
    "          \"events\": { \"change\": \"on_name_change\", \"submit\": \"on_submit\" } },\n"
    "        { \"id\": \"spn_age\", \"type\": \"edit\", \"spin\": 1, \"input\": 7,\n"
    "          \"rect\": { \"x\": 12, \"y\": 64, \"w\": 90, \"h\": 28 },\n"
    "          \"min\": 1, \"max\": 120, \"value\": 24, \"enabled\": false },\n"
    "        { \"id\": \"lst_pick\", \"type\": \"listbox\",\n"
    "          \"rect\": { \"x\": 12, \"y\": 100, \"w\": 180, \"h\": 90 },\n"
    "          \"items\": [\"甲\", \"乙\", \"丙\"] },\n"
    "        { \"id\": \"tre_files\", \"type\": \"tree\",\n"
    "          \"rect\": { \"x\": 210, \"y\": 24, \"w\": 250, \"h\": 160 },\n"
    "          \"nodes\": [ { \"text\": \"根\", \"expanded\": true,\n"
    "                        \"children\": [ { \"text\": \"子\", \"id\": \"n_sub\" } ] } ] },\n"
    "        { \"id\": \"tbl_data\", \"type\": \"table\",\n"
    "          \"rect\": { \"x\": 210, \"y\": 190, \"w\": 250, \"h\": 90 },\n"
    "          \"multi\": true, \"check\": true,\n"
    "          \"columns\": [ { \"title\": \"姓名\", \"w\": 120 }, { \"title\": \"年龄\" } ],\n"
    "          \"rows\": [ [\"张三\", \"18\"], [\"李四\", \"20\"] ] }\n"
    "      ] }\n"
    "  ]\n"
    "}\n";

/**
 * @brief 在直接子节点里按 id 找一个控件
 * @param parent 父节点
 * @param id 控件 id
 * @return const cg_node_t* 找到的节点；没有返回 NULL
 */
static const cg_node_t *child_by_id(const cg_node_t *parent, const char *id)
{
    if (!parent) return NULL;
    for (size_t i = 0; i < parent->nchildren; i++) {
        const cg_node_t *c = parent->children[i];
        if (c->id && strcmp(c->id, id) == 0) return c;
    }
    return NULL;
}

/**
 * @brief 窗口属性与控件树的骨架
 * @return void 无返回值
 */
static void test_doc_skeleton(void)
{
    char      err[256];
    cg_doc_t *doc;

    cg_test_note("---- 窗口属性（app 节点） ----");
    err[0] = '\0';
    doc = cg_doc_parse(k_doc, err, sizeof(err));
    cg_test_ok("解析测试文档成功", doc != NULL);
    if (!doc) {
        printf("        解析失败：%s\n", err);
        return;
    }
    cg_test_str("标题", doc->title, "测试窗口");
    cg_test_int("宽", doc->width, 500);
    cg_test_int("高", doc->height, 400);
    cg_test_int("底色", (long long)doc->bg, 0xF3EDF7);
    cg_test_int("置顶标记", doc->topmost, 1);

    cg_test_note("---- 控件树骨架 ----");
    cg_test_int("顶层控件数", (long long)doc->root->nchildren, 2);
    cg_test_ok("虚拟根没有父节点", doc->root->parent == NULL);

    {
        const cg_node_t *grp = child_by_id(doc->root, "grp_main");
        cg_test_ok("能找到容器 grp_main", grp != NULL);
        if (grp) {
            cg_test_str("容器类型", grp->type, "group");
            cg_test_int("容器里的控件数", (long long)grp->nchildren, 5);
            cg_test_ok("子控件的 parent 指回容器",
                       grp->nchildren > 0 && grp->children[0]->parent == grp);
        }
    }

    cg_test_note("---- 单个控件的字段（坐标 / 文字 / 样式） ----");
    {
        const cg_node_t *lab = child_by_id(doc->root, "lab_tip");
        cg_test_ok("能找到 lab_tip", lab != NULL);
        if (lab) {
            cg_test_str("类型", lab->type, "label");
            cg_test_str("文字", lab->text, "提示文本");
            cg_test_int("x", lab->x, 8);
            cg_test_int("y", lab->y, 12);
            cg_test_int("w", lab->w, 200);
            cg_test_int("h", lab->h, 24);
            cg_test_str("字体", lab->style.font, "Microsoft YaHei UI");
            cg_test_int("字号", lab->style.size, 9);
            cg_test_int("前景色", (long long)lab->style.fg, 0x49454F);
            cg_test_int("加粗", lab->style.bold, 1);
            cg_test_int("圆角", lab->style.radius, 6);
            cg_test_int("对齐=居中", (long long)lab->style.align, (long long)CG_ALIGN_CENTER);
            cg_test_int("未设底色用「不设置」",
                        (long long)lab->style.bg, (long long)CG_COLOR_NONE);
        }
    }

    cg_test_note("---- 悬停提示 ----");
    {
        const cg_node_t *edt = child_by_id(child_by_id(doc->root, "grp_main"), "edt_name");
        cg_test_ok("能找到 edt_name", edt != NULL);
        if (edt) {
            cg_test_str("hint", edt->hint, "在这里输入");
            cg_test_int("容器内坐标相对容器（x=12）", edt->x, 12);
        }
    }

    cg_doc_free(doc);
}

/**
 * @brief events 对象 -> 按事件枚举下标存的回调名
 * @return void 无返回值
 */
static void test_doc_events(void)
{
    char      err[256];
    cg_doc_t *doc;

    cg_test_note("---- 事件名解析 ----");
    doc = cg_doc_parse(k_doc, err, sizeof(err));
    cg_test_ok("解析测试文档成功", doc != NULL);
    if (!doc) return;

    {
        const cg_node_t *edt = child_by_id(child_by_id(doc->root, "grp_main"), "edt_name");
        if (edt) {
            cg_test_str("change 绑的回调名", edt->ev[CG_EVENT_CHANGE], "on_name_change");
            cg_test_str("submit 绑的回调名", edt->ev[CG_EVENT_SUBMIT], "on_submit");
            cg_test_ok("没写的事件保持 NULL", edt->ev[CG_EVENT_CLICK] == NULL);
        }
    }
    cg_doc_free(doc);

    cg_test_note("---- 不认识的事件名要被忽略而不是崩 ----");
    doc = cg_doc_parse("{\"widgets\":[{\"id\":\"b\",\"type\":\"button\","
                       "\"events\":{\"没有的事件\":\"f\",\"click\":\"on_ok\"}}]}",
                       err, sizeof(err));
    cg_test_ok("解析成功", doc != NULL);
    if (doc) {
        const cg_node_t *b = child_by_id(doc->root, "b");
        cg_test_ok("能找到按钮", b != NULL);
        if (b) {
            cg_test_str("认识的事件照常绑上", b->ev[CG_EVENT_CLICK], "on_ok");
            cg_test_ok("不认识的事件没被绑到任何位置", b->ev[CG_EVENT_DBLCLICK] == NULL);
        }
        cg_doc_free(doc);
    }
}

/**
 * @brief 结构化内容：条目 / 节点 / 列 / 行
 * @return void 无返回值
 */
static void test_doc_structured(void)
{
    char      err[256];
    cg_doc_t *doc;
    const cg_node_t *grp;

    cg_test_note("---- 列表项 / 树节点 / 表格行列 ----");
    doc = cg_doc_parse(k_doc, err, sizeof(err));
    cg_test_ok("解析测试文档成功", doc != NULL);
    if (!doc) return;
    grp = child_by_id(doc->root, "grp_main");

    {
        const cg_node_t *lst = child_by_id(grp, "lst_pick");
        cg_test_ok("能找到 listbox", lst != NULL);
        if (lst) {
            cg_test_int("条目数", (long long)lst->nitems, 3);
            cg_test_str("第 1 项", lst->nitems > 0 ? lst->items[0] : NULL, "甲");
            cg_test_str("第 3 项", lst->nitems > 2 ? lst->items[2] : NULL, "丙");
        }
    }
    {
        const cg_node_t *tre = child_by_id(grp, "tre_files");
        cg_test_ok("能找到 tree", tre != NULL);
        if (tre) {
            cg_test_int("根节点数", (long long)tre->nnodes, 1);
            if (tre->nnodes > 0) {
                const cg_item_t *root_item = tre->nodes[0];
                cg_test_str("根节点文字", root_item->text, "根");
                cg_test_int("根节点初始展开", root_item->expanded, 1);
                cg_test_int("根节点子节点数", (long long)root_item->nchildren, 1);
                if (root_item->nchildren > 0) {
                    cg_test_str("子节点文字", root_item->children[0]->text, "子");
                    cg_test_str("子节点 id", root_item->children[0]->id, "n_sub");
                    cg_test_int("子节点默认不展开", root_item->children[0]->expanded, 0);
                }
            }
        }
    }
    {
        const cg_node_t *tbl = child_by_id(grp, "tbl_data");
        cg_test_ok("能找到 table", tbl != NULL);
        if (tbl) {
            cg_test_int("列数", (long long)tbl->ncols, 2);
            cg_test_int("行数", (long long)tbl->nrows, 2);
            cg_test_int("每行单元格数", (long long)tbl->row_cells, 2);
            cg_test_str("第 1 列标题", tbl->ncols > 0 ? tbl->cols[0].title : NULL, "姓名");
            cg_test_int("第 1 列宽", tbl->ncols > 0 ? tbl->cols[0].width : -1, 120);
            cg_test_int("没写宽度的列 = 0（均分）", tbl->ncols > 1 ? tbl->cols[1].width : -1, 0);
            cg_test_str("第 1 行第 1 格", (tbl->nrows > 0) ? tbl->rows[0][0] : NULL, "张三");
            cg_test_str("第 2 行第 2 格", (tbl->nrows > 1) ? tbl->rows[1][1] : NULL, "20");
            cg_test_int("允许多选", tbl->multi, 1);
            cg_test_int("显示勾选列", tbl->check, 1);
        }
    }

    cg_doc_free(doc);
}

/**
 * @brief 状态字段与"越界值修正"：这些自动修正决定了界面不会出现非法状态
 * @return void 无返回值
 */
static void test_doc_state_clamp(void)
{
    char      err[256];
    cg_doc_t *doc;
    const cg_node_t *grp;

    cg_test_note("---- 状态字段 ----");
    doc = cg_doc_parse(k_doc, err, sizeof(err));
    if (!doc) {
        cg_test_ok("解析测试文档成功", 0);
        return;
    }
    grp = child_by_id(doc->root, "grp_main");
    {
        const cg_node_t *spn = child_by_id(grp, "spn_age");
        if (spn) {
            cg_test_int("min", spn->vmin, 1);
            cg_test_int("max", spn->vmax, 120);
            cg_test_int("value", spn->value, 24);
            cg_test_int("enabled=false", spn->enabled, 0);
            cg_test_int("没写 visible 视为可见", spn->visible, 1);
        }
    }
    cg_doc_free(doc);

    cg_test_note("---- 越界值修正 ----");
    doc = cg_doc_parse("{\"app\":{\"width\":10,\"height\":10},"
                       "\"widgets\":[{\"id\":\"s\",\"type\":\"slider\",\"min\":50,\"max\":50},"
                       "{\"id\":\"l\",\"type\":\"label\",\"rect\":{\"w\":-10,\"h\":-20}}]}",
                       err, sizeof(err));
    cg_test_ok("解析成功", doc != NULL);
    if (doc) {
        cg_test_int("窗口宽被抬到下限 80", doc->width, 80);
        cg_test_int("窗口高被抬到下限 60", doc->height, 60);
        {
            const cg_node_t *s = child_by_id(doc->root, "s");
            if (s) cg_test_int("max<=min 时自动抬到 min+1", s->vmax, 51);
        }
        {
            const cg_node_t *l = child_by_id(doc->root, "l");
            if (l) {
                cg_test_int("负宽被清零", l->w, 0);
                cg_test_int("负高被清零", l->h, 0);
            }
        }
        cg_doc_free(doc);
    }
}

/**
 * @brief 能省的字段都省掉时的兜底行为（手写小 .cgui 会用到）
 * @return void 无返回值
 */
static void test_doc_defaults(void)
{
    char      err[256];
    cg_doc_t *doc;

    cg_test_note("---- 缺字段时的默认值 ----");
    doc = cg_doc_parse("{\"widgets\":[{\"type\":\"button\"},{\"id\":\"x\"}]}", err, sizeof(err));
    cg_test_ok("没有 app 节点也能解析", doc != NULL);
    if (!doc) return;

    cg_test_str("默认标题", doc->title, "CStackGUI Application");
    cg_test_int("默认宽", doc->width, 640);
    cg_test_int("默认高", doc->height, 420);
    cg_test_int("默认底色 = 不设置", (long long)doc->bg, (long long)CG_COLOR_NONE);
    cg_test_str("默认边框样式", doc->border, "sizable");
    cg_test_int("默认任务栏显示", doc->taskbar, 1);
    cg_test_int("默认 Esc 关闭", doc->esc_close, 1);
    cg_test_int("默认回车当 submit（不下移焦点）", doc->enter_tab, 0);
    cg_test_int("顶层控件数", (long long)doc->root->nchildren, 2);

    {
        const cg_node_t *btn = doc->root->nchildren > 0 ? doc->root->children[0] : NULL;
        if (btn) {
            cg_test_str("写了 type 就按它建", btn->type, "button");
            cg_test_ok("没写 id 时自动生成且非空", btn->id && btn->id[0] != '\0');
        }
        {
            const cg_node_t *x = child_by_id(doc->root, "x");
            cg_test_ok("能找到只有 id 的那个控件", x != NULL);
            if (x) {
                cg_test_str("没写 type 时按 label 处理", x->type, "label");
                cg_test_ok("尺寸用类型表的默认值", x->w > 0 && x->h > 0);
            }
        }
    }
    cg_doc_free(doc);
}

/**
 * @brief 坏文档必须报错，并且给出能定位的原因
 * @return void 无返回值
 */
static void test_doc_errors(void)
{
    char      err[256];
    cg_doc_t *doc;

    cg_test_note("---- 坏文档的错误信息 ----");
    err[0] = '\0';
    doc = cg_doc_parse("{ 不是 JSON }", err, sizeof(err));
    cg_test_ok("垃圾 JSON 被拒绝", doc == NULL);
    cg_test_ok("错误信息非空", err[0] != '\0');

    err[0] = '\0';
    doc = cg_doc_parse("[1,2,3]", err, sizeof(err));
    cg_test_ok("顶层数组被拒绝", doc == NULL);
    cg_test_ok("提示要求对象", strstr(err, "object") != NULL);

    err[0] = '\0';
    doc = cg_doc_parse("{\"cgui\":2}", err, sizeof(err));
    cg_test_ok("不认识的版本号被拒绝", doc == NULL);
    cg_test_ok("提示里带版本号", strstr(err, "version") != NULL);

    err[0] = '\0';
    doc = cg_doc_parse("{\"widgets\":{}}", err, sizeof(err));
    cg_test_ok("widgets 不是数组被拒绝", doc == NULL);
    cg_test_ok("提示 widgets 必须是数组", strstr(err, "widgets") != NULL);

    err[0] = '\0';
    doc = cg_doc_parse("{\"widgets\":[{\"type\":\"没有这个类型\"}]}", err, sizeof(err));
    cg_test_ok("不认识的控件类型被拒绝", doc == NULL);
    cg_test_ok("提示 unknown widget type", strstr(err, "unknown widget type") != NULL);

    err[0] = '\0';
    doc = cg_doc_parse("{\"widgets\":[{\"type\":\"label\",\"children\":[]}]}", err, sizeof(err));
    cg_test_ok("非容器控件带 children 被拒绝", doc == NULL);
    cg_test_ok("提示容器问题", strstr(err, "children") != NULL);

    err[0] = '\0';
    doc = cg_doc_parse("{}", err, sizeof(err));
    cg_test_ok("空对象算合法文档（0 个控件）", doc != NULL);
    if (doc) {
        cg_test_int("0 个控件", (long long)doc->root->nchildren, 0);
        cg_doc_free(doc);
    }
}

/**
 * @brief 文档打印之前只有自检走过，这里单独盯一下
 * @return void 无返回值
 */
static void test_doc_dump(void)
{
    char      err[256];
    cg_doc_t *doc;
    FILE     *f;
    long      size = 0;

    cg_test_note("---- 文档树打印 ----");
    doc = cg_doc_parse(k_doc, err, sizeof(err));
    cg_test_ok("解析测试文档成功", doc != NULL);
    if (!doc) return;

    f = fopen("cg_doc_dump.txt", "w");
    cg_test_ok("能建输出文件（当前目录是 build\\tests）", f != NULL);
    if (f) {
        cg_doc_dump(doc, f);
        size = ftell(f);
        fclose(f);
    }
    cg_test_ok("打印内容非空", size > 0);
    cg_doc_free(doc);
}

/**
 * @brief 老写法自动升级：多行编辑框 / 调节器已合并进 edit（2026-09-22）
 * @return void 无返回值
 * @note 这两类型名从类型表删掉之后，未知类型是**整份文档解析失败**，所以解析层必须留兼容映射。
 *       这里盯的就是它：映射后的字段要正好等于以前写死的那套（多行 + 纵向滚动 / 整数过滤 + 调节器）。
 *       用户手上那些老工程全靠这段代码还能打开，回归一次就是灾难
 */
static void test_doc_legacy_alias(void)
{
    static const char *legacy =
        "{\n"
        "  \"cgui\": 1,\n"
        "  \"app\": { \"title\": \"老写法\", \"width\": 400, \"height\": 300 },\n"
        "  \"widgets\": [\n"
        "    { \"id\": \"edt_body\", \"type\": \"multiedit\",\n"
        "      \"rect\": { \"x\": 10, \"y\": 10, \"w\": 300, \"h\": 120 } },\n"
        "    { \"id\": \"spn_age\", \"type\": \"spinner\",\n"
        "      \"rect\": { \"x\": 10, \"y\": 150, \"w\": 100, \"h\": 28 },\n"
        "      \"min\": 1, \"max\": 120, \"value\": 24 }\n"
        "  ]\n"
        "}\n";
    char      err[256];
    cg_doc_t *doc;

    cg_test_note("---- 老类型名（multiedit / spinner）自动升级成 edit ----");
    err[0] = '\0';
    doc = cg_doc_parse(legacy, err, sizeof(err));
    cg_test_ok("老写法不再报 unknown widget type", doc != NULL);
    if (!doc) {
        printf("        解析失败：%s\n", err);
        return;
    }
    {
        const cg_node_t *multi = child_by_id(doc->root, "edt_body");
        const cg_node_t *spin  = child_by_id(doc->root, "spn_age");

        cg_test_ok("multiedit 节点还在", multi != NULL);
        if (multi) {
            cg_test_str("multiedit 升级成 edit", multi->type, "edit");
            cg_test_int("multiedit -> 多行", multi->multiline, 1);
            cg_test_int("multiedit -> 纵向滚动", multi->scroll, 2);
        }
        cg_test_ok("spinner 节点还在", spin != NULL);
        if (spin) {
            cg_test_str("spinner 升级成 edit", spin->type, "edit");
            cg_test_int("spinner -> 自动调节器", spin->spin, 1);
            cg_test_int("spinner -> 整数输入方式", spin->input, 7);
            cg_test_int("范围原样保留（min）", spin->vmin, 1);
            cg_test_int("范围原样保留（max）", spin->vmax, 120);
            cg_test_int("值原样保留", spin->value, 24);
            cg_test_int("数值框不写 text（由 value 派生）", (long long)strlen(spin->text), 0);
        }
    }
    cg_doc_free(doc);
}

/**
 * @brief 串口组件：字段解析、缺省回退、越界修正
 * @return void 无返回值
 */
static void test_doc_serial(void)
{
    static const char *doc_text =
        "{\n"
        "  \"cgui\": 1,\n"
        "  \"widgets\": [\n"
        "    { \"id\": \"ser_a\", \"type\": \"serial\" },\n"
        "    { \"id\": \"ser_b\", \"type\": \"serial\", \"port\": \"COM4\",\n"
        "      \"baud\": 115200, \"bits\": 7, \"stop\": 2, \"parity\": 2, \"auto\": true }\n"
        "  ]\n"
        "}\n";
    char      err[256];
    cg_doc_t *doc;

    cg_test_note("---- 串口组件：什么都不写 = 全缺省 ----");
    err[0] = '\0';
    doc = cg_doc_parse(doc_text, err, sizeof(err));
    cg_test_ok("解析成功", doc != NULL);
    if (!doc) {
        printf("        解析失败：%s\n", err);
        return;
    }
    {
        const cg_node_t *a = child_by_id(doc->root, "ser_a");
        const cg_node_t *b = child_by_id(doc->root, "ser_b");

        cg_test_ok("缺省节点在", a != NULL);
        if (a) {
            cg_test_ok("端口没配（NULL）", a->sport == NULL);
            cg_test_int("波特率缺省 9600", a->s_baud, 9600);
            cg_test_int("数据位缺省 8", a->s_bits, 8);
            cg_test_int("停止位缺省 1", a->s_stop, 1);
            cg_test_int("校验缺省无", a->s_parity, 0);
            cg_test_int("不自动启动", a->s_auto, 0);
        }

        cg_test_note("---- 写全了：原样保留 ----");
        cg_test_ok("全参数节点在", b != NULL);
        if (b) {
            cg_test_str("端口名", b->sport, "COM4");
            cg_test_int("波特率 115200", b->s_baud, 115200);
            cg_test_int("数据位 7", b->s_bits, 7);
            cg_test_int("停止位 2", b->s_stop, 2);
            cg_test_int("校验偶", b->s_parity, 2);
            cg_test_int("自动启动", b->s_auto, 1);
        }
    }
    cg_doc_free(doc);

    cg_test_note("---- 越界值回退缺省，不造出怪参数 ----");
    doc = cg_doc_parse("{\"widgets\":[{\"id\":\"s\",\"type\":\"serial\","
                       "\"port\":\"\",\"baud\":-1,\"bits\":44,\"stop\":9,\"parity\":7}]}",
                       err, sizeof(err));
    cg_test_ok("解析成功", doc != NULL);
    if (doc) {
        const cg_node_t *s = child_by_id(doc->root, "s");

        if (s) {
            cg_test_ok("空端口名当没配", s->sport == NULL || s->sport[0] == '\0');
            cg_test_int("非法波特率回 9600", s->s_baud, 9600);
            cg_test_int("非法数据位回 8", s->s_bits, 8);
            cg_test_int("非法停止位回 1", s->s_stop, 1);
            cg_test_int("非法校验回无", s->s_parity, 0);
        }
    }
    cg_doc_free(doc);
}

CG_TEST_SUITE("文档：窗口属性与控件树", test_doc_skeleton);
CG_TEST_SUITE("文档：老类型名自动升级", test_doc_legacy_alias);
CG_TEST_SUITE("文档：事件名解析", test_doc_events);
CG_TEST_SUITE("文档：结构化内容", test_doc_structured);
CG_TEST_SUITE("文档：状态与越界修正", test_doc_state_clamp);
CG_TEST_SUITE("文档：缺省值兜底", test_doc_defaults);
CG_TEST_SUITE("文档：坏文档的错误信息", test_doc_errors);
CG_TEST_SUITE("文档：串口组件字段", test_doc_serial);
CG_TEST_SUITE("文档：树打印", test_doc_dump);
