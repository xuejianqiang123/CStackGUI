# 截图工具：托盘 + 全局热键的 QQ 风格截图链路

> 目录 `examples\shottool\`　编译 `build-app.bat shottool`　运行 `examples\shottool\shottool.exe`　自检 `shottool.exe --selftest` → 控件 22 个 / 回调 10 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

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

## 二、怎么用

1. 运行 → 面板（日志 + 状态栏）。点「开始截图」/ 按 `Ctrl+Alt+A` / 左键点托盘图标，任一入口触发。
2. 屏幕被整屏截图铺满，按住左键拖出选区（提示行实时报 `选区 W x H`），松手进标注；`Esc` 取消。
3. 标注条在选区下方：工具 / 颜色 / 粗细 / 文字内容 + 撤销 / 清除 / 完成 / 贴图 / 取消。
   换工具时日志会记一行「工具：椭圆」这类 —— 用来确认"你选的工具确实生效了"（事件 `select`）。
4. 「完成」= 存 PNG + 复制到剪贴板（可直接粘贴）+ 回面板；「贴图」= 同样存/复制但留在屏幕上；「取消」或 `Esc` = 不存图回面板。
5. 真正退出：「退出」按钮或 `Ctrl+Shift+Q` —— 先摘托盘图标再关消息循环。关窗口只是藏进托盘。

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
| `actions`（2 条） | action | `shot`（accel `Ctrl+Alt+A`，应用内兜底）/ `quit`（`Ctrl+Shift+Q`） |

## 四、关键接口

- `cg_hotkey_set` / `WM_HOTKEY`：注册全局热键 = 触发同名动作；**与动作 accel 的区别**（accel 在消息循环里由 TranslateAccelerator 吃，窗口失焦就不生效）。NULL 解绑全部，关窗自动收。
- `cg_screen_size` / `cg_screen_capture`：主屏逻辑尺寸、屏幕区域抓 PNG（逻辑像素入参，DPI 内部换算，越界自动夹进主屏）。
- `cg_win_set_border(app,"none"/"sizable")`：面板 ↔ 无边框 overlay 的开关；配套 `cg_win_set_pos`（**物理**像素）+ `cg_win_set_size`（**逻辑**像素）+ `cg_scale_px`（两者之间的换算）。
- `cg_widget_set_image(cv, path)`：设在 **canvas** 上 = **画板背景图**（本示例的立身之本）。
- `cg_widget_screenshot(cv, path)`：抓画板 = 成品（画板的 WM_PRINT 已接，PrintWindow 不会抓出空图）。
- `cg_clip_set_image(path)` / `cg_clip_get_image(path)`：图片写 / 读剪贴板（CF_DIB + CF_BITMAP 双格式）。
- `cg_set_timer(app,150,fn,app)`：一次性定时器 —— 藏窗口后**隔一拍再抓屏**，否则截到的是自己。
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

- **OCR / 录屏 / 截长图**：它靠逆向的 QQ DLL 与外部引擎（PaddleOCR、百度 API）；本工程不做，也不假装做。
- **多显示器**：只覆盖**主显示器**（`GetSystemMetrics(SM_CXSCREEN)` 口径），跨屏拖选不在范围内。
- **半透明遮罩**：runtime 没有分层窗口（`WS_EX_LAYERED`），选区外不会变暗 —— 靠黄色橡皮筋 + 尺寸提示说话。
- **独立贴图窗口**：框架只有一个顶层窗口，贴图与主面板**不能同时显示**（贴图态按 `Esc`/双击先回面板）。
- **高 DPI 边缘**：100% 缩放下逐像素对齐；150% 这类缩放下选区右/下沿可能有 ±1px 的取整缝（抓屏与铺窗走的是同一套换算，肉眼基本不可见）。

## 七、相关文档

- `examples\教学使用说明.md` → 「六、学完之后看什么」与「七、实用工具详解 · shottool」。
- `runtime/include/cstackgui.h` → 截图 / 屏幕抓取 / 全局热键 / 剪贴板图片四段注释。
