# M3 自绘外观层（自绘后端雏形）

> 一句话：**把"系统画成什么样"从 Win32 原生外观里抽出来，用矢量绘制给出 Material 3 风格的圆角 / 主题色 / 状态**，
> 业务代码和 `.cgui` 一个字都不用改。

## 1. 为什么要做这一层

原生 Win32 控件（按钮、编辑框……）的绘制由系统承包，圆角、配色、悬停态大多数不生效
（`label` 能改文字色、`switch` 因为是 owner-draw 能改色，但按钮/编辑框等系统控件不给画）。
这让做出来的界面"很 Win32"，和现代 M3 观感有距离。

架构上这本来就是 `cg_backend_t` 该管的范畴：换一套外观 = 换一个 backend，上层（`cg_doc`/`cg_app`/业务）零改动。
`ARCHITECTURE.md §8.2` 的规划是「加 `cg_backend_d2d.c`，实现圆角/波纹/主题色」——
**但本机 MinGW-w64 的 include 里没有 `d2d1.h` / `dwrite.h`**，手写 D2D 的 COM vtable 绑定极易下标错位导致崩溃。
而 **GDI+ 已经是运行时依赖**（图片/画板在用，`-lgdiplus` 已链接），它同样能画抗锯齿圆角矩形、平滑文字。

**决策**：用 GDI+ 实现自绘外观层 `cg_skin.c`，作为自绘后端的第一个可替换内核。
把绘制集中在独立模块里，将来 D2D 头可用时只换 `cg_skin.c` 内部的绘制实现，接入点不变。
代价：GDI+ 是软件/老式矢量栈，不如 D2D 的 GPU 合成快、没有 D2D 的"特效基元"（真正的高刷波纹动画要等 D2D 后端）。

## 2. 接入点（不动上层）

- `runtime/src/win32/cg_win_ctl.c`：`style_of()` 里把 `button` 的样式从 `BS_PUSHBUTTON` 改成 `BS_OWNERDRAW`，
  WM_DRAWITEM 就会发到主窗口；
- `runtime/src/win32/cg_win_ownerdraw.c`：`draw_own_item()` 顶部加 `button` 分支，调 `cg_skin_draw_button()`；
- `runtime/src/win32/cg_win_event.c`：`cgw_FocusProc`（统一子类）里给 `button` 加悬停/按下状态跟踪
  （`WM_MOUSEMOVE`/`WM_MOUSELEAVE`/`WM_LBUTTONDOWN`/`UP` → 置 `hot`/`pressed` 并 `InvalidateRect`）；
- `runtime/src/win32/cg_win_window.c`：`cgw_win_init()` 里用
  `DwmSetWindowAttribute(DWMWA_WINDOW_CORNER_PREFERENCE=ROUND)` 给整窗圆角
  （`dwmapi.dll` 动态加载，老系统无此 API 自动跳过，零链接依赖）。
- `runtime/src/skin/cg_skin.c`（新增）：所有绘制逻辑，靠 `cg_internal.h` 的 `cg_widget_by_hwnd()` 反查控件。
- `runtime/src/skin/cg_skin_edit.c`：输入框（edit / multiedit / spinner）的圆角裁切 + 单行文字垂直居中，
  靠 `SetWindowSubclass` 挂子类实现（EDIT 不支持 owner-draw）。
- `runtime/src/core/cg_internal.h`：`cg_widget_t` 加 `hot` / `pressed` 两个 int 状态位（calloc 默认 0）。
- `build-runtime.bat`：不用改——它自动扫 `runtime\src\core|win32|skin\` 下的全部 `.c`，
  新文件放进 `skin\` 就行。

## 3. 状态模型

| 状态 | 来源 | 视觉 |
| --- | --- | --- |
| 正常 | 默认 | 中性表面填充（浅灰）或 `style.bg` 指定的主题色 |
| 悬停 | `hot`（鼠标进入） | 颜色按系数微亮 |
| 按下 | `pressed` 或 `ODS_SELECTED` | 颜色按系数微暗 |
| 聚焦 | `ODS_FOCUS` | M3 细描边 |
| 禁用 | `ODS_DISABLED` | 灰底灰字 |

颜色规则：
- 设了 `style.bg` → 实心主题色，文字按亮度选黑/白（深色底配白字）；
- 没设 `style.bg` → 中性表面色（`0xEFEFF1`），深色文字；
- 圆角半径 `style.radius`（未设则 8 逻辑像素），按 `app->scale` 缩放成物理像素。

## 4. 最小可跑闭环（本轮交付）

只覆盖 **`button`** 一种控件，证明整条通路：
原生控件仍由系统管输入/IME/无障碍（只替换"怎么画"），
GDI+ 画圆角矩形 + 主题色 + 状态，文字仍走 GDI `DrawTextW`（绘制于 GDI+ 之后，叠在同个 DC 上）。
GDI+ 不可用时自动回退到 GDI `RoundRect`，保证按钮永不消失。

验证：
```bat
build-runtime.bat
build-app.bat gallery
examples\gallery\gallery.exe --selftest examples\gallery\gallery.cgui
:: 截图肉眼确认按钮变圆角 + 主题色，悬停/按下有反馈
```

## 5. 路线图（后续，按价值排序）

1. **覆盖更多控件**：`checkbox`/`radio` 的圆角勾选、`slider`/`progress` 的圆角轨道、`edit` 的圆角边框与浮动标签。
2. **窗口级 M3**：圆角已在 `win_init` 做；再加 M3 表面色调、 elevation 阴影。
3. **波纹（ripple）**：按下时从点击点扩散的水波纹——GDI+ 只能做"近似"动画（WM_TIMER 驱动），真正流畅要靠 D2D 后端。
4. **`cg_backend_d2d.c`**：等工具链有 `d2d1.h`/`dwrite.h` 时，把 `cg_skin.c` 的绘制内核换成 D2D/DirectWrite，
   接入点（§2）不变，上层零改动。
5. **画布预览同步**：让 designer 的 `WidgetView` 也用同样的圆角/主题色预览，所见即所得。

## 6. 与画布 / 格式的关系

- `.cgui` 的 `style.radius` / `style.bg` / `style.fg` 已经是现成字段，自绘层直接读，无需改格式；
- 画布侧把 `radius` 暴露成可调属性（`Inspector`）、并把控件预览画成圆角，即完成"所见即所得"。
