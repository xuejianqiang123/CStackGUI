# 深色主题终端：app.dark 默认深色 + 顶层命名样式表 + 运行时切换

> 目录 `examples\darkterm\`　编译 `build-app.bat darkterm`　运行 `examples\darkterm\darkterm.exe`　自检 `darkterm.exe --selftest` → 控件 26 个 / 回调 7 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
只讲**深色**这一件事：`.cgui` 的 app 里写 `"dark": true`，程序一打开就是深色（想跟系统走就写 `"dark": "auto"`）；配色集中在顶层 `styles{}` 命名样式表里；再用 `cg_theme_set_dark` 在运行时切换并立即重画。
关键认知是"**深色只换默认配色**"：窗口底色、没写颜色的文字会跟着变；控件自己写了 `fg` / `bg` 的**不跟随**。界面上还演示了进度条 / 圆环 / 开关 / 徽章这些"哪些跟随、哪些不跟随"的现场边界。

## 二、怎么用
1. 运行即深色。右上角「切换到浅色主题」按钮点一下就在深浅之间切，按钮文字与状态行会同步更新。
2. 切到浅色时观察：窗口底色、标题 / 状态行 / 底部说明这些**没写颜色**的文字跟着变；深色面板、日志视图、按钮、输入框、徽章**保持原样**。
3. 日志视图：`btn_fill`「追加几行」灌几行样例日志，`btn_clear`「清空日志」清空。
4. `sw_scroll`「自动滚动到底」开关控制追加后是否滚到底（关了就得自己拖）。
5. 底部 `edt_cmd` 输入框里输入（默认 `AT+VERSION`）回车或点「发送」；「保存日志」把日志存盘。
6. 界面上几组说明（`lab_tip`、`lab_foot1` / `lab_foot2` 等）就是这台面"哪些跟随、哪些不跟随"的注脚。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `btn_theme` | button | 运行时切换深 / 浅主题 |
| `edt_log` | edit（多行，只读） | 日志视图，fg/bg 写在 `styles.log` |
| `btn_fill` / `btn_clear` | button | 追加样例行 / 清空日志 |
| `sw_scroll` | switch | 自动滚动到底 |
| `prg_buf` / `ring_rt` | progress / progressring | 进度条 / 圆环 |
| `led_conn` | led | 状态灯 |
| `badge_dark` / `badge_self` | badge | 自绘徽章，bg/fg 自己写 |
| `edt_cmd` / `btn_send` | edit / button | 底部命令输入与发送 |

## 四、关键接口
- `cg_theme_set_dark` / `cg_theme_is_dark`：运行时切换主题、查询当前主题；语义与 `app.dark` 完全一致，**立即重画**。
- `cg_edit_scroll_end`：多行 edit 追加文本后要自己滚到底（没有 terminal 那套托管）。
- `cg_widget_set_text`：把日志与按钮文字刷成最新状态。
- `cg_app_has_window`：回调第一件事，自检模式下不动界面、不弹框。

## 五、容易踩的坑
- 深色只换运行时那套**兜底配色**，**写死了颜色的控件不跟随**；`progress` 的槽、`switch` / `checkbox` 的填充仍是浅色系。
- `listbox` / `table` / `tree` / `tabs` / 下拉列表 / 状态栏是系统经典控件，公开接口改不动它们的主题，所以本示例一个都没用（免得深色界面里摆一块白）。
- 多行 edit 没有 terminal 的"自动滚 + 按行着色 + 行数上限"：文本得自己攒（本示例用环形缓冲），文本多了要自己收着点。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · darkterm」。
- `docs\CGUI-FORMAT.md` → 第 1.1.1 节（`app.dark`）、第 2.2.1 节（`styles{}` 与 `style` 的三种形态）。
