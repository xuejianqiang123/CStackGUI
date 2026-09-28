# 入门 05 · 命令与图形：菜单、画板、媒体

> 目录 `examples\lesson05_commands\`　编译 `build-app.bat lesson05_commands`　运行 `examples\lesson05_commands\lesson05_commands.exe`　自检 `lesson05_commands.exe --selftest examples\lesson05_commands\lesson05_commands.cgui` → 控件 23 个 / 回调 9 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 这一课讲什么
两件事：**命令从哪来**（同一个动作可以从菜单栏 / 工具栏 / 菜单按钮三处发起，最后都走同一个 `do_command()`）与**图形与媒体**（画板绘图、外形、图片、取色、网页浏览）。

## 怎么跑
1. 在项目根编译：`build-app.bat lesson05_commands`，双击 `examples\lesson05_commands\lesson05_commands.exe`。
2. 分别从菜单栏、工具栏、菜单按钮发同一个命令，看状态栏写明"这次是从哪儿点的"。
3. 用色块取色，点「画圆 / 画矩形」在画板上作图，点「清空」清掉；再在画板上**点一下**，看它按点击位置画一个点。
4. 点「载入示例页」让网页控件打开一个本地 html。

## 关键控件一览（本课 23 个控件 / 9 个回调）
| 控件 id | 类型 | 看什么 |
| --- | --- | --- |
| `menu_main` | `menu` | 菜单栏，点中项用 `cg_menu_last_id(self)` 认 |
| `tb_cmd` | `toolbar` | 点中第几项读 `cg_widget_value(self)`（0 起） |
| `mbtn_more` | `menubutton` | 长得像按钮的菜单 |
| `cp_ink` | `colorpicker` | 当前色在 `value`（`0xRRGGBB`），改完派发 `change` |
| `cv_board` | `canvas` | `cg_canvas_*` 画图，命令被录制、重绘时回放 |
| `sh_*` | `shape` | `format` = `rect` / `roundrect` / `ellipse` |
| `img_logo` | `image` | `src` 放图 |
| `br_page` | `browser` | 实验性网页控件，`cg_browser_navigate` 开网页 |

## 重点看哪几处
- **一个动作只有一处实现**：菜单项、工具栏按钮、菜单按钮项最后都走同一个 `do_command()`，加一条命令只改一处。
- **菜单项要写 `id`**，用 `cg_menu_last_id()` 认；工具栏没有 id，点中第几项读 `cg_widget_value()`。
- **画板自己的鼠标事件给的坐标就是画板内坐标**（`cg_event_x` / `cg_event_y`），**不用再减** `cg_widget_x` / `cg_widget_y`；画板绘制会被记录并按序回放。

## 详细讲解
见 `docs\articles\05-命令与图形.md`，以及 `examples\教学使用说明.md` 的「入门 05」小节。
