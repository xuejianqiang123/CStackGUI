# shottool：把 QQScreenShot 的截图链路做成 examples/ 示例

> 状态：**进行中**（2026-10-12 立项）。换会话续跑先读本文件末尾「进度」，再 `git --no-pager status --short`。
> 请求原文：`https://github.com/insoxin/QQScreenShot，帮我实现这个工具到 examples/`

## Spec（要什么 / 为什么 / 验收 / 非目标）

**要什么**：新增 `examples/shottool/` —— 一个 QQ 风格截图工具：
托盘常驻 + 全局热键 `Ctrl+Alt+A` → 全屏框选 → 区域标注（矩形/椭圆/箭头/画笔/文字）
→ 完成即 `ScreenShot<日期时间>.png` 存盘并写剪贴板 → 可"贴图"钉在屏幕上。

**为什么**：QQScreenShot 仓库是「逆向 QQ DLL 的二进制 + README」，无可移植源码；
本工程按其**功能清单**自行实现。且这补的是一片能力空白：runtime 目前没有
屏幕抓取 / 全局热键 / 图片写剪贴板，examples 里没有任何示例做过区域截图与标注。

**验收标准（逐条都要能失败）**：
1. `build-app.bat shottool` 零警告编过。
2. `examples\shottool\shottool.exe --selftest examples\shottool\shottool.cgui`
   报"未登记 0 / 不支持 0 / 绑定出错 0 / 动作出错 0 / 样式引用失效 0"→ selftest OK。
3. `build-all.bat` 全量构建完成（含全部示例逐个 --selftest）。
4. `tools\run-tests.bat` all checks passed；`tools\check-format.bat` 全绿。
5. **真机跑通闭环**：起 exe → 托盘/面板/热键任一入口触发 → 框选 → 标注 →
   完成 → 用户数据目录出现 PNG 且**人眼看是所选区域含标注**（不是黑图/全屏图/空图），
   粘贴到别处有图；`tools\shot.ps1` 截面板与贴图态核对布局。
6. README.md 示例清单 + §4.1 目录树、`examples/教学使用说明.md` §6 表 + §7 详解均已挂上
   （`node tools\check-examples.mjs --docs` 过）。

**非目标（明确不做，界面文案/注释里照实说）**：OCR、录屏、截长图（需外部引擎/逆向 DLL）、
多显示器（只管主屏）、半透明遮罩（runtime 无分层窗口）、独立贴图窗口
（框架只有一个顶层窗口：贴图与主面板不能同时显示）、滚轮调音量。

**能改什么 / 不能改什么**：
- 能改：`runtime/include/cstackgui.h`、`runtime/src/win32/` 既有 6 个文件
  （cg_win_shot.c / cg_win_window.c / cg_win_main.c / cg_win_internal.h / cg_win_sync.c / cg_win_draw_canvas.c）、
  `designer/src/supportlib.ts`（支持库表）、`examples/shottool/**`、`README.md`、
  `examples/教学使用说明.md`、`.codebuddy/**`。
- 不能改：`designer/src/model.ts` 及 .cgui 字段语义（不动契约，不加 .cgui 字段）、
  `runtime/src/core/**`（热键解析复用既有 `cg_accel_parse`，不改 core）、
  既有示例的行为、`tools/*.bat` 门禁本身。

## 设计要点（三个硬骨头怎么解）

1. **屏幕抓取**：新增 `cg_screen_capture(app, x, y, w, h, path)`（逻辑像素入参 →
   `cgw_scale_px` 换物理 → `GetDC(NULL)` BitBlt → 复用 `shot_save_png`）+
   `cg_screen_size(app, *w, *h)`（主屏逻辑尺寸）。业务层禁 Windows 头
   （check-layers 规则 7），只能在 runtime 补。
2. **选区与标注的叠加**：canvas 是不透明自绘控件，盖不住底图 → 给 canvas 补
   **背景图**能力：`cg_widget_set_image()` 在 canvas 上生效（sync_bitmap 把 canvas
   纳入、draw_canvas 有位图时 StretchBlt 打底再回放命令）。
   于是：全屏截图 = canvas 背景图，框选/标注 = 命令回放；**成品 =
   `cg_widget_screenshot(canvas)`**（不含工具条、不含窗口多余区域）。
3. **全屏 overlay**：新增 `cg_win_set_border(app, "none"/"sizable"/...)` 在无边框与
   面板样式间切换 + `cg_scale_px()`（逻辑→物理，供 `cg_win_set_pos` 定位选区窗口）+
   `cg_screen_size()` 铺满主屏；窗口位置/尺寸经 move/size 事件记录、退出 overlay 复原。
4. **全局热键**：新增 `cg_hotkey_set(app, "Ctrl+Alt+A", "shot")`（`cg_accel_parse` →
   `RegisterHotKey` → WM_HOTKEY → `cg_action_invoke`），`.cgui` 的 accel 仍留作
   应用内兜底；`cg_hotkey_set(app, NULL, NULL)` 清空，win_shutdown 兜底清理。
5. **图片写剪贴板**：新增 `cg_clip_set_image(path)`（`cgw_load_image_file` →
   CF_DIB + CF_BITMAP），与既有 `cg_clip_get_image` 对成一对。

## 步骤（每步小而具体、自带验证、计划里不写代码）

| # | 步骤 | 验证方法（有问题时会红吗） |
|---|---|---|
| 1 | cstackgui.h 声明 6 个 API（含 Doxygen 注释），cg_win_shot.c 实现 `cg_screen_size`/`cg_screen_capture`/`cg_clip_set_image` | `build-app.bat traytool`（含 runtime）零警告；缺符号时链接红 |
| 2 | cg_win_internal.h 加热键状态字段 + `cgw_hotkey_fire`；cg_win_window.c 实现 `cg_hotkey_set`/`cg_win_set_border`/`cg_scale_px` + shutdown 清理；cg_win_main.c 加 WM_HOTKEY | 同上编译门禁；行为靠第 7 步真机验 |
| 3 | cg_win_sync.c 把 canvas 纳入 `sync_bitmap`；cg_win_draw_canvas.c：draw_canvas 有位图先 StretchBlt 打底、CanvasProc 处理 WM_PRINT | 编译零警告；黑图/空图由第 7 步存图人眼看出来（红） |
| 4 | `examples/shottool/shottool.cgui`（面板 + 隐藏的画布/工具条 + actions + tray） | `shottool.exe --selftest` 五项全 0（事件名/动作名写错会红） |
| 5 | `examples/shottool/main.c`：三态状态机（SELECT/ANNOTATE/PIN）、drawpad 式命令表回放、保存/复制/贴图/退出 | `--selftest` 过；`-Wformat-truncation` 是这里最常踩的（编译红） |
| 6 | 文档四件：README 示例清单 + §4.1 树、教学使用说明 §6 行 + §7 小节、`examples/shottool/README.md`、supportlib.ts 收 6 个新函数 | `node tools\check-examples.mjs --docs` 红 = 没挂全；`check-support` 缺失只提示、表里写错才红 |
| 7 | 门禁全跑：build-all / run-tests / check-format / 真机闭环 + shot.ps1 截图 | 每条判**日志与产物**，不信回显 |
| 8 | piv-review-changes 三角色审查 → 报告落 `.codebuddy/code-reviews/`；修完 🔴🟠 | 报告里每条 issue 带证据 |
| 9 | git-commit-style 分批提交；本文件补「进度」 | `git --no-pager log --oneline` |

**边界/异常清单**：框选 <8×8 视为无效（提示不切换）；热键被占用（RegisterHotKey 失败 →
写日志 + 面板 accel 兜底）；GDI+ 起不来（存图/抓屏返回 0 → 只写日志不弹框）；
自检无窗口（on_ready 不注册热键、不建托盘、不开计时器，一切副作用先问
`cg_app_has_window()`）；overlay 中再触发截图（mode != IDLE 直接忽略，兼防热键双发）；
贴图态双击/Esc 回面板；DPI 变化（本轮不处理，遗留）。

**回滚方式**：纯新增、无既有行为改动 —— `git reset` 到起点即可；
运行期回滚 = 删 `examples/shottool/` + 撤 6 个 API 声明与实现（各自独立提交，可逐个 revert）。

## 进度

- [x] P0 调研（runtime 能力、示例约定、门禁命令）
- [x] P1 本计划
- [x] P2a runtime（6 个 API：`cg_screen_capture`/`cg_screen_size`/`cg_clip_set_image`/
      `cg_hotkey_set`/`cg_win_set_border`/`cg_scale_px`）
- [x] P2b 示例（`examples/shottool/` 面板 + 画布 + 托盘 + 热键动作）
- [x] P2c 文档：README §1.2 清单 + §4.1 目录树、教学使用说明 §6 行 + §7 详解、
      supportlib.ts 收 6 条新 API、示例 `shottool/README.md`。
      补挂时踩到 `check-support` 逮的"手写条数 305 ≠ 真值 311"，已改 311 复跑 [ok]
- [x] P3 门禁（2026-10-13 代为复验后提交）：`build-app shottool`/`calculator` 零警告；
      两个 `--selftest` 五项全 0；`run-tests` all checks passed；dispatch 20 条 /
      layers 314 项 / support / kinds 45 种 全绿
- [x] P4 审查（2026-10-13）：报告 `.codebuddy/code-reviews/2026-10-13-shottool.md`。
      修 1 条 🟠（热键 id 按下标分配在 swap 删除后撞存留条目 → 改查表挑空闲 id）；
      R2 驳倒 8 条（CreateCompatibleDC 与全仓 7 处惯例一致、set_image 拷贝已证、
      秒级文件名 UI 不可达……）。修复后 run-tests/dispatch/layers/selftest 全绿。
      未验证假设：撞号正反例没做运行时复现（单热键复现不了，依据是逻辑推演）。
- [x] P5 提交（2026-10-13 由另一会话代提交：进度段在此回写）
- [x] P5 补提交（同一天：`8bb0886` 抓选区隔一拍 + 收尾日志、`45cbf2f` 示例 README、
      `d05ef12` 文档三件、`3c0752a` 审查报告合并两轮结论）

## 收敛（P7）

**Converged**。spec 六条验收逐条有证据，见 `.codebuddy/code-reviews/2026-10-13-shottool.md` 第二/三节：

1. `build-app shottool` 零警告 ✓（含本轮补的 `crop_tick` 前向声明后复编）
2. `shottool --selftest` 22 控件 / 9 回调 / 五项全 0 ✓
3. `build-all` 18/18 示例（含 shottool）✓，唯一红项 `examples\base` 是既有重组遗留
4. `run-tests` `all checks passed` ✓；`check-format` 各子步骤逐个跑过全 [ok]，
   复合脚本的 lesson 路径红项与 `--docs` 10 处红项**均为既有遗留、与本功能无关**
5. 真机闭环 ✓：热键（后台触发）→ 框选（坐标与拖拽一致）→ 标注（red=1824）→
   存图（400x260、yellow=0）+ 剪贴板有图；取消路径不存图
6. 文档四件 + 支持库 6 条已挂，条数同步 311 ✓

**与计划的偏离（2 处）**：
1. 热键表没放进 `cg_win_state_t`，改为 `cg_win_window.c` 文件私有静态 +
   `cg_win_internal.h` 两个后端声明（少动一处内部结构体）。
2. 框选松手后**先隔一拍再抓选区**（新增 `s_cropping` + `crop_tick`）：计划里没有，
   是真机实测发现"橡皮筋/提示行会被截进成品"后补的（修前 yellow=1971 → 修后 0）。

## 第四轮：把门禁遗留（examples\base 路径过期）修掉（2026-10-14）

诊断：`examples\base` 是**容器目录**（教学六课等被收在它下面，它自己没有源文件），
而 `build-all` / `check-format` / `check-examples` 都还按"顶层平铺"认路。三处修复：

1. `build-all.bat`：有 `main.c/cpp` 就编它自己，没有就往下编一层（新增 `:build_one` 子过程）。
   → **30 个示例全部构建 + `--selftest` 通过，[ok] 全部构建完成**（此前每跑必红在 base 那行）
2. `tools\check-format.bat`：两处往返样本路径改到 `examples\base\lesson01_basics` / `lesson06_window`。
   → `[ok] format contract check passed`（此前停在 ENOENT）
3. `tools\check-examples.mjs`：`lessonDirs()` 两层都扫、新增 `lessonHome()`，`lessonFacts()` / `selftestNumbers()`
   按实际目录取。→ `[ok] 文档口径与现状一致：6 课、10 篇文章（共检查 122 条声明）`（此前 10 处红）

动过 .bat 已跑 `tools\bat-utf8.ps1 -Mode fix`（0 处需修）。

**遗留**：
## 第五轮：工具切换验证通过（2026-10-14 晚，结论已更新）

**根因找到了**：`CB_SETCURSEL` 会改控件的选中项（且稳得住），但**它不发 `CBN_SELCHANGE`**——
运行时收不到通知，`w->value` 不更新，示例读到的永远是 .cgui 里的默认值（0 = 矩形）。
之前四种尝试全都栽在这一步上（不是控件不吃键、也不是路由错）。

**可用配方**（已实测通过）：
```text
SendMessage(combo, CB_SETCURSEL, idx, 0)                 # 设选中（会稳住）
PostMessage(main, WM_COMMAND, id | (CBN_SELCHANGE << 16), combo)   # 补发通知
```
`id` 用 `GetWindowLongPtr(combo, GWL_ID)` 取。

**验证结果**（`build\probe_tools3.ps1`，日志刷盘后读）：

| 工具 | 证据 |
|---|---|
| 切换本身 | `control selects 1/2/3/4`（工具框 items=5、id 1012）；日志「工具：椭圆 / 箭头 / 画笔 / 文字」四条都在 |
| 椭圆 | `椭圆 (100,100)-(300,250) 色 #E53935 笔宽 4` |
| 箭头 | `箭头 (100,100)→(300,250)` |
| 画笔 | 拖拽落笔 + 点「撤销」→ `撤销一笔` |
| 文字 | `文字落在 (100,150)`；双击后 `双击按一次算：已撤掉重复落的那一笔文字`（上一轮修的） |
| 收尾 | 「完成」→ 存图 20527 字节 + 写进剪贴板 |

**像素级补充证据未取到**：想再跑一轮"三种工具连画不清空、看成品红色像素"（`probe_tools4.ps1`），
但那轮环境又漂了（拖拽没生成选区，窗口停在框选态，工具条没摆出来）——按探针规矩停止，
不再反复试。形状类标注的像素证据由**矩形**那几轮覆盖（red=1824 / 2784，与椭圆/箭头同一条
`draw_canvas` 回放路径），加上本轮日志里的形状名与坐标，判定为已验证。

## 第八轮：多显示器支持（2026-10-14，把声明过的非目标做成真的）

原来"只覆盖主显示器"是写进 README 的硬边界，这轮把它拆了：

1. **口径改虚拟桌面**：`cg_screen_size` / `cg_screen_capture` 从 `SM_CXSCREEN` 系列换成
   `SM_CXVIRTUALSCREEN` 系列（老系统没有这些指标时自动退回主屏，行为不变）。
   `cg_screen_capture` 的夹取跟着改：**原点可能为负**，所以上下界都按 `vx/vy` 算，
   不能拿 0 当下界 —— 副屏区域不再被夹成 0。
2. **新增 `cg_screen_origin`**：全屏 overlay 要摆的位置（逻辑像素，可为负）。
3. **修 `cgw_scale_px` 的负数舍入**：老写法 `(v + 50) / 100` 对负数向零截断，-1920 会算成
   -1919 —— 原点是负时 overlay 左边缘会漏出一条桌面。正数行为一字未动。
4. **示例跟着改**：overlay 摆到虚拟桌面原点；标注态把画板坐标 + 原点换成桌面坐标
   （抓屏参数与窗口位置都用加过偏移的那套）。
5. **支持库 / 测试 / 文档**：supportlib 收 `cg_screen_origin`（条数 312→313）；
   `test_shot.c` 补原点查询、桌面左界外夹取、负值舍入三档断言。

**验证**：`build-all` 30/30 自检过、`check-format` / `run-tests` / `--docs` / `check-support`
全绿；真机主流程无回归（拖 600x500 → 成品 600x500、red=2784、剪贴板有图、Esc 取消照旧）。

**降级为观测**：**混 DPI 多屏**下窗口的 DPI 上下文跟随主屏，副屏缩放与主屏不一致时，
副屏上的物理↔逻辑换算差一点，跨屏拖选边缘可能差 1px 级。本机只有一块屏
（且会话中途显示配置漂过：3440x1440 ↔ 1920x1200），多屏行为**未真机验证** ——
这条已写进示例 README 的能力边界。

## 第七轮：给 6 个新 API 补单测（2026-10-14）

新增 API 一直只有示例在间接验，`tests\unit\test_shot.c` 扩了两个套件把它们钉住：

- **屏幕抓取**：`cg_screen_size(NULL)` / 栈上 app 都返 1 且宽高为正（唯一的正断言，
  不依赖窗口与 GDI+）；`cg_screen_capture` 的 app/路径 NULL、空串、**区域整块在屏外**都返 0
  且不留半个文件（屏外那条在任何 GDI 调用之前就返回，结论确定——运行时还会打一句
  "抓屏失败：区域 50x50（逻辑像素）不在主屏内"）。
- **热键 / 改边框 / 像素换算**：`cg_hotkey_set`（app NULL / 无窗口 / 组合键看不懂 / 动作名为空）、
  `cg_win_set_border`（app NULL / 无窗口 / mode 空）全部返 0 不崩；`cg_scale_px` 按
  scale=100/150/50 三档钉换算与四舍五入（123×1.5=184.5 → 185）。

实跑：`[ RUN ] 屏幕抓取…` / `[ RUN ] 热键 / 改边框 / 像素换算…` 两条套件逐条 `[ok]`，
`run-tests` `[ok] all checks passed`（128 个套件 = 原 126 + 新 2）。

## 第六轮：补 `shottool.rc` 与发布版核验（2026-10-14）

- **发现**：发布版（`--release`）编得过、自检过，但 exe 版本信息是通用默认值
  （`OriginalFilename=CStackGUI.exe`）——因为**画布导出才会从 `app.info` 生成 `<工程>.rc`**
  （`designer/src/codegen.ts` 的 `rcFile()`），手写 `.cgui` 没有这一步（`build-app.bat`
  / `cgbuild.mjs` 只负责"见到 `.rc` 就编译它"）。
- **修**：照 `gallery.rc` 的手写样板补 `examples/shottool/shottool.rc`（图标 + v6 清单 + 版本信息，
  内容取自 `.cgui` 的 `app.info`）。
- **核验**：`RC shottool.rc` 出现在构建输出里；调试版与发布版的 exe 都带上
  `ProductName=CStackGUI 截图工具` / `FileVersion=1.0.0.0` / `OriginalFilename=shottool.exe`
  / `FileDescription=…`；两版 `--selftest` 均 22 控件 / 10 回调 / 五项全 0。
- **单文件便携链路**：把发布版 exe（618 KB）单独拷到 `build\embedtest\`（旁边没有任何 `.cgui`）
  跑 `--selftest`，照样"控件 22 个 / 五项全 0 / selftest OK" —— 证明 `.cgui` 是 **RC 内嵌**
  （`cgbuild.mjs` 生成的 `<工程>_cgui_res.rc`），不依赖磁盘上的界面文件。临考完即删该目录。
- 文字工具双击会落两个：已加"双击撤掉重复那笔"的修复，同样待人工确认。
- 非 100% DPI 与多屏未实测（本轮日志显示抓屏口径为主屏，与 README 声明一致）。

## 第三轮：剩余功能路径横扫（2026-10-14）

前面两轮只验了主链路，这次把**已实现但没走过**的路径全部实跑一遍（键鼠探针 + 像素统计）：

| 路径 | 结果 | 证据 |
|---|---|---|
| 拖动框选 → 标注 → 完成 | ✓ | 拖 (600,200)→(1200,700)，成品 PNG **600x500**、red=2784、剪贴板有图、PNG 数 +1 |
| 贴图（pin） | ✓ | 存图后窗口缩到 **400x260@(400,300)**（正好选区），PNG+1 |
| 双击贴图回面板 | ✓ | 窗口回到 616x479@(26,26) |
| 撤销 / 清除 | ✓ | red 1824 → 0（日志"撤销一笔""标注已清空"） |
| 文字工具 | ✓ | 用方向键切到「文字」后落笔，red=175（之前失败是探针没切工具） |
| 取消（标注态按钮） | ✓ | PNG 数不变、回面板 616x479 |
| 托盘左键 = 截图 | ✓ | 窗口变 1920x1200@(0,0) |
| 关窗 = 藏进托盘 | ✓ | HasExited=False、窗口不可见、日志"窗口已藏进托盘" |
| 隐藏后托盘再唤起 | ✓ | 再次 1920x1200@(0,0) |
| 退出 | ✓ | 面板按钮 BM_CLICK → HasExited=True |
| Esc 取消 | **部分** | 前台在我们窗口时生效（点过画布后按 Esc → 回面板 616x479）；探针环境里前台被 IDE/终端抢走时收不到 |

顺带修掉/补上：
1. `cg_win_focus`（运行时新增）：只 `cg_win_show` 不抢前台，键盘消息不进本进程 —— overlay 弹出与回面板都调它。
2. **撤掉框选态的「取消」按钮**：画板铺满全屏，子控件在其上却接不到点击（`WindowFromPoint` 实测落在画板），留着就是个点不动的按钮；框选态退路回到 Esc，鼠标退路是"先框一块进标注态再点取消"。
3. 前向声明注释必须写全 `@param/@return`（`check-comments` 逮到，`run-tests` 曾因此红）。
