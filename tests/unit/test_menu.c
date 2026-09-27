/**
 * @file    test_menu.c
 * @brief   运行时菜单增删改（cg_widget_list.c 的 cg_menu_* 一带）的单元测试
 * @details 为什么盯它：菜单行表是**扁平数组 + parent 存下标**，所以插入/删除会把后面
 *          所有行挪位。忘了修正 parent 的症状是"加了一项，**另一个下拉**里的项跑到
 *          顶层去了" —— 界面还画得出来、也不崩，只是悄悄错位，人眼很难发现。
 *          所以这里把索引平移钉死：在子菜单中间插/删之后，必须逐项核对
 *          "谁挂在谁下面、顺序对不对"，而不只是看条数对不对。
 *
 *          另一类要钉的是**失败路径**：父项不存在 / 往非下拉项里加 / id 找不到，
 *          都必须返回失败**且界面一个字节都不变**（"加一半"比"没加"更难查）。
 *
 *          白盒：直接 include cg_internal.h 读行表，不需要真的开窗。
 *          （真要验"菜单栏上点得到"，那是 serial_verify 第 3 级的真机活。）
 */
#include "cg_test.h"
#include "cg_internal.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* 行表布局（深度优先摊平，父项下标一定比自己小）：
 *   menu  : 0 文件 | 1 新建 | 2 打开 | 3 - | 4 最近 | 5 a.txt | 6 退出
 *           7 帮助 | 8 关于
 *   所以"文件"的直接子项 = 新建/打开/-/最近/退出（分散在 1,2,3,4,6 —— 中间隔着
 *   最近 自己的子树 a.txt），这正是最容易把索引算法写错的那种形状。 */
static const char *k_doc =
    "{\"widgets\":["
    "{\"id\":\"mnu\",\"type\":\"menu\",\"nodes\":["
    "  {\"text\":\"文件\",\"id\":\"m_file\",\"children\":["
    "    {\"text\":\"新建\",\"id\":\"m_new\"},"
    "    {\"text\":\"打开\",\"id\":\"m_open\"},"
    "    {\"text\":\"-\"},"
    "    {\"text\":\"最近\",\"id\":\"m_recent\",\"children\":[{\"text\":\"a.txt\",\"id\":\"m_r1\"}]},"
    "    {\"text\":\"退出\",\"id\":\"m_quit\"}]},"
    "  {\"text\":\"帮助\",\"id\":\"m_help\",\"children\":[{\"text\":\"关于\",\"id\":\"m_about\"}]}]},"
    "{\"id\":\"mbtn\",\"type\":\"menubutton\",\"text\":\"更多\",\"nodes\":[{\"text\":\"甲\",\"id\":\"mb_1\"}]},"
    "{\"id\":\"btn\",\"type\":\"button\",\"text\":\"普通按钮\"}"
    "]}";

/** @brief 解析文档 + 建控件树（与 test_action.c 同一套夹具口径） */
/**
 * @brief 解析文档 + 建控件树（与动作测试同一套夹具口径）
 * @param app 待初始化的应用
 * @return int 成功返回 0；解析失败返回 -1
 */
static int build_app(cg_app_t *app)
{
    char      err[256] = { 0 };
    cg_doc_t *doc = cg_doc_parse(k_doc, err, sizeof(err));

    if (!doc) {
        printf("        解析失败：%s\n", err);
        return -1;
    }
    memset(app, 0, sizeof(*app));
    app->scale = 1;
    app->doc = *doc;      /* app->doc 是内嵌结构，所有权转移 */
    free(doc);
    cg_build_widgets(app);
    cg_action_build(app);
    return 0;
}

/** @brief 收尾：控件自持字符串，要和文档一起收 */
/**
 * @brief 收尾：控件自持字符串，要和文档一起收
 * @param app 要释放的应用
 * @return void 无返回值
 */
static void free_app(cg_app_t *app)
{
    cg_widget_t *w = app->widgets;

    cg_action_release(app);
    while (w) {
        cg_widget_t *next = w->next;
        cg_widget_free(w);
        w = next;
    }
    app->widgets = NULL;
    cg_doc_release(&app->doc);
}

/** @brief 找某一行的下标（按文字），找不到 -1 —— 断言里读起来比裸下标清楚 */
/**
 * @brief 找某一行的下标（按文字），找不到返回 -1 —— 断言里读起来比裸下标清楚
 * @param w 菜单控件
 * @param text 要找的行文字
 * @return int 命中的行下标；找不到返回 -1
 */
static int row_of(const cg_widget_t *w, const char *text)
{
    for (int i = 0; i < w->nmenu; i++) {
        if (w->menu[i].text && strcmp(w->menu[i].text, text) == 0) return i;
    }
    return -1;
}

/** @brief 某一项挂在谁下面（按文字找父项，回它的文字；顶层回 "(顶层)"） */
/**
 * @brief 某一项挂在谁下面（按文字找父项，回它的文字；顶层回 "(顶层)"）
 * @param w 菜单控件
 * @param text 要找的行文字
 * @return const char* 父项的文字；顶层返回 "(顶层)"，没有这一项返回 "(没有这一项)"
 */
static const char *parent_text_of(const cg_widget_t *w, const char *text)
{
    int i = row_of(w, text);
    int p = (i >= 0) ? w->menu[i].parent : -2;
    if (p == -2) return "(没有这一项)";
    if (p == -1) return "(顶层)";
    return w->menu[p].text ? w->menu[p].text : "(?)";
}

/* ------------------------------------------------------------ 增 */

/** @brief 追加 / 中间插入 / 顶层前插，以及**索引平移** */
/**
 * @brief 追加 / 中间插入 / 顶层前插，以及**索引平移**
 * @return void 无返回值
 */
static void test_menu_add(void)
{
    cg_app_t     app;
    cg_widget_t *m, *mb;
    int          at;

    cg_test_note("---- 加菜单项 + 索引平移 ----");
    if (build_app(&app) != 0) return;
    m = cg_find(&app, "mnu");
    cg_test_ok("菜单控件在", m != NULL);
    if (!m) { free_app(&app); return; }
    cg_test_int("初始 9 行", m->nmenu, 9);
    cg_test_int("可点项 5 个（新建/打开/a.txt/退出/关于；下拉与分隔线不算）",
                cg_menu_count(m), 5);

    /* ① 追加到"文件"末尾：它的子树是行 0~6，所以新项落在 7 */
    at = cg_menu_add_item(m, "m_file", -1, "打印", "m_print");
    cg_test_int("追加返回新行下标 7", at, 7);
    cg_test_int("行数 +1", m->nmenu, 10);
    cg_test_str("  -> 文字", m->menu[7].text, "打印");
    cg_test_str("  -> 挂在「文件」下", parent_text_of(m, "打印"), "文件");
    cg_test_ok("  -> 有自己的命令 id", m->menu[7].cmd != 0);
    cg_test_int("  -> 可点项 +1", cg_menu_count(m), 6);
    /* 这一条是重点：文件子树被顶下去之后，后面几行的 parent 必须跟着挪 */
    cg_test_str("「帮助」还是顶层", parent_text_of(m, "帮助"), "(顶层)");
    cg_test_str("「关于」仍挂在「帮助」下（parent 下标修对了）",
                parent_text_of(m, "关于"), "帮助");

    /* ② 插到"文件"的第 1 个直接子项之前（即"打开"之前）。
     * 注意要在插入**之前**记下「打开」的位置 —— 插完它自己就后移了一位。 */
    {
        int open_at = row_of(m, "打开");
        at = cg_menu_add_item(m, "m_file", 1, "插入项", "m_ins");
        cg_test_int("插到「打开」原来的位置上", at, open_at);
        cg_test_str("  -> 文字", m->menu[at].text, "插入项");
        cg_test_str("  -> 挂在「文件」下", parent_text_of(m, "插入项"), "文件");
        cg_test_int("  -> 「打开」被顶后一位", row_of(m, "打开"), open_at + 1);
        cg_test_ok("  -> 「新建」仍在「打开」之前（顺序没乱）",
                   row_of(m, "新建") < row_of(m, "打开"));
    }

    /* ③ 顶层前插：所有 parent >= 0 的行都要 +1 */
    at = cg_menu_add_item(m, NULL, 0, "顶层新项", "m_top");
    cg_test_int("插到最前", at, 0);
    cg_test_str("「文件」被顶到第 1 行", m->menu[1].text, "文件");
    cg_test_str("  -> 「文件」还是顶层",
                m->menu[1].parent == -1 ? "(顶层)" : "?", "(顶层)");
    cg_test_str("「新建」仍挂在「文件」下", parent_text_of(m, "新建"), "文件");
    cg_test_str("「关于」仍挂在「帮助」下", parent_text_of(m, "关于"), "帮助");

    /* ④ 分隔线：没有命令 id，也不算可点项 */
    at = cg_menu_add_item(m, "m_file", -1, "-", NULL);
    cg_test_ok("分隔线加成功", at >= 0);
    cg_test_int("  -> 没有命令 id", m->menu[at].cmd, 0);
    cg_test_int("  -> 不算可点项（还是 8 个）", cg_menu_count(m), 8);
    /* 追加必须落在**文件子树内**（紧跟其后的是「帮助」）。若落到表尾，这里会读到 NULL。 */
    cg_test_str("  -> 后面紧跟「帮助」，说明插在子树末尾而不是表尾",
                m->menu[at + 1].text, "帮助");

    cg_test_note("---- 失败路径：必须失败，且界面一个字节都不变 ----");
    {
        int before = m->nmenu;
        cg_test_int("父项不存在 -> -1", cg_menu_add_item(m, "没有这个下拉", -1, "x", "m_x"), -1);
        cg_test_int("  -> 行数不变", m->nmenu, before);
        cg_test_int("往非下拉项里加 -> -1", cg_menu_add_item(m, "m_new", -1, "x", "m_x"), -1);
        cg_test_int("  -> 行数不变", m->nmenu, before);
        cg_test_int("往按钮里加 -> -1", cg_menu_add_item(cg_find(&app, "btn"), NULL, -1, "x", "m_x"), -1);
        cg_test_int("id 可以留空（只是个不可寻址的项）",
                    cg_menu_add_item(m, "m_file", -1, "没有 id 的项", NULL) >= 0, 1);
    }

    cg_test_note("---- menubutton 走同一套行表（重建是空操作，但行表要改对） ----");
    mb = cg_find(&app, "mbtn");
    cg_test_ok("菜单按钮在", mb != NULL);
    if (mb) {
        at = cg_menu_add_item(mb, NULL, -1, "乙", "mb_2");
        cg_test_int("加成功", at, 1);
        cg_test_int("  -> 行数 2", mb->nmenu, 2);
        cg_test_str("  -> 文字", mb->menu[1].text, "乙");
        cg_test_int("  -> 是顶层", mb->menu[1].parent, -1);
    }

    free_app(&app);
}

/* ------------------------------------------------------------ 删与清空 */

/** @brief 删一项（连带子树）/ 清空某个下拉 / 清空整个菜单 */
/**
 * @brief 删一项（连带子树）/ 清空某个下拉 / 清空整个菜单
 * @return void 无返回值
 */
static void test_menu_remove_clear(void)
{
    cg_app_t     app;
    cg_widget_t *m;
    int          n;

    cg_test_note("---- 删一项：连带整棵子树，并修正后面所有 parent ----");
    if (build_app(&app) != 0) return;
    m = cg_find(&app, "mnu");
    if (!m) { free_app(&app); return; }

    /* 「文件」子树 = 文件 + 新建/打开/-/最近/a.txt/退出 = 7 行 */
    n = cg_menu_remove_item(m, "m_file");
    cg_test_int("删掉 7 行（含子树）", n, 7);
    cg_test_int("剩 2 行", m->nmenu, 2);
    cg_test_str("剩下第 0 行是「帮助」", m->menu[0].text, "帮助");
    cg_test_str("第 1 行是「关于」", m->menu[1].text, "关于");
    cg_test_str("  -> 它仍挂在「帮助」下（parent 下标修对了）",
                parent_text_of(m, "关于"), "帮助");
    cg_test_int("找不到的 id -> -1", cg_menu_remove_item(m, "没有这一项"), -1);
    cg_test_int("空 id -> -1", cg_menu_remove_item(m, ""), -1);
    cg_test_int("删叶子：关于", cg_menu_remove_item(m, "m_about"), 1);
    cg_test_int("再删同一项 -> -1", cg_menu_remove_item(m, "m_about"), -1);
    free_app(&app);

    cg_test_note("---- 清空某个下拉：下拉项自身保留 ----");
    if (build_app(&app) != 0) return;
    m = cg_find(&app, "mnu");
    if (!m) { free_app(&app); return; }

    n = cg_menu_clear_items(m, "m_recent");
    cg_test_int("清掉「最近」下的 1 项", n, 1);
    cg_test_str("「最近」自己还在", m->menu[row_of(m, "最近")].text, "最近");
    cg_test_ok("  -> 它仍是个下拉", m->menu[row_of(m, "最近")].popup != 0);
    cg_test_str("a.txt 没了", row_of(m, "a.txt") >= 0 ? "还在" : "没了", "没了");
    cg_test_str("文件子树其它项没被牵连", parent_text_of(m, "退出"), "文件");
    cg_test_str("「关于」仍挂在「帮助」下", parent_text_of(m, "关于"), "帮助");

    n = cg_menu_clear_items(m, "m_file");
    cg_test_int("清空「文件」的 5 个直接子项（新建/打开/-/最近/退出）", n, 5);
    cg_test_str("「文件」还在（是个空下拉）", m->menu[row_of(m, "文件")].text, "文件");
    cg_test_str("  -> 此时整根菜单只剩它和「帮助」两根下拉",
                m->nmenu == 3 ? "对" : "不对", "对");
    cg_test_int("失败路径：下拉不存在 -> -1", cg_menu_clear_items(m, "没有这个下拉"), -1);
    cg_test_int("失败路径：往非下拉项清 -> -1", cg_menu_clear_items(m, "m_about"), -1);
    free_app(&app);

    cg_test_note("---- 清空整个菜单（parent_id 传 NULL） ----");
    if (build_app(&app) != 0) return;
    m = cg_find(&app, "mnu");
    if (!m) { free_app(&app); return; }
    n = cg_menu_clear_items(m, NULL);
    cg_test_int("顶层 2 项 + 各自子树 = 9 行", n, 9);
    cg_test_int("行表空了", m->nmenu, 0);
    cg_test_int("可点项 0", cg_menu_count(m), 0);
    cg_test_int("再清一次 = 0（不是失败）", cg_menu_clear_items(m, NULL), 0);
    free_app(&app);
}

/* ------------------------------------------------------------ 改状态与文字 */

/** @brief 启用 / 勾选 / 文字；动作项只读；找不到 id 必须不动界面 */
/**
 * @brief 启用 / 勾选 / 文字；动作项只读；找不到 id 必须不动界面
 * @return void 无返回值
 */
static void test_menu_state(void)
{
    cg_app_t     app;
    cg_widget_t *m;
    int          i;

    cg_test_note("---- 启用 / 勾选 / 文字 ----");
    if (build_app(&app) != 0) return;
    m = cg_find(&app, "mnu");
    if (!m) { free_app(&app); return; }

    i = row_of(m, "新建");
    cg_test_int("默认可用", m->menu[i].enabled, 1);
    cg_menu_set_item_enabled(m, "m_new", 0);
    cg_test_int("灰掉之后 enabled=0", m->menu[i].enabled, 0);
    cg_menu_set_item_enabled(m, "m_new", 1);
    cg_test_int("再启用回来", m->menu[i].enabled, 1);

    cg_test_int("默认没勾选", m->menu[i].checked, 0);
    cg_menu_set_item_checked(m, "m_new", 1);
    cg_test_int("打勾", m->menu[i].checked, 1);

    cg_menu_set_item_text(m, "m_new", "新文件");
    cg_test_str("改文字", m->menu[i].text, "新文件");
    cg_test_ok("  -> 命令 id 不受影响（点击还认得它）", m->menu[i].cmd != 0);

    cg_test_note("---- 不改的：失败路径与不允许的写法 ----");
    {
        int before_n = m->nmenu;
        const char *before_t = m->menu[i].text;
        int before_e = m->menu[i].enabled;

        cg_menu_set_item_enabled(m, "没有这一项", 0);
        cg_menu_set_item_checked(m, "", 1);
        cg_menu_set_item_text(m, "没有这一项", "x");
        cg_test_int("找不到的 id：行数不变", m->nmenu, before_n);
        cg_test_str("  -> 文字不变", m->menu[i].text, before_t);
        cg_test_int("  -> 启用态不变", m->menu[i].enabled, before_e);

        cg_menu_set_item_text(m, "m_new", "-");
        cg_test_str("不许把一项改成 \"-\"（那是分隔线的记号）", m->menu[i].text, before_t);
        cg_test_ok("  -> 命令 id 还在，没变成分隔线", m->menu[i].cmd != 0);

        cg_menu_set_item_enabled(cg_find(&app, "btn"), "m_new", 0);
        cg_test_int("传到非菜单控件上是空操作", m->menu[i].enabled, before_e);
    }

    free_app(&app);

    cg_test_note("---- 动作项：文字/启用/勾选归动作管，这里不生效 ----");
    {
        cg_app_t app2;
        char     err[256] = { 0 };
        cg_doc_t *doc = cg_doc_parse(
            "{\"actions\":{\"save\":{\"text\":\"保存\",\"handler\":\"on_save\"}},"
            "\"widgets\":[{\"id\":\"m2\",\"type\":\"menu\",\"nodes\":["
            " {\"text\":\"文件\",\"id\":\"f\",\"children\":["
            "   {\"action\":\"save\",\"id\":\"m_save\"}]}]}]}",
            err, sizeof(err));

        cg_test_ok("解析带动作项的菜单", doc != NULL);
        if (doc) {
            cg_widget_t *m2;
            memset(&app2, 0, sizeof(app2));
            app2.scale = 1;
            app2.doc = *doc;
            free(doc);
            cg_build_widgets(&app2);
            cg_action_build(&app2);
            m2 = cg_find(&app2, "m2");
            cg_test_ok("菜单在", m2 != NULL);
            if (m2) {
                int k = row_of(m2, "保存");     /* 文字已由动作归一化 */
                cg_test_ok("动作的文字落到了行上", k >= 0);
                if (k >= 0) {
                    cg_menu_set_item_text(m2, "m_save", "我改的");
                    cg_test_str("动作项：改文字不生效（归动作管）", m2->menu[k].text, "保存");
                    cg_menu_set_item_enabled(m2, "m_save", 0);
                    cg_test_int("动作项：改启用也不生效", m2->menu[k].enabled, 1);
                }
            }
            free_app(&app2);
        }
    }
}

CG_TEST_SUITE("菜单：运行时加项（索引平移）", test_menu_add);
CG_TEST_SUITE("菜单：运行时删项与清空", test_menu_remove_clear);
CG_TEST_SUITE("菜单：启用/勾选/文字与只读项", test_menu_state);
