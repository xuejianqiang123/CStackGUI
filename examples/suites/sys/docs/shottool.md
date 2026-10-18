> **本文档是合并前独立工程 `examples\shottool\` 的原文**（2026-10-18 该工程已并入
> [`examples\suites\sys\`](../sys)：本工具的完整逻辑在 `modules\sh.c`，
> 编译 `build-app.bat suites\sys`，五个工具各占一个页签）。
>
> 文中「目录 / 编译 / 运行 / 自检」那几行命令是**合并前的口径，现已失效**（原工程目录已删）；
> 其余内容 —— 它演示什么、关键接口、容易踩的坑 —— 仍然逐条成立，对应 `modules\sh.c`。
# 截图工具：托盘 + 全局热键的 QQ 风格截图链路

> 目录 `examples\shottool\`　编译 `build-app.bat shottool`　运行 `examples\shottool\shottool.exe`　自检 `shottool.exe --selftest` → 控件 25 个 / 回调 10 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

> 另有一份 `shottool.rc`：画布导出时会从 `.cgui` 的 `app.info` 生成 `<工程>.rc`，**手写 `.cgui` 不会**——
> 补上它，exe 的右键属性 → 详细信息里才有产品名 / 版本 / 说明（写法与 `gallery.rc` 同源，
> 生成器见 `designer/src/codegen.ts` 的 `rcFile()`）。文件是 UTF-8，别存成 GBK。

## 一、它演示什么

按 [QQScreenShot](https://github.com/insoxin/QQScreenShot) 那类"常驻后台、一按就出来"的截图工具**自实现**（那个仓库只有逆向 QQ 的二进制与 README，没有可移植源码），走通核心链路：

- **三个入口一条命令**：全局热键 `Ctrl+Alt+A`（`cg_hotkey_set`，**程序在后台 / 托盘里也按得到**）、面板按钮、托盘左键 —— 都落到同一条动作 `shot`。热键被别的程序占用只会注册失败（写日志），按钮与托盘就是兜底入口。
- **框选**：面板先藏起来 → 一次性定时器等一拍（等 DWM 把窗口撤下屏）→ `cg_screen_capture` 抓整屏 → 窗口切无边框铺满（`cg_win_set_border(app,"none")`）→ 这张截图设成**画板背景图**（`cg_widget_set_image` 作用在 `canvas` 上）→ 拖橡皮筋。窗口在 (0,0) 无边框铺满，所以**画板坐标 == 屏幕逻辑坐标**，选区直接就是下一次抓屏的参数。
- **标注**：按选区重抓一块 → 窗口贴到选区原点 → 截图当底、标注是录制的绘制命令（矩形 / 椭圆 / 箭头 / 画笔 / 文字，撤销按"整笔"走）。标注条摆在**选区下方额外撑出来的那行**：不盖画面、不进成品。
- **成品 = 抓画板自己**（`cg_widget_screenshot(cv_shot)`）：背景图 + 命令回放恰好就是"选区 + 标注"，不用裁剪、也不用把工具条藏来藏去。文件名 QQ 口味：`ScreenShot<日期时间>.png`，落在用户数据目录，**完成即写进剪贴板**（`cg_clip_set_image`）。
- **贴图**：存图后留在屏幕上当贴图（窗口缩到选区本身），双击贴图或 `Esc` 回面板。
- **GIF 录屏**：「录屏 GIF」按钮或 `Ctrl+Alt+G` —— **复用同一套框选**（先框一块区域），松手即开始录；
  按帧率定时抓屏、逐帧喂给 runtime 的 GIF 编码器；点「停止」存成动画 GIF（`Rec<日期时间>.gif`）。
  编码器有**脏矩形优化**：只编码与上一帧不同的那一块，没变的像素走透明索引 ——
  录一段界面操作，静止部分几乎不占体积（实测：41 帧里 28 帧因无变化被跳过，成品 25 KB）。

## 二、怎么用

1. 运行 → 面板（日志 + 状态栏）。点「开始截图」/ 按 `Ctrl+Alt+A` / 左键点托盘图标，任一入口触发。
2. 屏幕被整屏截图铺满，按住左键拖出选区（提示行实时报 `选区 W x H`），松手进标注；`Esc` 取消。
3. 标注条在选区下方：工具 / 颜色 / 粗细 / 文字内容 + 撤销 / 清除 / 完成 / 贴图 / 取消。
   换工具时日志会记一行「工具：椭圆」这类 —— 用来确认"你选的工具确实生效了"（事件 `select`）。
4. 「完成」= 存 PNG + 复制到剪贴板（可直接粘贴）+ 回面板；「贴图」= 同样存/复制但留在屏幕上；「取消」或 `Esc` = 不存图回面板。
5. 真正退出：「退出」按钮或 `Ctrl+Shift+Q` —— 先摘托盘图标再关消息循环。关窗口只是藏进托盘。

**录屏**（`Ctrl+Alt+G` 或「录屏 GIF」按钮）：面板上先用「帧率」下拉选 5/10/15/20 fps（默认 10）→
触发后同样是先框一块区域 → 松手**立刻开始录**。录制时窗口收成**只有工具条那一行**、摆在选区
**下方**（这样才不会盖住要录的画面），工具条只剩「停止」「取消」：

- 「停止」= 存 GIF 到用户数据目录 + 气泡提示 + 回面板；「取消」= 丢弃这一段。
- 录制中看不到选区边框（边框会盖住画面），区域尺寸写在提示行与日志里。
- 状态栏显示「录制中」，日志逐条记：区域 / 帧率 / 结束时的帧数与字节数。

## 三、一屏总览

| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `sb_main` | statusbar | 模式（就绪/框选中/标注中/贴图中）/ 热键状态 / 截图张数 |
| `lab_title` / `lab_tip1` / `lab_tip2` | label | 标题与两行用法 |
| `btn_shot` / `btn_quit` | button | 引用动作 `shot` / `quit`（点击即触发动作） |
| `lst_log` | listbox | 全流程日志（热键注册、抓屏、选区尺寸、存图路径、失败原因） |
| `cv_shot` | canvas | 框选与标注的画布：**背景图 = 刚抓的屏幕**，标注 = 录制的绘制命令 |
| `lab_mode` | label | overlay 提示行（必须排在 `cv_shot` **之后**，后创建的窗口才压得住画布） |
| `cbo_tool` / `cbo_color` / `cbo_width` / `edt_text` | combo×3 / edit | 标注条：工具 / 颜色 / 粗细 / 文字内容 |
| `btn_undo` / `btn_clear` / `btn_finish` / `btn_pin` / `btn_cancel` | button×5 | 按整笔撤销 / 清空标注 / 存图复制回面板 / 贴图 / 不存图退出 |
| `btn_rec` | button | 引用动作 `rec`（录屏入口，等价于热键 `Ctrl+Alt+G`） |
| `lab_fps` / `cbo_fps` | label / combo | 录屏帧率：5 / 10 / 15 / 20 fps（默认 10；**开始那一刻**读一次） |
| `actions`（3 条） | action | `shot`（`Ctrl+Alt+A`）/ `rec`（`Ctrl+Alt+G`）/ `quit`（`Ctrl+Shift+Q`） |

## 四、关键接口

- `cg_hotkey_set` / `WM_HOTKEY`：注册全局热键 = 触发同名动作；**与动作 accel 的区别**（accel 在消息循环里由 TranslateAccelerator 吃，窗口失焦就不生效）。NULL 解绑全部，关窗自动收。
- `cg_screen_size` / `cg_screen_origin` / `cg_screen_capture`：**虚拟桌面**的逻辑尺寸 / 原点 / 区域抓 PNG（坐标入参，DPI 内部换算，越界自动夹进虚拟桌面；多屏时坐标可为负）。
- `cg_win_set_border(app,"none"/"sizable")`：面板 ↔ 无边框 overlay 的开关；配套 `cg_win_set_pos`（**物理**像素）+ `cg_win_set_size`（**逻辑**像素）+ `cg_scale_px`（两者之间的换算）。
- `cg_widget_set_image(cv, path)`：设在 **canvas** 上 = **画板背景图**（本示例的立身之本）。
- `cg_widget_screenshot(cv, path)`：抓画板 = 成品（画板的 WM_PRINT 已接，PrintWindow 不会抓出空图）。
- `cg_clip_set_image(path)` / `cg_clip_get_image(path)`：图片写 / 读剪贴板（CF_DIB + CF_BITMAP 双格式）。
- `cg_set_timer(app,150,fn,app)`：一次性定时器 —— 藏窗口后**隔一拍再抓屏**，否则截到的是自己。
- `cg_gif_rec_start` / `cg_gif_rec_stop` / `cg_gif_rec_info`：GIF 录屏三件套 —— 按区域与帧率开始、
  停止时给路径就落盘（给 NULL 丢弃）、随时查已录帧数与毫秒。
- `cg_gif_open` / `cg_gif_add` / `cg_gif_close` / `cg_gif_abort`：更下一层的 GIF 编码器
  （**已有像素缓冲**时用）：把一串 24 位 RGB 帧写成动画 GIF。它是**纯 C、可单测**的，
  也是录屏那一半赖以工作的底座（详见 `runtime/src/kit/cg_kit_gif.c` 的文件头注释）。
- `cg_cancel_close` / `cg_tray_*`：关窗即藏进托盘；托盘左键截图、右键叫回。
- `cg_app_has_window`：抓屏 / 剪贴板 / 托盘 / 换边框一律先问，`--selftest` 才跑得干净。

## 五、容易踩的坑

- **画板是不透明自绘控件**：早先"canvas 盖在图上"会把图挡死 —— 所以补了"画板背景图"这条能力，而不是指望透明叠加。
- **WM_PRINT 必须接**：`PrintWindow` 发的是 `WM_PRINT` 不是 `WM_PAINT`，DefWindowProc 只画非客户区 —— 不接的话抓画板是**一张空图，而且 PrintWindow 照样返回成功**，连兜底抓屏都不会触发。
- **藏窗口后要隔一拍再抓**：立即抓会把"正在消失的自己"截进去，150ms 是经验值。
- **面板矩形只能在空闲态记**：overlay 的 move/size 事件会把面板位置覆盖掉，下次回面板窗口就飞了（代码里 `s_mode = MODE_IDLE` 必须**先于**挪窗口那几步）。
- **改边框只动 GWL_STYLE**：扩展样式（置顶 / 任务栏）各有各的接口，混着改会让 exstyle 的 TOPMOST 位和真实 z 序对不上；改样式必须带 `SWP_FRAMECHANGED`。
- **文字工具连点两下会落两个**（双击 = 两次 mousedown）：要改字就先撤销或换「清除」。
- **Esc / 回车要窗口在前台才收得到**：代码里 `cg_win_show()` 之后会调 `cg_win_focus()` 抢前台，
  但系统有权拒绝（日志会写一句"没抢到前台"）。所以**取消的鼠标退路永远留着**：框选态先框一块
  进标注态，再点工具条上的「取消」（框选态不放按钮 —— 画板铺满全屏，子控件接不到点击）。
- **抓选区前要隔一拍**：框选松手后"取消橡皮筋"那一帧还没重画出去，立刻抓会把黄框截进成品
  （已用 120ms 一次性定时器规避）。
- `cg_time_now` / `cg_str_format` 返回堆串，`cg_widget_set_text` 只拷一份，用完 `cg_free`。

## 六、能力边界（QQScreenShot 有、本示例明确不做）

- **OCR / 截长图**：它靠逆向的 QQ DLL 与外部引擎（PaddleOCR 等）；本工程不做，也不假装做。
  （**录屏**原本也在这一栏，2026-10-14 已实现为 GIF 录屏，见上面二、怎么用。）
- **GIF 的画质边界**：调色板是**固定**的（216 网页色 + 39 灰阶 + 1 透明位），不做按内容量化
  （八叉树 / 中位切分）。录界面操作、文字、纯色块几乎无损；**录照片 / 视频这类连续色调的内容
  会有色带**。要更好的画质就得换自适应调色板，那是改进项不是缺陷。
- **混 DPI 多屏的像素级对齐**：**多显示器是支持的**（口径 = 整个虚拟桌面，`cg_screen_size` /
  `cg_screen_origin` / `cg_screen_capture` 都按 `SM_XVIRTUALSCREEN` 系列算，副屏区域抓得到、
  坐标可以是负的）；但窗口的 DPI 上下文跟随主屏，**副屏缩放与主屏不一致时**，副屏上的
  物理↔逻辑换算会差一点，跨屏拖选的边缘可能差 1px 级。同 DPI 多屏（或单屏）没这个问题。
- **半透明遮罩**：runtime 没有分层窗口（`WS_EX_LAYERED`），选区外不会变暗 —— 靠黄色橡皮筋 + 尺寸提示说话。
- **独立贴图窗口**：框架只有一个顶层窗口，贴图与主面板**不能同时显示**（贴图态按 `Esc`/双击先回面板）。
- **高 DPI 边缘**：100% 缩放下逐像素对齐；150% 这类缩放下选区右/下沿可能有 ±1px 的取整缝（抓屏与铺窗走的是同一套换算，肉眼基本不可见）。

## 七、相关文档

- `examples\教学使用说明.md` → 「六、学完之后看什么」与「七、实用工具详解 · shottool」。
- `runtime/include/cstackgui.h` → 截图 / 屏幕抓取 / 全局热键 / 剪贴板图片四段注释。
