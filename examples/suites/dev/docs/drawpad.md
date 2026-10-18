> **本文档是合并前独立工程 `examples\drawpad\` 的原文**（2026-10-18 该工程已并入
> [`examples\suites\dev\`](../dev)：本工具的完整逻辑在 `modules\dp.c`，
> 编译 `build-app.bat suites\dev`，五个工具各占一个页签）。
>
> 文中「目录 / 编译 / 运行 / 自检」那几行命令是**合并前的口径，现已失效**（原工程目录已删）；
> 其余内容 —— 它演示什么、关键接口、容易踩的坑 —— 仍然逐条成立，对应 `modules\dp.c`。
# 画板 drawpad：鼠标拖拽作图、撤销 / 重做、导出 PNG

> 目录 `examples\drawpad\`　编译 `build-app.bat drawpad`　运行 `examples\drawpad\drawpad.exe`　自检 `drawpad.exe --selftest examples\drawpad\drawpad.cgui` → 控件 13 个 / 回调 7 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
画板上的图形**不是像素，而是被录制的绘制命令**：每画一笔记一条命令，控件重绘（被盖住又露出、改大小）时按序回放 —— 所以**没有**"擦掉某一笔"的接口。示例把每条命令**另存**进一张固定数组，它才是可撤销的"真相来源"：撤销 = 丢掉最后一条 → 清空记录 → 重放剩下的。
鼠标事件挂在**画板自己**（`mousedown` / `mousemove` / `mouseup`），按下时运行时自动捕获鼠标，拖出边界也不断笔；直线 / 矩形 / 椭圆在拖动中有**橡皮筋预览**，松手才提交。

## 二、怎么用
1. 在项目根编译：`build-app.bat drawpad`，双击 `examples\drawpad\drawpad.exe`。
2. 顶部「工具」下拉选一种（直线 / 矩形 / 椭圆 / 文字 / 自由笔）；点「颜色」色块弹系统取色器；「笔宽」数值框调 1..12。
3. 在白色画板上**按下拖动**：直线 / 矩形 / 椭圆拖动中就有预览、松手提交；文字工具单击一次落一个字；自由笔一路划过去逐段提交。
4. 「撤销」退掉最后一笔，「重做」放回来，「清空」全部丢弃；状态栏三段分别是当前工具、命令条数、最近一次操作。
5. 「导出 PNG」把画板截图存进用户数据目录（文件名带时间戳）；底部终端会记下每一步作图 / 撤销 / 导出。
6. 命令行：`drawpad.exe`（自动找同名 `.cgui`）、`drawpad.exe xxx.cgui`、`--selftest`。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `cbo_tool` | `combo` | 工具：0 直线 / 1 矩形 / 2 椭圆 / 3 文字 / 4 自由笔 |
| `cp_ink` | `colorpicker` | 当前色（`value` = `0xRRGGBB`），点它弹系统取色器 |
| `edt_pen` | `edit`（`spin`） | 笔宽数值框，1..12 |
| `btn_undo` / `btn_redo` / `btn_clear` | `button` | 撤销 / 重做 / 清空，共用一个回调 |
| `btn_export` | `button` | 导出 PNG |
| `cv_board` | `canvas` | 画板本身，挂 `mousedown` / `mousemove` / `mouseup` |
| `term_log` | `terminal` | 每一步操作记一行 |
| `sb_main` | `statusbar` | 三段：当前工具 / 命令条数 / 最近操作 |

## 四、关键接口
- 画板绘制：`cg_canvas_clear`、`cg_canvas_set_color`、`cg_canvas_set_line_width`、`cg_canvas_line`、`cg_canvas_rect`、`cg_canvas_ellipse`、`cg_canvas_text`
- 鼠标坐标：`cg_event_x`、`cg_event_y`（**就是画板内**的逻辑像素）
- 截图与落盘：`cg_widget_screenshot`、`cg_app_data_dir`、`cg_path_join`
- 记录与状态：`cg_term_write`、`cg_status_set`、`cg_status_setf`

## 五、容易踩的坑
- 鼠标事件**必须挂在画板这个子控件上**，不能挂窗口级 `app.events` —— 按下 / 移动 / 抬起落在画板上时消息只送给画板，父窗口收不到（早先挂窗口的写法在真机上"按下拖动毫无反应"）。
- 画板事件里的坐标**就是画板内**像素，不用再减 `cg_widget_x` / `cg_widget_y`。
- 画板的绘制是"录制 → 回放"模型，**没有"画一笔但不作数"的接口**，所以拖动预览只能"把临时命令写进命令表空槽 + 整表重放"（见 `preview_op`）。
- 命令表是固定数组（上限 `kMaxOps = 4096`）：满了只报一次提示并拒绝起笔；自由笔贴着上一个点 2px 以内的移动不记，免得几秒见底。
- 画板重绘走双缓冲，所以"每步重放整张画布"的预览不会闪。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · drawpad」。
- `docs\articles\05-命令与图形.md`：`canvas` 的绘制接口与"命令被录制回放"的基础在这课。
- `docs\CGUI-FORMAT.md`：`canvas` 的字段、事件与"没有预演接口"这条边界。
