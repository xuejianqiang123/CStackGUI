/* supportlib/widget.ts -- 「控件支持库」页签的索引数据
 *
 * 数据唯一真相是 runtime/include/cstackgui.h —— 本文件只做分组与中文说明。
 * 改完跑 node tools\check-support.mjs 对齐头文件（表里写了不存在的函数会红）。
 *
 * 2026-10-31 从 designer/src/supportlib.ts（1757 行）按顶层库拆出：
 * 单文件 131KB 时定位一条命令要滚很久，而三个库之间没有任何耦合。
 */
import { c } from "./types.js";
import type { SupLib } from "./types.js";

export const WIDGET_LIB: SupLib = {
    key: "widget",
    label: "界面控件支持库",
    hint: "读写控件、列表/树/表格、菜单、画板、对话框",
    groups: [
      {
        label: "通用读取",
        cmds: [
          c("cg_widget_id", "取控件 id", "const char *cg_widget_id(const cg_widget_t *w)", "一个函数被多个控件共用时，靠它区分是谁触发的", {
            sample: `/* on_row_click 同时绑在三个按钮上 */
const char *who = cg_widget_id(self);
if (strcmp(who, "btn_add") == 0) { /* ... */ }`,
          }),
          c("cg_widget_type", "取控件类型", "const char *cg_widget_type(const cg_widget_t *w)", "返回 \"button\"/\"edit\"… 这类类型名"),
          c("cg_widget_text", "读文字", "const char *cg_widget_text(const cg_widget_t *w)", "当前显示/输入的文字", {
            sample: `const char *s = cg_widget_text(cg_find(cg_app_of(self), "edt_name"));`,
          }),
          c("cg_widget_x", "读 X", "int cg_widget_x(const cg_widget_t *w)", "相对父容器的逻辑像素"),
          c("cg_widget_y", "读 Y", "int cg_widget_y(const cg_widget_t *w)", "相对父容器的逻辑像素"),
          c("cg_widget_w", "读宽度", "int cg_widget_w(const cg_widget_t *w)", "逻辑像素"),
          c("cg_widget_h", "读高度", "int cg_widget_h(const cg_widget_t *w)", "逻辑像素"),
          c("cg_widget_visible", "读可见", "int cg_widget_visible(const cg_widget_t *w)", "1=显示，0=隐藏"),
          c("cg_widget_enabled", "读可用", "int cg_widget_enabled(const cg_widget_t *w)", "1=可交互，0=变灰点不动"),
          c("cg_widget_checked", "读勾选", "int cg_widget_checked(const cg_widget_t *w)", "checkbox / radio / switch 的勾选状态"),
          c("cg_widget_value", "读值", "int cg_widget_value(const cg_widget_t *w)", "滑块位置 / 列表框与下拉框选中项 / 标签页页码 / 表格选中行 / 工具栏按钮序号", {
            sample: `int page = cg_widget_value(cg_find(cg_app_of(self), "pages_main"));`,
          }),
          c("cg_widget_count", "控件总数", "int cg_widget_count(const cg_app_t *app)", "不含窗口伪控件，调试用"),
          c("cg_widget_get_user", "读挂载数据", "void *cg_widget_get_user(const cg_widget_t *w)", "拿回 cg_widget_set_user 挂上去的业务数据"),
        ],
      },
      {
        label: "通用修改",
        cmds: [
          c("cg_widget_set_text", "改文字", "void cg_widget_set_text(cg_widget_t *w, const char *text)", "标签/按钮/输入框都能改，立即生效", {
            sample: `cg_widget_set_text(cg_find(cg_app_of(self), "lab_status"), "已保存");`,
          }),
          c("cg_widget_set_rect", "改位置+尺寸", "void cg_widget_set_rect(cg_widget_t *w, int x, int y, int width, int height)", "一次改四个（逻辑像素）"),
          c("cg_widget_set_pos", "改位置", "void cg_widget_set_pos(cg_widget_t *w, int x, int y)", "相对父容器"),
          c("cg_widget_set_size", "改尺寸", "void cg_widget_set_size(cg_widget_t *w, int width, int height)", "逻辑像素"),
          c("cg_widget_set_visible", "显示/隐藏", "void cg_widget_set_visible(cg_widget_t *w, int visible)", "1 显示 0 隐藏", {
            sample: `cg_widget_set_visible(cg_find(cg_app_of(self), "prg_level"), busy);`,
          }),
          c("cg_widget_set_enabled", "启用/禁用", "void cg_widget_set_enabled(cg_widget_t *w, int enabled)", "0 = 变灰点不动（适合「提交中」防连点）"),
          c("cg_widget_set_checked", "设勾选", "void cg_widget_set_checked(cg_widget_t *w, int on)", "checkbox / radio / switch"),
          c("cg_widget_set_value", "设值", "void cg_widget_set_value(cg_widget_t *w, int value)", "进度条/滑块/列表框选中项等（会按 min/max 截断；颜色选择器别用这个）", {
            sample: `cg_widget_set_value(cg_find(cg_app_of(self), "prg_level"), 60);`,
          }),
          c("cg_widget_set_value_range", "设取值范围", "void cg_widget_set_value_range(cg_widget_t *w, int min, int max)", "滑块/进度条/数值输入框（edit + 调节器）/评分/分页的取值范围"),
          c("cg_widget_set_colors", "改颜色", "void cg_widget_set_colors(cg_widget_t *w, unsigned fg, unsigned bg)", "前景/背景色，0xRRGGBB；bg 传 CG_COLOR_NONE 表示跟系统", {
            sample: `cg_widget_set_colors(w, CG_RGB(0x1D, 0x1B, 0x20), CG_RGB(0xEA, 0xDD, 0xFF));`,
          }),
          c("cg_widget_set_font", "改字体", "void cg_widget_set_font(cg_widget_t *w, const char *family, int size_pt, int bold)", "字号单位是磅，和画布一致"),
          c("cg_widget_set_hint", "设悬停提示", "void cg_widget_set_hint(cg_widget_t *w, const char *text)", "鼠标停上去的小提示；空串清除", {
            sample: `cg_widget_set_hint(cg_find(cg_app_of(self), "btn_save"), "保存到文件");`,
          }),
          c("cg_widget_hint", "读悬停提示", "const char *cg_widget_hint(const cg_widget_t *w)", "没设过返回 NULL"),
          c("cg_widget_set_user", "挂业务数据", "void cg_widget_set_user(cg_widget_t *w, void *user)", "把结构体/上下文挂到控件上，回调里用第三个参数 user 拿到", {
            sample: `/* on_ready 里挂一次 */
cg_widget_set_user(cg_find(app, "lst_log"), &g_ctx);`,
          }),
          c("cg_bind", "控件绑定", "int cg_bind(cg_widget_t *src, cg_widget_t *dst, int two_way)", "dst 的值/文字/勾选一直跟着 src 走，省掉手写同步回调（画布「属性」里也能配）", {
            sample: `/* 滑块拖动时旁边的标签显示数字（等价于 .cgui 里给标签写 bind.src） */
cg_bind(cg_find(app, "sld_level"), cg_find(app, "lab_value"), 0);

/* 双向：滑块和微调框两个入口改同一个值 */
cg_bind(cg_find(app, "sld_level"), cg_find(app, "spn_level"), 1);`,
          }),
          c("cg_splitter_pos", "读分隔条位置", "int cg_splitter_pos(const cg_widget_t *w)", "相对父容器的像素（竖条是 x，横条是 y）；配 cg_ini_write 存布局", {
            sample: `int pos = cg_splitter_pos(cg_find(app, "sp_main"));
cg_ini_write(cfg, "布局", "分隔条", cg_str_format("%d", pos));`,
          }),
          c("cg_splitter_set", "设分隔条位置", "void cg_splitter_set(cg_widget_t *w, int pos)", "两侧面板跟着重排（会被夹到最小 16px）；启动时恢复布局用", {
            sample: `/* on_ready 里恢复上次拖出来的比例 */
int pos = atoi(cg_ini_read(cfg, "布局", "分隔条", "320"));
cg_splitter_set(cg_find(app, "sp_main"), pos);`,
          }),
        ],
      },
      {
        label: "图表（chart）",
        cmds: [
          c("cg_chart_push", "推一个点", "void cg_chart_push(cg_widget_t *w, int series, int value)", "给某个系列追加一个点（满了丢最老的）；series 从 0 起", {
            sample: `/* 建议在时钟回调里按采样节奏推，不要另开线程碰控件 */
void on_tick(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_chart_push(cg_find(app, "cht_temp"), 0, read_sensor());
}`,
          }),
          c("cg_chart_push_all", "一次推所有系列", "void cg_chart_push_all(cg_widget_t *w, const int *values, int nvalues)", "多通道采样最常用：一次给每个系列各一个点，只刷新一次界面", {
            sample: `int vals[2];
vals[0] = read_temp();
vals[1] = read_humi();
cg_chart_push_all(cg_find(app, "cht_env"), vals, 2);`,
          }),
          c("cg_chart_clear", "清空曲线", "void cg_chart_clear(cg_widget_t *w)", "点位归零，系列与范围保留"),
          c("cg_chart_add_series", "加一个系列", "int cg_chart_add_series(cg_widget_t *w, const char *name)", "运行时加曲线，返回系列下标（到上限返回 -1）", {
            sample: `cg_chart_add_series(cg_find(app, "cht_env"), "压力");`,
          }),
          c("cg_chart_series_count", "读系列数", "int cg_chart_series_count(const cg_widget_t *w)", "当前有几条曲线"),
          c("cg_chart_filled", "读已填点数", "int cg_chart_filled(const cg_widget_t *w, int series)", "这个系列已经攒了多少个点"),
          c("cg_chart_point", "读一个点", "int cg_chart_point(const cg_widget_t *w, int series, int index)", "下标 0 = 最老的点；越界返回 0"),
          c("cg_chart_set_range", "改 Y 轴范围", "void cg_chart_set_range(cg_widget_t *w, int min, int max)", "超范围的值按边界裁剪后画"),
        ],
      },
      {
        label: "终端 / 日志（terminal）",
        cmds: [
          c("cg_term_write", "写一行", "void cg_term_write(cg_widget_t *w, int dir, const void *data, int len)", "把收到/发出的数据灌进终端；方向决定行首标记与颜色（RX 绿 / TX 蓝 / SYS 灰）", {
            sample: `/* 串口/网络收到多少就灌多少（len 传 -1 表示按 C 字符串取长度） */
cg_term_write(cg_find(app, "term_log"), CG_TERM_RX, buf, n);

/* 发出去的也记一笔；数据里带 \\r\\n 会自动切成多行 */
cg_term_write(cg_find(app, "term_log"), CG_TERM_TX, "AT+GMR\\r\\n", -1);`,
          }),
          c("cg_term_clear", "清空", "void cg_term_clear(cg_widget_t *w)", "清屏（日志区旁边常配一个「清空」按钮）"),
          c("cg_term_set_hex", "切 HEX / 文本", "void cg_term_set_hex(cg_widget_t *w, int on)", "1 = 十六进制（01 AB 20），0 = 文本；只影响**之后**写入的行", {
            sample: `/* HEX 开关：一个按钮切一下 */
cg_term_set_hex(term, !cg_term_hex(term));`,
          }),
          c("cg_term_hex", "读显示模式", "int cg_term_hex(const cg_widget_t *w)", "1 = 当前是 HEX 模式"),
          c("cg_term_set_timestamp", "时间戳开关", "void cg_term_set_timestamp(cg_widget_t *w, int on)", "每行前面带 [时:分:秒]（排查数据间隔很有用）"),
          c("cg_term_set_max_lines", "设行数上限", "void cg_term_set_max_lines(cg_widget_t *w, int lines)", "超了从最老的丢；传 0 恢复默认 500"),
          c("cg_term_count", "读行数", "int cg_term_count(const cg_widget_t *w)", "当前多少行（用来判断「到底收到东西没有」）"),
          c("cg_term_save", "存成文件", "int cg_term_save(cg_widget_t *w, const char *path)", "UTF-8 + CRLF，不含方向标记；成功返回 0", {
            sample: `cg_term_save(cg_find(app, "term_log"), "session.log");`,
          }),
        ],
      },
      {
        label: "状态灯 / 仪表盘（led / meter）",
        cmds: [
          c("cg_led_set", "设灯状态", "void cg_led_set(cg_widget_t *w, int state)", "0 灭 / 1 亮 / 2 闪（越界夹到 0..2）；状态只能由程序写，灯本身没有事件", {
            sample: `/* 连上就点亮、断了就灭 */
cg_led_set(cg_find(app, "led_link"), ok ? 1 : 0);

/* 2 = 一闪一闪；节奏在画布「闪烁周期」里配（缺省 500ms，最小 100） */
cg_led_set(cg_find(app, "led_busy"), 2);`,
          }),
          c("cg_led_state", "读灯状态", "int cg_led_state(const cg_widget_t *w)", "返回 0 灭 / 1 亮 / 2 闪"),
          c("cg_led_set_color", "改灯色", "void cg_led_set_color(cg_widget_t *w, unsigned rgb)", "亮起的颜色，0xRRGGBB（缺省绿 0x1B6E33）；画布「亮灯颜色」改的就是它", {
            sample: `cg_led_set_color(cg_find(app, "led_err"), 0xB3261E);   /* 报警用红 */`,
          }),
          c("cg_meter_set", "设仪表值", "void cg_meter_set(cg_widget_t *w, int value)", "超过量程按边界夹取（和滑块一样走 min/max）；只能由程序写", {
            sample: `/* 按采样节奏刷新，别另开线程碰控件 */
void on_tick(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_meter_set(cg_find(app, "mtr_temp"), read_temp_c10() / 10);
}`,
          }),
          c("cg_meter_set_range", "设量程", "void cg_meter_set_range(cg_widget_t *w, int min, int max)", "改最小值 / 最大值；画布上就是「范围」那两格"),
        ],
      },
      {
        label: "列表框 / 下拉框",
        cmds: [
          c("cg_widget_list_add", "加一条", "int cg_widget_list_add(cg_widget_t *w, const char *item)", "往列表框/下拉框/标签页/状态栏追加一项，返回下标"),
          c("cg_widget_list_addf", "加一条（格式化）", "int cg_widget_list_addf(cg_widget_t *w, const char *fmt, ...)", "日志类最常用：自动滚到最后一行", {
            sample: `static int n = 0;
cg_widget_list_addf(cg_find(cg_app_of(self), "lst_log"), "第 %d 次点击", ++n);`,
          }),
          c("cg_widget_list_clear", "清空", "void cg_widget_list_clear(cg_widget_t *w)", "把内容全删掉"),
          c("cg_widget_list_count", "条目数", "int cg_widget_list_count(const cg_widget_t *w)", "当前有多少条"),
          c("cg_widget_list_item", "读第 i 条", "const char *cg_widget_list_item(const cg_widget_t *w, int index)", "下标从 0 起；越界返回 NULL"),
          c("cg_widget_list_scroll_bottom", "滚到最后", "void cg_widget_list_scroll_bottom(cg_widget_t *w)", "日志追加后手动滚到底（addf 已自动滚）"),
        ],
      },
      {
        label: "图标列表",
        cmds: [
          c("cg_iconlist_add", "加一行", "int cg_iconlist_add(cg_widget_t *w, const char *icon, const char *title, const char *sub, const char *status)",
            "行 = 图标 + 主标题 + 第二行小字 + 右侧状态；icon 是 png/jpg/bmp/gif 路径（NULL 画占位框），四个文字参数都可为 NULL", {
            sample: `cg_iconlist_add(cg_find(cg_app_of(self), "il_demo"),
                 "assets/app.png", "复制文件路径", "\\"...x.exe\\" copypath", "在用");`,
          }),
          c("cg_iconlist_clear", "清空", "void cg_iconlist_clear(cg_widget_t *w)", "删掉所有行（滚动位置一并复位）"),
          c("cg_iconlist_count", "行数", "int cg_iconlist_count(const cg_widget_t *w)", "当前有多少行"),
          c("cg_iconlist_set_checked", "设勾选", "void cg_iconlist_set_checked(cg_widget_t *w, int row, int checked)", "行级勾选；要 .cgui 写 \"check\": true 才显示勾选框列，行号越界忽略"),
          c("cg_iconlist_checked", "读勾选", "int cg_iconlist_checked(const cg_widget_t *w, int row)", "1 = 该行被勾选；行无效返回 0"),
          c("cg_iconlist_set_row", "改一行", "void cg_iconlist_set_row(cg_widget_t *w, int row, const char *icon, const char *title, const char *sub, const char *status)",
            "整行改写，勾选态保持；NULL / 空串把该段清掉"),
        ],
      },
      {
        label: "状态栏",
        cmds: [
          c("cg_status_set", "改第 N 段", "void cg_status_set(cg_widget_t *w, int part, const char *text)", "分段下标从 0 起；等于段数时相当于追加", {
            sample: `cg_status_set(cg_find(cg_app_of(self), "sb_main"), 0, "就绪");`,
          }),
          c("cg_status_setf", "改第 N 段（格式化）", "void cg_status_setf(cg_widget_t *w, int part, const char *fmt, ...)", "像 printf 那样拼好再写，省一个临时缓冲"),
        ],
      },
      {
        label: "标签页",
        cmds: [
          c("cg_tabs_count", "页数", "int cg_tabs_count(const cg_widget_t *w)", "标签页一共有几页"),
          c("cg_tabs_select", "切到某页", "void cg_tabs_select(cg_widget_t *w, int index)", "程序主动切页（子控件随之显示/隐藏）"),
          c("cg_tabs_selected", "当前页", "int cg_tabs_selected(const cg_widget_t *w)", "从 0 起"),
        ],
      },
      {
        label: "树",
        cmds: [
          c("cg_tree_add", "加节点", "int cg_tree_add(cg_widget_t *w, int parent, const char *text)", "parent 传 -1 表示加在根上，返回节点下标", {
            sample: `int root = cg_tree_add(tree, -1, "项目");
cg_tree_add(tree, root, "runtime");
cg_tree_add(tree, root, "designer");`,
          }),
          c("cg_tree_addf", "加节点（格式化）", "int cg_tree_addf(cg_widget_t *w, int parent, const char *fmt, ...)", "像 printf 那样拼好节点文字再加"),
          c("cg_tree_clear", "清空整棵树", "void cg_tree_clear(cg_widget_t *w)", "删掉所有节点"),
          c("cg_tree_count", "节点数", "int cg_tree_count(const cg_widget_t *w)", "含所有层级"),
          c("cg_tree_item_text", "读节点文字", "const char *cg_tree_item_text(const cg_widget_t *w, int item)", "item 是 cg_tree_add 返回的下标"),
          c("cg_tree_selected", "当前选中节点", "int cg_tree_selected(const cg_widget_t *w)", "没选中返回 -1", {
            sample: `static void on_tree_select(cg_widget_t *self, cg_event_t ev, void *user) {
    int item = cg_tree_selected(self);
    if (item < 0) return;
    cg_widget_set_text(cg_find(cg_app_of(self), "lab_tip"), cg_tree_selected_text(self));
}`,
          }),
          c("cg_tree_selected_text", "选中节点文字", "const char *cg_tree_selected_text(const cg_widget_t *w)", "等价于 item_text(selected)"),
          c("cg_tree_expand", "展开/折叠", "void cg_tree_expand(cg_widget_t *w, int item, int expand)", "expand=1 展开，0 折叠"),
        ],
      },
      {
        label: "表格",
        cmds: [],
        children: [
          {
            label: "基本",
            cmds: [
              c("cg_table_add_column", "加列", "int cg_table_add_column(cg_widget_t *w, const char *title, int width)", "width 传 0 表示按控件宽度均分"),
              c("cg_table_add_row", "加行", "int cg_table_add_row(cg_widget_t *w, const char *const *cells, int ncells)", "不足的列按空串处理，返回行号", {
                sample: `const char *cells[] = { "张三", "上海" };
cg_table_add_row(cg_find(cg_app_of(self), "tbl_data"), cells, 2);`,
              }),
              c("cg_table_clear", "清空表格", "void cg_table_clear(cg_widget_t *w)", "清行（列保留）"),
              c("cg_table_row_count", "行数", "int cg_table_row_count(const cg_widget_t *w)", "当前有多少行"),
              c("cg_table_cell", "读单元格", "const char *cg_table_cell(const cg_widget_t *w, int row, int col)", "行/列都从 0 起"),
              c("cg_table_selected", "当前选中行", "int cg_table_selected(const cg_widget_t *w)", "没选中返回 -1"),
            ],
          },
          {
            label: "行状态（多选 / 勾选 / 图标）",
            cmds: [
              c("cg_table_set_checked", "勾选某行", "void cg_table_set_checked(cg_widget_t *w, int row, int on)", "需要 .cgui 里 table 开了 check（画布属性里勾）", {
                sample: `for (int r = 0; r < cg_table_row_count(tbl); r++) cg_table_set_checked(tbl, r, 1);`,
              }),
              c("cg_table_checked", "读某行勾选", "int cg_table_checked(const cg_widget_t *w, int row)", "1=该行被勾选"),
              c("cg_table_set_row_value", "存行数值", "int cg_table_set_row_value(cg_widget_t *w, int row, int value)", "给每行挂一个业务整数（如数据库主键），界面不显示"),
              c("cg_table_row_value", "读行数值", "int cg_table_row_value(const cg_widget_t *w, int row)", "读回上面存的整数"),
              c("cg_table_set_row_icon", "设行图标", "void cg_table_set_row_icon(cg_widget_t *w, int row, const char *path)", "png/jpg/bmp/gif 路径；NULL 清除"),
              c("cg_table_selected_count", "选中行数", "int cg_table_selected_count(const cg_widget_t *w)", "多选模式下才有意义"),
              c("cg_table_selected_row", "第 i 个选中行", "int cg_table_selected_row(const cg_widget_t *w, int index)", "配合 selected_count 遍历多选的行号"),
            ],
          },
        ],
      },
      {
        label: "图片",
        cmds: [
          c("cg_widget_set_image", "换图片", "void cg_widget_set_image(cg_widget_t *w, const char *path)", "image / iconbutton 都行（png/jpg/bmp/gif）；路径不存在显示占位框", {
            sample: `cg_widget_set_image(cg_find(cg_app_of(self), "img_logo"), "assets/logo.png");`,
          }),
          c("cg_widget_image", "读图片路径", "const char *cg_widget_image(const cg_widget_t *w)", "当前显示的图片文件路径"),
        ],
      },
      {
        label: "菜单",
        cmds: [
          c("cg_menu_last_id", "刚点的菜单项 id", "const char *cg_menu_last_id(const cg_widget_t *w)", "只能在自己的 click 回调里问；菜单项没写 id 返回 NULL", {
            sample: `static void on_menu_click(cg_widget_t *self, cg_event_t ev, void *user) {
    const char *id = cg_menu_last_id(self);
    if (id && strcmp(id, "m_quit") == 0) cg_quit(cg_app_of(self));
}`,
          }),
          c("cg_menu_last_text", "刚点的菜单项文字", "const char *cg_menu_last_text(const cg_widget_t *w)", "菜单项没写 id 时按文字判断"),
          c("cg_menu_count", "可点项个数", "int cg_menu_count(const cg_widget_t *w)", "不含分隔线与下拉项，调试用"),
          c("cg_menu_add_item", "加菜单项", "int cg_menu_add_item(cg_widget_t *w, const char *parent_id, int pos, const char *text, const char *id)",
            "运行时往菜单里加一项。parent_id = 那个下拉自己的 id（顶层传 NULL），pos = -1 追加，text 写 \"-\" 是分隔线。返回新项下标 / 失败 -1", {
              params: [
                "w：菜单控件（menu / menubutton）",
                "parent_id：下拉项的 id；NULL 或 \"\" = 顶层",
                "pos：插到该下拉第几项之前；-1 = 追加",
                "text：显示文字；\"-\" = 分隔线",
                "id：项标识（后续增删改定位用），可空",
              ],
              ret: "新项在行表里的下标；失败 -1",
              sample: `/* 例：「最近打开」下拉里追加一条 */
cg_menu_add_item(cg_find(app, "mnu"), "m_recent", -1, "D:\\\\data\\\\a.txt", "m_r1");`,
            }),
          c("cg_menu_remove_item", "删菜单项", "int cg_menu_remove_item(cg_widget_t *w, const char *id)",
            "删掉那一项**连同它的子树**；返回删掉的行数 / 失败 -1", {
              sample: `cg_menu_remove_item(mnu, "m_r1");`,
            }),
          c("cg_menu_clear_items", "清空菜单项", "int cg_menu_clear_items(cg_widget_t *w, const char *parent_id)",
            "清空某个下拉里的项（下拉自身保留）；parent_id 传 NULL = 清空整个菜单", {
              sample: `cg_menu_clear_items(mnu, "m_recent");   /* 「最近打开」清空 */`,
            }),
          c("cg_menu_set_item_text", "改菜单项文字", "void cg_menu_set_item_text(cg_widget_t *w, const char *id, const char *text)",
            "改某一项的文字（动作项无效：文字归动作管）", {
              sample: `/* 例：菜单里显示当前端口 */
cg_menu_set_item_text(cg_find(app, "mnu"), "m_port", "端口: COM7");`,
            }),
          c("cg_menu_set_item_enabled", "灰掉/恢复菜单项", "void cg_menu_set_item_enabled(cg_widget_t *w, const char *id, int enabled)",
            "禁用（灰掉）或恢复某一项；动作项无效（用 cg_action_set_enabled）", {
              sample: `cg_menu_set_item_enabled(mnu, "m_save", 0);   /* 没有内容可存时灰掉 */`,
            }),
          c("cg_menu_set_item_checked", "菜单项打勾", "void cg_menu_set_item_checked(cg_widget_t *w, const char *id, int checked)",
            "给某一项打勾 / 取消；动作项无效（用 cg_action_set_checked）", {
              sample: `cg_menu_set_item_checked(mnu, "m_grid", 1);`,
            }),
        ],
      },
      {
        label: "画板",
        cmds: [
          c("cg_canvas_clear", "擦除画板", "void cg_canvas_clear(cg_widget_t *w)", "清掉之前画的所有图形"),
          c("cg_canvas_set_color", "设画笔颜色", "void cg_canvas_set_color(cg_widget_t *w, unsigned rgb)", "影响后续图形，0xRRGGBB"),
          c("cg_canvas_set_line_width", "设线宽", "void cg_canvas_set_line_width(cg_widget_t *w, int px)", "影响后续描边"),
          c("cg_canvas_line", "画线", "void cg_canvas_line(cg_widget_t *w, int x1, int y1, int x2, int y2)", "坐标相对控件左上角", {
            sample: `cg_canvas_clear(cv);
cg_canvas_set_color(cv, 0x6750A4);
cg_canvas_set_line_width(cv, 2);
cg_canvas_line(cv, 0, 0, 100, 80);`,
          }),
          c("cg_canvas_rect", "画矩形", "void cg_canvas_rect(cg_widget_t *w, int x, int y, int wd, int ht, int fill)", "fill=0 描边，1 实心"),
          c("cg_canvas_ellipse", "画椭圆", "void cg_canvas_ellipse(cg_widget_t *w, int x, int y, int wd, int ht, int fill)", "fill=0 描边，1 实心"),
          c("cg_canvas_text", "写文字", "void cg_canvas_text(cg_widget_t *w, int x, int y, const char *text)", "在指定位置画一行文字"),
        ],
      },
      {
        label: "网页浏览",
        cmds: [
          c("cg_browser_navigate", "打开网页", "void cg_browser_navigate(cg_widget_t *w, const char *url)", "http/https 或本地路径；实验性（走系统 IE 内核）"),
        ],
      },
      {
        label: "对话框",
        cmds: [],
        children: [
          {
            label: "信息框",
            cmds: [
              c("cg_message_box", "弹消息框", "void cg_message_box(cg_widget_t *parent, const char *text, const char *title, int icon)", "只有一个「确定」钮；icon：0=信息 1=警告 2=错误 3=问号", {
                params: ["parent：用来定位的控件，传 self 即可", "text：正文", "title：标题（可 NULL）", "icon：0/1/2/3"],
                sample: `if (!cg_app_has_window(cg_app_of(self))) return;   /* 自检时别弹 */
cg_message_box(self, "保存成功", "提示", 0);`,
              }),
            ],
          },
          {
            label: "打开文件",
            cmds: [
              c("cg_open_file_dialog", "选文件打开", "int cg_open_file_dialog(cg_widget_t *parent, const char *title, const char *filter, char *out_path, int out_size)", "系统「打开」对话框；返回 1 = 用户确定，路径写进 out_path", {
                params: ["filter 写法：\"文本文件(*.txt)|*.txt|所有文件(*.*)|*.*\"", "out_path：至少 1024 字节的缓冲区"],
                sample: `char path[1024];
if (cg_open_file_dialog(self, "选择文件", "文本文件(*.txt)|*.txt|所有文件(*.*)|*.*", path, sizeof(path))) {
    cg_widget_set_text(cg_find(cg_app_of(self), "lab_path"), path);
}`,
              }),
            ],
          },
          {
            label: "保存文件",
            cmds: [
              c("cg_save_file_dialog", "选路径保存", "int cg_save_file_dialog(cg_widget_t *parent, const char *title, const char *filter, const char *default_ext, char *out_path, int out_size)", "系统「另存为」对话框；返回 1 = 用户确定", {
                sample: `char path[1024];
if (cg_save_file_dialog(self, "保存到", "文本文件(*.txt)|*.txt", "txt", path, sizeof(path))) {
    FILE *f = fopen(path, "wb");
    if (f) { fputs("内容", f); fclose(f); }
}`,
              }),
            ],
          },
          {
            label: "选择颜色",
            cmds: [
              c("cg_choose_color", "选颜色", "int cg_choose_color(cg_widget_t *parent, unsigned *rgb)", "系统取色器；确认时把选中的 0xRRGGBB 写回 rgb 并返回 1", {
                sample: `unsigned rgb = 0x6750A4;
if (cg_choose_color(self, &rgb)) {
    cg_widget_set_colors(cg_find(cg_app_of(self), "lab_tip"), rgb, CG_COLOR_NONE);
}`,
              }),
            ],
          },
        ],
      },
    ],
};
