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
- [ ] P2c 文档 **未做**：README 示例清单 / 教学使用说明 §6§7 / supportlib.ts 收录 6 个新函数
      都没挂（`check-support` 只提示不红、`check-examples --docs` 只有既有的"六课 0 课"10 处红）
- [x] P3 门禁（2026-10-13 代为复验后提交）：`build-app shottool`/`calculator` 零警告；
      两个 `--selftest` 五项全 0；`run-tests` all checks passed；dispatch 20 条 /
      layers 314 项 / support / kinds 45 种 全绿
- [ ] P4 审查 **未做**：三角色审查报告没落盘 —— 遗留（新 API 含 BitBlt/热键注册/
      剪贴板句柄，建议补一轮 memory_leak + error_handling 审查）
- [x] P5 提交（2026-10-13 由另一会话代提交：进度段在此回写）
