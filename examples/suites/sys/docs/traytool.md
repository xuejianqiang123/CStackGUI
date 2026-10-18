> **本文档是合并前独立工程 `examples\traytool\` 的原文**（2026-10-18 该工程已并入
> [`examples\suites\sys\`](../sys)：本工具的完整逻辑在 `modules\tr.c`，
> 编译 `build-app.bat suites\sys`，五个工具各占一个页签）。
>
> 文中「目录 / 编译 / 运行 / 自检」那几行命令是**合并前的口径，现已失效**（原工程目录已删）；
> 其余内容 —— 它演示什么、关键接口、容易踩的坑 —— 仍然逐条成立，对应 `modules\tr.c`。
# 托盘工具：没有主窗口也一直在的程序怎么活

> 目录 `examples\traytool\`　编译 `build-app.bat traytool`　运行 `examples\traytool\traytool.exe`　自检 `traytool.exe --selftest` → 控件 6 个 / 回调 1 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
一类"关掉窗口也还在跑"的程序：启动就建托盘图标；点窗口右上角关闭只是**藏进托盘**（进程继续跑），真正退出走菜单动作或 `Ctrl+Shift+Q`。五条命令写成 `.cgui` 的 `actions`（各带 accel 快捷键），C 侧**只有一个回调 `on_action`** 服务全部命令，靠 `cg_action_name(self)` 分辨是哪一个 —— "同一个 handler 服务多个动作"是这套 API 的常用姿势。附带剪贴板、整窗截图存 PNG、运行时深浅主题切换。
有意留的简化：右键点托盘**不会自动弹菜单**（框架没有"在光标处弹菜单"的接口），示例改成把窗口叫回前台，再在窗口里走菜单。

## 二、怎么用
1. 运行后窗口显示中。点右上角关闭 = 藏进托盘（进程还在跑），日志会写明"窗口已隐藏到托盘，进程还在跑"。
2. 托盘图标：左键单击 = 显示 / 隐藏，左键双击 = 叫回窗口，右键 = 拉出窗口（再走菜单）。
3. 菜单「程序」：显示 / 隐藏窗口（`Ctrl+Shift+H`）、切换深色 / 浅色（`Ctrl+Shift+D`）、退出（`Ctrl+Shift+Q`）。
4. 菜单「操作」：时间戳写进剪贴板（`Ctrl+Shift+C`）、窗口截图存 PNG（`Ctrl+Shift+S`）、清空日志、弹个托盘气泡。
5. 截图 PNG 落在用户数据目录（`cg_app_data_dir`），文件名 `shot_<时间戳>.png`；存图失败只写日志、不弹框。
6. 真正退出：菜单「程序 → 退出」或 `Ctrl+Shift+Q` —— 会**先摘掉托盘图标**再关消息循环。
7. `lab_state` 与状态栏实时显示「窗口状态 / 主题 / 截图张数」。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `menu_main` | menu | 程序 / 操作 / 帮助 三组菜单 |
| `sb_main` | statusbar | 就绪 / 主题 / 截图张数 |
| `lab_state` | label | 窗口状态 + 主题 + 截图数 |
| `lab_tip` / `lab_tip2` | label | 关窗即最小化、托盘操作说明 |
| `lst_log` | listbox | 操作日志（追加即滚到底） |
| `actions`（5 条） | action | toggle_win / toggle_dark / copy_time / shot / quit |

## 四、关键接口
- `cg_tray_set` / `cg_tray_balloon` / `cg_tray_remove` / `cg_tray_action`：建 / 弹气泡 / 摘 / 处理托盘图标（退出前必须 `cg_tray_remove`）。
- `cg_cancel_close` / `cg_win_show` / `cg_win_set_topmost`：把关闭拦截成"藏进托盘"，再从托盘叫回来并置顶。
- `cg_win_screenshot`：整窗截图存 PNG。
- `cg_app_data_dir` / `cg_path_join`：拼出截图的落盘路径。
- `cg_clip_set_text`：时间戳写进剪贴板。
- `cg_theme_set_dark` / `cg_theme_is_dark`：运行时切深浅主题。
- `cg_action_name`：一个 `on_action` 里分辨是哪个动作。
- `cg_app_has_window`：凡需窗口 / 托盘 / 对话框 / 截图处先问，自检才跑得干净。

## 五、容易踩的坑
- 退出前**必须** `cg_tray_remove`，少这一句托盘里会留一个死图标。
- 右键点托盘不会自动弹菜单，改把窗口叫回前台。
- 自检（`--selftest`）不开窗、不建托盘、不弹框：`on_action` 等回调里凡是碰界面 / 托盘 / 截图的地方都先问 `cg_app_has_window`。
- `cg_time_now` / `cg_str_format` 返回的是堆串，`cg_widget_set_text` 只拷一份、不接管所有权，用完必须 `cg_free`。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · traytool」。
- `docs\CGUI-FORMAT.md` → 第 2.7 节（菜单 `nodes`）、第 2.8.1 节（`actions` / `action`）。
