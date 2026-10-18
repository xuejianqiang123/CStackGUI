# 入门 06 · 主窗口与窗口事件：窗口配置、窗口事件、程序信息

> 目录 `examples\lessons\lesson06_window\`　编译 `build-app.bat lesson06_window`　运行 `examples\lessons\lesson06_window\lesson06_window.exe`　自检 `lesson06_window.exe --selftest examples\lessons\lesson06_window\lesson06_window.cgui` → 控件 13 个 / 回调 4 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 这一课讲什么
前五课都在讲控件，这一课讲**窗口本身**：它的配置写在 `.cgui` 的 `app` 里，它的事件写在 `app.events` 里，它的"程序信息"写进 exe 的版本信息。窗口也是一个"控件"（伪控件 id 固定 `__window`），事件和控件事件共用同一套 `CG_BIND` 机制。

## 怎么跑
1. 在项目根编译：`build-app.bat lesson06_window`，双击 `examples\lessons\lesson06_window\lesson06_window.exe`。
2. 顶部一组按钮试窗口操作：关于 / 居中 / 总在最前 / 隐藏 1 秒再显示；右边那张"窗口事件流水"列表会随操作追加。
3. 拖窗口、敲键盘、点客户区、最小化 / 恢复，看被本课演示的那几种窗口事件写进流水表。
4. 勾上「关窗前问一句」再关窗口，看 `cg_cancel_close()` 是怎么把这次关闭拦下来的。

## 关键控件一览（本课 13 个控件 / 4 个回调）
| 控件 | 类型 | 看什么 |
| --- | --- | --- |
| 顶部信息分组框 | `group` | 产品名 / 版本 / 作者，由 `cg_app_info()` 从 exe 版本信息读回 |
| 一排操作按钮 | `button` | 关于 / 居中 / 总在最前 / 隐藏再显示 |
| 事件流水列表 | `listbox` | 窗口事件逐条追加 |
| 关窗询问勾选框 | `checkbox` | 勾上后关窗口会被 `cg_cancel_close()` 拦下 |
| `sb_main` | `statusbar` | 三格状态栏 |

## 重点看哪几处
- **窗口事件不参与 `--selftest` 合成触发**（自检不开窗），只统计控件回调 —— 所以这一课的回调数最少，看点在真跑起来之后。
- `app.info` 会真的进 exe 的 `VERSIONINFO`（右键 exe → 属性 → 详细信息能看到），运行时用 `cg_app_info(app, "product")` 读回。
- `idle` 触发非常频繁，别在里面做耗时活。

## 详细讲解
见 `docs\articles\06-主窗口与窗口事件.md`（含窗口事件全表），以及 `examples\教学使用说明.md` 的「入门 06」小节。
