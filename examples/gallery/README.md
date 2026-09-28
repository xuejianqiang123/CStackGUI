# 控件陈列室 gallery：一屏把所有控件与事件摆出来

> 目录 `examples\gallery\`　编译 `build-app.bat gallery`　运行 `examples\gallery\gallery.exe`　自检 `gallery.exe --selftest examples\gallery\gallery.cgui` → 控件 67 个 / 回调 47 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
规模最大的一份示例：一次编译，在**三页顶层标签页**里见到全部控件 —— 页 0「基础闭环」讲控件之间怎么联动，页 1「控件陈列」逐个摆控件，页 2「新特性」演示时钟 / 通用对话框 / 画板 / 取色 / 表格勾选多选 / 评分 / 分页 / 徽章 / 标签片 / 卡片 / 外形框 / 网页浏览 / 焦点事件。
界面里每个可见控件在 `main.c` 里都有对应回调：点一下，状态栏第二段与页 0 的 `lst_log` 就会写出"谁被点了、值是多少"。它也可以当一个**控件字典**用：拿某个控件的 `id` / `type` 回查 `gallery.cgui`，比翻文档快。

## 二、怎么用
1. 在项目根编译：`build-app.bat gallery`，再双击 `examples\gallery\gallery.exe`（界面文件 `gallery.cgui` 就在旁边，改界面不必重编译）。
2. 先停在页 0：改「个人信息」里的姓名、勾 VIP、拖等级滑块，看标签与进度条跟着动、`lst_log` 追加日志。
3. 切到页 1：菜单、工具栏、菜单按钮、开关、数值微调、日期 / 时间、树、表格、月历各点一下，注意状态栏写什么。
4. 切到页 2：点「打开文件 / 保存文件 / 消息框」看通用对话框；在表格里勾选多行；拖画板、取色、评分、翻页，并看时钟每秒刷新的那一项。
5. 想查某个控件怎么写：在 `gallery.cgui` 里搜它的 `id`，再到 `main.c` 搜同名回调函数。

## 三、一屏总览（只列关键几类，全部 67 个见 `gallery.cgui`）
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `pages_main` | `tabs` | 顶层三页（基础闭环 / 控件陈列 / 新特性），切页发 `select` |
| `edt_name` / `chk_vip` / `sld_level` | `edit` / `checkbox` / `slider` | 页 0 的"值 → 显示"联动 |
| `prg_level` / `ring_count` | `progress` / `progressring` | 只被程序写的指示控件 |
| `lst_log` | `listbox` | 页 0 的操作日志列表 |
| `menu_main` / `tb_main` / `mbtn_more` | `menu` / `toolbar` / `menubutton` | 三种命令入口：`cg_menu_last_id()`、`cg_widget_value()` |
| `spn_count` | `edit`（`spin`） | 数值微调 |
| `tree_files` / `tbl_data` / `tb_users` | `tree` / `table` | 动态加节点 / 加行；`tb_users` 演示 `check` + `multi` |
| `cal_date` / `dt_date` / `rt_notes` | `monthcal` / `datepicker` / `richtext` | 日期类与富文本 |
| `tm_clock` | `timer` | 每秒 `tick` 一次 |
| `cv_board` / `rt_star` / `pg_main` | `canvas` / `rate` / `pagination` | 画板 / 评分 / 分页 |
| `bd_count` / `tg_demo` / `card_main` / `sh_*` | `badge` / `tag` / `card` / `shape` | 装饰与容器 |
| `br_page` | `browser` | 网页浏览（实验性） |
| `sb_main` | `statusbar` | 两段：业务概况 + 最近一次操作 |

## 四、关键接口
`cg_find` / `cg_widget_set_text` / `cg_widget_set_value` / `cg_widget_value` 读写控件；`cg_widget_checked` / `cg_widget_list_add` / `cg_widget_list_item` / `cg_menu_last_id` 读交互结果；`cg_tabs_selected` / `cg_tree_add` / `cg_table_add_row` / `cg_table_clear` 操作数据类控件；`cg_status_set` / `cg_set_timer` / `cg_cancel_close` 状态与时钟；`cg_open_file_dialog` / `cg_save_file_dialog` / `cg_message_box` 通用对话框。

## 五、容易踩的坑
- 页内控件的 `rect` 是相对**标签页客户区**的，切页时运行时自动显隐，不需要自己在回调里 `ShowWindow`。
- `on_close` 用 `cg_cancel_close()` 演示"这次先不关"：不处理就会被它拦下来。
- 自检不开窗，文件对话框这类会弹窗的动作，回调里要先 `if (cg_app_has_window(app))`。

## 六、相关文档
- `examples\教学使用说明.md`：六课学习路线与全部工具的索引。
- `docs\CGUI-FORMAT.md` 第 2 / 4 节：`.cgui` 字段与控件类型 / 事件表。
- `docs\ARCHITECTURE.md`：运行时整体结构与取舍。
