/**
 * @file    l4.c
 * @brief   lesson04_data —— 套件模块（原 examples\lesson04_data 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\suite-merge.mjs（.cgui 侧）+ build\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 l4_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 l4_bindings、原 on_ready 变成 l4_init、
 *          窗口事件改成直连转发 l4_app_<event>（套件只有一个窗口，
 *          按名派发只能接一个，其余工具的窗口职责会被静默吞掉）。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 */

/**
 * @file    main.c
 * @brief   入门 04：数据与日志 —— 标签页 / 树 / 表格 / 终端 / 图表 / 分隔条 / 时钟
 * @details 前几课都是"一个控件一个值"。这一课装的是**一组数据**，外加一个**自己会刷新**的日志区，
 *          所以 API 分成两类 —— 记住这个区分就不会找错函数：
 *
 *            操作        列表框 listbox              树 tree                    表格 table
 *            加内容      cg_widget_list_add()        cg_tree_add(parent, text)  cg_table_add_row()
 *            清空        cg_widget_list_clear()      cg_tree_clear()            cg_table_clear()
 *            有多少个    cg_widget_list_count()      cg_tree_count()            cg_table_row_count()
 *            读某一项    cg_widget_list_item(i)      cg_tree_item_text(i)       cg_table_cell(row,col)
 *            当前选中    cg_widget_value()           cg_tree_selected()         cg_table_selected()
 *
 *          另外几个"只有它有"的关键 API，一句话各自带走：
 *
 *            tabs      标签页：cg_tabs_select / cg_tabs_selected；子控件用 .cgui 的 "page" 标归属，
 *                              切页时运行时自动显隐 —— 不用你在回调里 ShowWindow。
 *                              页内坐标相对**整个标签页控件**的客户区（含标签行：运行时不做
 *                              TabCtrl_AdjustRect 偏移），所以页内内容从 y=36 起；
 *                              标签行高随 DPI/主题变，y 别再给更紧
 *            terminal  日志区：cg_term_write(w, CG_TERM_RX/TX/SYS, 文字, -1)，一次调用 = 一段数据
 *                              （里面带 \r\n 会切成多行）；时间戳看 .cgui 的 checked，留多少行看 value
 *            chart     图表：  cg_chart_push(w, 系列, 值)；系列名 = .cgui 的 items，
 *                              而 value 是**窗口点数**（每系列留几个点），不是当前值
 *            splitter  分隔条：cg_splitter_pos / cg_splitter_set；它天生是"**两栏**"的语义 —— 拖它撑开
 *                              **前面那一块**，同一容器里**最后一块**吃掉余量；中间不要再夹兄弟
 *                              （夹了只平移、不改尺寸，拖两下就被窗口边裁掉）。要在一栏里放多个控件，
 *                              先用容器包成一个（本课把两个 chart 收进 tabs 的"图表"页，中间只剩一根分隔条）。
 *                              还有一条来自"运行时没有布局引擎"：拖大的一侧只会留白，拖小的一侧会裁到
 *                              容器里那些**固定尺寸**的子控件（容器不重排它们）—— 所以 pane 里放一个
 *                              **能伸缩**的控件最省事（本课右列的 term_log 就是），左边 tabs 页里那堆
 *                              固定尺寸控件不跟着动是正常的
 *            timer     时钟：  .cgui 里 period(毫秒) + events.tick → on_<id>_tick；period 不写 = 关着
 *
 *          三个新手最容易踩的点，这里都有实例：
 *            1. 树的 cg_tree_add(树, parent, 文字) 里 **parent 是节点下标，-1 表示加在根上**；
 *               所以"加在选中节点下面"就是 cg_tree_selected() 的返回值（没选中正好是 -1）；
 *            2. 表格的行勾选 / 行数值 / 行图标**不是 .cgui 字段**，是运行时 API
 *               （cg_table_set_checked / cg_table_set_row_value / cg_table_set_row_icon）；
 *            3. 弹框前先问 cg_app_has_window()：--selftest 不开窗，`cg_message_box` 会把 CI 卡住。
 *
 *          界面上每个能点的东西都有**自己的回调**（on_<id>_<动作>），回调只做三件事：改控件、
 *          写日志区、刷状态栏 —— "事件 → 数据 → 界面"这条链就是本课的全部内容。
 *
 *          想看别的课：01 基础与容器 / 02 输入与选择 / 03 数值与指示 / 05 命令与图形。
 *
 * 界面在 lesson04_data.cgui 里；本文件只有行为。
 * 编译：在项目根跑 build-app.bat lesson04_data
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 业务状态 */

/** 各控件自己加过几条，用来自动起名字 */
static int s_added_items;
static int s_added_nodes;
static int s_added_rows;
/** 时钟拍数与两个图表的当前值（不加 rand()，用拍数做伪随机游走，跑几次都一样好复现） */
static int s_beat;
static int s_load = 42;
static int s_err  = 3;
/** 状态栏第二段的文字：最近一次"选中了什么" */
static char s_sel[96];

/* ------------------------------------------------------------ 公共小工具 */

/**
 * @brief 往日志区写一行
 * @param app 应用
 * @param dir 方向：CG_TERM_RX 数据进来 / CG_TERM_TX 你的操作 / CG_TERM_SYS 程序说明
 * @param fmt printf 风格文字
 * @return void 无返回值
 * @note 行首的 [时:分:秒] 是终端控件按 .cgui 的 checked 自己加的，业务不用管时间戳
 */
static void term_line(cg_app_t *app, int dir, const char *fmt, ...)
{
    char    buf[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    cg_term_write(cg_find(app, "l4_term_log"), dir, buf, -1);
    printf("[log] %s\n", buf);      /* 自检时在控制台也能看到时序 */
}

/**
 * @brief 把"数据规模 / 当前选中 / 心跳"三段刷到状态栏（本课唯一的结果出口）
 * @param app 应用
 * @return void 无返回值
 */
static void refresh_status(cg_app_t *app)
{
    cg_widget_t *sb = cg_find(app, "l4_sb_main");

    cg_status_setf(sb, 0, "列表 %d · 树 %d · 表格 %d",
                   cg_widget_list_count(cg_find(app, "l4_lst_items")),
                   cg_tree_count(cg_find(app, "l4_tre_dir")),
                   cg_table_row_count(cg_find(app, "l4_tbl_data")));
    cg_status_set(sb, 1, s_sel[0] ? s_sel : "选中：-");
    cg_status_setf(sb, 2, "心跳 %d", s_beat);
}

/**
 * @brief 记下"当前选中了什么"，状态栏第二段显示它
 * @param fmt printf 风格文字
 * @return void 无返回值
 */
static void note_sel(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(s_sel, sizeof(s_sel), fmt, ap);
    va_end(ap);
}

/* ------------------------------------------------------------ tabs：装页面的容器 */

/**
 * @brief 切页签：四页分别是列表框 / 树 / 表格 / 图表，子控件按 .cgui 的 page 自动显隐
 * @param self 标签页控件
 * @param ev 事件类型（select）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_tab_data_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app  = cg_app_of(self);
    int         page = cg_tabs_selected(self);
    const char *name = cg_widget_list_item(self, page);   /* 标签页的页签文字也是"列表项" */

    (void)ev;
    (void)user;
    term_line(app, CG_TERM_SYS, "切到第 %d 页：%s（cg_tabs_selected = %d）",
              page + 1, name ? name : "?", page);
    refresh_status(app);
}

/* ------------------------------------------------------------ 列表框 */

/**
 * @brief 选中项变了：控件 value = 选中第几项（0 起），没选中是 -1
 * @param self 列表框控件
 * @param ev 事件类型（select）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_lst_items_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app  = cg_app_of(self);
    int         idx  = cg_widget_value(self);
    const char *text = cg_widget_list_item(self, idx);

    (void)ev;
    (void)user;
    note_sel("列表第 %d 项 %s", idx, text ? text : "（无）");
    term_line(app, CG_TERM_TX, "列表选中第 %d 项：%s", idx, text ? text : "（没选中）");
    refresh_status(app);
}

/**
 * @brief 「加一条」：带格式化的追加接口直接拼串，省掉先 snprintf 再 add 一步
 * @param self 被点的按钮
 * @param ev 事件类型（click）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_list_add_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *lst = cg_find(app, "l4_lst_items");

    (void)ev;
    (void)user;
    cg_widget_list_addf(lst, "条目 %d", ++s_added_items);
    cg_widget_list_scroll_bottom(lst);      /* 长列表 / 日志类常用：滚到最后一项 */
    term_line(app, CG_TERM_TX, "列表加一条 → 现在 %d 项", cg_widget_list_count(lst));
    refresh_status(app);
}

/**
 * @brief 「清空」：个数要先记下来，清完再问就是 0 了
 * @param self 被点的按钮
 * @param ev 事件类型（click）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_list_clear_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *lst = cg_find(app, "l4_lst_items");
    int          n   = cg_widget_list_count(lst);

    (void)ev;
    (void)user;
    cg_widget_list_clear(lst);
    note_sel("（列表已清空）");
    term_line(app, CG_TERM_TX, "列表已清空（原来是 %d 项）", n);
    refresh_status(app);
}

/* ------------------------------------------------------------ 树 */

/**
 * @brief 树上选中了某个节点：取选中下标，文字用取选中节点文字的接口
 * @param self 树控件
 * @param ev 事件类型（select）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_tre_dir_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app  = cg_app_of(self);
    int         idx  = cg_tree_selected(self);
    const char *text = cg_tree_selected_text(self);

    (void)ev;
    (void)user;
    note_sel("树节点 #%d %s", idx, text ? text : "（无）");
    term_line(app, CG_TERM_TX, "树上选中第 %d 个节点：%s（「加节点」的落点）",
              idx, text ? text : "（没选中）");
    refresh_status(app);
}

/**
 * @brief 节点被展开：运行时会把"被操作的节点"设成当前选中，所以这里问得到是哪一个
 * @param self 树控件
 * @param ev 事件类型（expand）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_tre_dir_expand(cg_widget_t *self, cg_event_t ev, void *user)
{
    const char *text = cg_tree_selected_text(self);

    (void)ev;
    (void)user;
    term_line(cg_app_of(self), CG_TERM_TX, "节点「%s」展开了", text ? text : "?");
    refresh_status(cg_app_of(self));
}

/**
 * @brief 节点被折叠：展开与折叠是两个事件，写成两个回调对照着看更清楚
 * @param self 树控件
 * @param ev 事件类型（collapse）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_tre_dir_collapse(cg_widget_t *self, cg_event_t ev, void *user)
{
    const char *text = cg_tree_selected_text(self);

    (void)ev;
    (void)user;
    term_line(cg_app_of(self), CG_TERM_TX, "节点「%s」折叠了", text ? text : "?");
    refresh_status(cg_app_of(self));
}

/**
 * @brief 「加节点」：父节点传当前选中节点，没选中时取到的正好是 -1 = 加在根上
 * @param self 被点的按钮
 * @param ev 事件类型（click）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_tree_add_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app    = cg_app_of(self);
    cg_widget_t *tre    = cg_find(app, "l4_tre_dir");
    int          parent = cg_tree_selected(tre);
    int          idx    = cg_tree_addf(tre, parent, "新节点 %d", ++s_added_nodes);

    (void)ev;
    (void)user;
    term_line(app, CG_TERM_TX, "在%s下面加了节点 #%d（现在共 %d 个）",
              (parent < 0) ? "根" : "选中节点", idx, cg_tree_count(tre));
    refresh_status(app);
}

/**
 * @brief 「全展开」：逐个节点调展开接口，节点下标从 0 数到个数减一
 * @param self 被点的按钮
 * @param ev 事件类型（click）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_tree_all_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *tre = cg_find(app, "l4_tre_dir");
    int          n   = cg_tree_count(tre);
    int          i;

    (void)ev;
    (void)user;
    for (i = 0; i < n; i++) cg_tree_expand(tre, i, 1);
    term_line(app, CG_TERM_TX, "展开了全部 %d 个节点", n);
    refresh_status(app);
}

/* ------------------------------------------------------------ 表格 */

/**
 * @brief 表格选中行变了：取选中行号，单元格用取单元格的接口
 * @param self 表格控件
 * @param ev 事件类型（select）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_tbl_data_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app  = cg_app_of(self);
    int         row  = cg_table_selected(self);
    const char *name;
    const char *city;

    (void)ev;
    (void)user;
    if (row < 0) {
        term_line(app, CG_TERM_TX, "表格没有选中行（cg_table_selected = -1）");
        refresh_status(app);
        return;
    }
    name = cg_table_cell(self, row, 0);
    city = cg_table_cell(self, row, 1);
    note_sel("表格第 %d 行 %s", row, name ? name : "?");
    /* .cgui 里 table 开了 multi：还能问"一共选中了几行" */
    term_line(app, CG_TERM_TX, "表格选中第 %d 行：%s / %s（多选共 %d 行）",
              row, name ? name : "?", city ? city : "?", cg_table_selected_count(self));
    refresh_status(app);
}

/**
 * @brief 勾选列的行状态变了（.cgui 里 table 要开 check）
 * @param self 表格控件
 * @param ev 事件类型（check）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_tbl_data_check(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       row = cg_table_selected(self);   /* 点勾选框时这一行同时也是"选中行" */
    int       n   = cg_table_row_count(self);
    int       checked = 0;
    int       i;

    (void)ev;
    (void)user;
    if (row < 0) row = 0;
    for (i = 0; i < n; i++) {
        if (cg_table_checked(self, i)) checked++;
    }
    note_sel("表格勾选 %d / %d 行", checked, n);
    term_line(app, CG_TERM_TX, "第 %d 行勾选 = %d（全表共勾了 %d / %d 行）",
              row, cg_table_checked(self, row), checked, n);
    refresh_status(app);
}

/**
 * @brief 「加一行」：单元格数组的顺序 = 列的顺序，不足的列运行时按空串补齐
 * @param self 被点的按钮
 * @param ev 事件类型（click）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_row_add_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *tbl = cg_find(app, "l4_tbl_data");
    char         name[32];
    const char  *cells[3];

    (void)ev;
    (void)user;
    snprintf(name, sizeof(name), "新人 %d", ++s_added_rows);
    cells[0] = name;
    cells[1] = "未填城市";
    cells[2] = "待定";
    cg_table_add_row(tbl, cells, 3);
    term_line(app, CG_TERM_TX, "表格加一行 → 现在 %d 行", cg_table_row_count(tbl));
    refresh_status(app);
}

/**
 * @brief 「清空」表格：只清行，列头还在
 * @param self 被点的按钮
 * @param ev 事件类型（click）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_row_clear_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *tbl = cg_find(app, "l4_tbl_data");
    int          n   = cg_table_row_count(tbl);

    (void)ev;
    (void)user;
    cg_table_clear(tbl);
    note_sel("（表格已清空）");
    term_line(app, CG_TERM_TX, "表格已清空（原来是 %d 行）", n);
    refresh_status(app);
}

/* ------------------------------------------------------------ 分隔条 */

/**
 * @brief 分隔条松手（拖完才发一次 release，拖动中不发，免得刷屏）
 * @param self 分隔条
 * @param ev 事件类型（release）
 * @param user 未使用
 * @return void 无返回值
 * @note 真实程序会在这里把位置记进 INI，下次启动用设置接口恢复
 */
static void on_sp_main_release(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    term_line(cg_app_of(self), CG_TERM_SYS,
              "分隔条 x=%d：左块撑开、右列让位、日志区吃余量", cg_splitter_pos(self));
}

/* ------------------------------------------------------------ 日志区（terminal） */

/**
 * @brief 「清空日志」：先清屏，然后自己再写一行"清完了"
 * @param self 被点的按钮
 * @param ev 事件类型（click）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_log_clear_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app  = cg_app_of(self);
    cg_widget_t *term = cg_find(app, "l4_term_log");
    int          n    = cg_term_count(term);

    (void)ev;
    (void)user;
    cg_term_clear(term);
    term_line(app, CG_TERM_SYS, "日志已清空（原来是 %d 行）", n);
}

/**
 * @brief 「保存日志」：存成 UTF-8 + CRLF 的文本文件
 * @param self 被点的按钮
 * @param ev 事件类型（click）
 * @param user 未使用
 * @return void 无返回值
 * @note 弹框前先问有没有窗口：--selftest 不开窗，弹框会把 CI 卡住；
 *       所以自检时改写用户数据目录，保证这条路径也被跑到
 */
static void on_btn_log_save_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app  = cg_app_of(self);
    cg_widget_t *term = cg_find(app, "l4_term_log");
    char         path[1024];

    (void)ev;
    (void)user;
    path[0] = '\0';

    if (cg_app_has_window(app)) {
        if (!cg_save_file_dialog(self, "保存日志", "文本文件(*.txt)|*.txt|所有文件(*.*)|*.*",
                                 "txt", path, sizeof(path))) {
            return;                         /* 用户取消：什么都不做 */
        }
    } else {
        char *dir  = cg_app_data_dir();     /* %LOCALAPPDATA%\<exe名>，会自动建 */
        char *full = dir ? cg_path_join(dir, "lesson04_data.log") : NULL;

        cg_free(dir);
        if (!full) return;
        snprintf(path, sizeof(path), "%s", full);
        cg_free(full);
    }

    if (cg_term_save(term, path) == 0) {
        term_line(app, CG_TERM_TX, "日志已存到 %s（%d 行）", cg_path_name(path), cg_term_count(term));
    } else {
        term_line(app, CG_TERM_SYS, "存不进去：%s", path);
    }
}

/**
 * @brief 「导出」（本课的禁用态示范）
 * @param self 按钮
 * @param ev 事件类型（click）
 * @param user 未使用
 * @return void 无返回值
 * @note .cgui 里写了 "enabled": false —— 皮肤按禁用态把它画成灰底灰字，点不动，这个回调一次也不会来。
 *       但它**照样要登记**在绑定表里：--selftest 会合成事件并检查"有没登记"，
 *       没登记就报"未登记 1 个"并以非 0 退出
 */
static void on_btn_export_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    term_line(cg_app_of(self), CG_TERM_TX, "导出（真做起来就是把日志另存成 CSV）");
}

/* ------------------------------------------------------------ 时钟（timer） */

/**
 * @brief 时钟每拍（.cgui 里 period: 1000）：这就是"数据自己会刷新"
 * @param self 时钟控件（时钟没有窗口，但回调的 self 就是它）
 * @param ev 事件类型（tick）
 * @param user 未使用
 * @return void 无返回值
 */
static void on_tmr_beat_tick(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app       = cg_app_of(self);
    cg_widget_t *chart_line = cg_find(app, "l4_cht_line");
    cg_widget_t *chart_bar  = cg_find(app, "l4_cht_bar");

    (void)ev;
    (void)user;
    s_beat++;

    /* 折线 / 柱状各推一个点：cg_chart_push(图, 第几个系列, 值)，单系列就传 0 */
    s_load += (s_beat * 37) % 17 - 8;
    if (s_load < 5)  s_load = 5;
    if (s_load > 95) s_load = 95;
    s_err = (s_beat * 13) % 18 + 1;
    cg_chart_push(chart_line, 0, s_load);
    cg_chart_push(chart_bar,  0, s_err);

    /* 心跳当"对端推来的一行数据"，用 RX（绿）：日志区三色一眼能分开 */
    term_line(app, CG_TERM_RX, "心跳 #%d：负载 %d%% · 错误 %d 个", s_beat, s_load, s_err);
    refresh_status(app);
}

/* ------------------------------------------------------------ 启动 */

/**
 * @brief 界面建好：先给两个图表灌一段历史数据，再写两行开场白
 * @param app 应用
 * @param user 就绪钩子的业务数据，本课没用
 * @return void 无返回值
 * @note 图表刚建好是空的；value / items 改了会重建点缓冲（已有点会丢），所以先摆好参数再灌数据
 */
void l4_init(cg_app_t *app, void *user)
{
    cg_widget_t *chart_line = cg_find(app, "l4_cht_line");
    cg_widget_t *chart_bar  = cg_find(app, "l4_cht_bar");
    int          i;

    (void)user;

    for (i = 0; i < 12; i++) {
        cg_chart_push(chart_line, 0, 40 + (i * 7) % 40);
        cg_chart_push(chart_bar,  0, 1 + (i * 5) % 12);
    }

    term_line(app, CG_TERM_SYS, "就绪：左侧四个页签，右侧每秒一行心跳");
    term_line(app, CG_TERM_SYS, "RX 绿 = 数据 / TX 蓝 = 操作 / -- 灰 = 说明");
    refresh_status(app);
}

const cg_binding_t l4_bindings[] = {
    CG_BIND("l4_on_tab_data_select",      on_tab_data_select),
    CG_BIND("l4_on_lst_items_select",     on_lst_items_select),
    CG_BIND("l4_on_btn_list_add_click",   on_btn_list_add_click),
    CG_BIND("l4_on_btn_list_clear_click", on_btn_list_clear_click),
    CG_BIND("l4_on_tre_dir_select",       on_tre_dir_select),
    CG_BIND("l4_on_tre_dir_expand",       on_tre_dir_expand),
    CG_BIND("l4_on_tre_dir_collapse",     on_tre_dir_collapse),
    CG_BIND("l4_on_btn_tree_add_click",   on_btn_tree_add_click),
    CG_BIND("l4_on_btn_tree_all_click",   on_btn_tree_all_click),
    CG_BIND("l4_on_tbl_data_select",      on_tbl_data_select),
    CG_BIND("l4_on_tbl_data_check",       on_tbl_data_check),
    CG_BIND("l4_on_btn_row_add_click",    on_btn_row_add_click),
    CG_BIND("l4_on_btn_row_clear_click",  on_btn_row_clear_click),
    CG_BIND("l4_on_sp_main_release",      on_sp_main_release),
    CG_BIND("l4_on_btn_log_clear_click",  on_btn_log_clear_click),
    CG_BIND("l4_on_btn_log_save_click",   on_btn_log_save_click),
    CG_BIND("l4_on_btn_export_click",     on_btn_export_click),
    CG_BIND("l4_on_tmr_beat_tick",        on_tmr_beat_tick),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则跑自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
