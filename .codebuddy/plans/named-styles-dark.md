# 实现计划 + **逐字契约**：命名样式（`styles{}`）与深色模式

> 本轮四个域（运行时 / 画布 / 示例 / 文档）的**唯一契约**。
> **与落盘代码不一致就报回来，以代码为准**；不要照自己理解改这个文件（我 own）。

## 0. 要解决的问题

现在**每个控件一份 `style` 对象**：同一套字号/颜色在几十个控件上各写一遍，改一次字号要改几十处
（`examples\netterm` 里 `"font": "Microsoft YaHei UI", "size": 9` 重复了十几遍）。
另外整个工程只有浅色：工控/嵌入式场景常常长时间盯屏，需要深色。

## 1. `.cgui` 契约：顶层 `styles{}`（与 `app` / `widgets` 同级）

```json
"styles": {
  "title": { "font": "Microsoft YaHei UI", "size": 12, "bold": true, "fg": "#1D1B20" },
  "log":   { "size": 9,  "fg": "#00FF00", "bg": "#101010" }
}
```

- 键 = **样式名**（字符串，非空；成对出现的是第 1 节那个 `cg_style_t` 的字段子集）。
- 字段同上：`font` / `size` / `bold` / `fg` / `bg` / `radius` / `align`。**没写的字段不参与**（见 §2 合并规则）。
- **容错**：名字为空、值不是对象、字段值非法 —— 一律**跳过该字段/该条**，不报错、不中断加载
  （与 `actions` 一个口径）。
- 重名：后写的**覆盖**前写的（与 `actions` 一致）。

## 2. 控件怎么引用：`style` 支持**三种形态**（都要支持，且**向后兼容**）

| `.cgui` 写法 | 含义 |
| --- | --- |
| `"style": { "size": 11 }` | **内联**（现状，行为不变） |
| `"style": "log"` | **纯命名引用**：整个样式来自 `styles.log` |
| `"style": { "use": "log", "size": 11 }` | **命名 + 局部覆盖**：先取 `styles.log`，再用本对象里**写了的**字段覆盖 |

合并规则：**先命名样式，后内联字段**（内联优先）。`use` 指向不存在的名字 → **当没有 `use`**
处理（只用内联字段），不报错。

> 关键实现点：合并要求"**知道这个控件到底写了哪几个字段**"。现有 `parse_style()` 会无条件铺默认值，
> 分不出"没写"与"写成默认值" —— 所以解析时要带一个**字段存在位掩码**，只把写了的字段覆盖上去。

## 3. 深色模式契约

开关两处，语义相同：

```json
"app": { "dark": true }          /* 也可以写 "auto"：跟随系统的应用主题 */
```
```c
int cg_theme_set_dark(cg_app_t *app, int on);   /* 运行时切换；on=1 深色，0 浅色 */
int cg_theme_is_dark(const cg_app_t *app);      /* 现在是深色吗 */
```

`"auto"` 的判据：读 HKCU `Software\Microsoft\Windows\CurrentVersion\Themes\Personalize`
的 `AppsUseLightTheme`（0 = 深色）✓。

### 覆盖范围（这是**能力的边界**，不是缺陷 —— 必须照实写进文档）

| ✅ 跟随深色 | 怎么做到 |
| --- | --- |
| 窗口背景、`panel`/`card`/`group`/`box`/`shape` | 窗口与自绘面板的底色 |
| `label`/`button`/`iconbutton`/`menubutton`/`checkbox`/`radio`/`switch`/`colorpicker`/`badge`/`tag`/`rate`/`pagination` | 这些在 `cg_skin` 里本来就是**自绘**的（backend 自己画） |
| `edit`/`richtext`/`terminal` | `WM_CTLCOLOR*` 给深底浅字（终端本来就可着色） |
| `progress`/`progressring`/`led`/`meter`/`chart`/`canvas`/`image` | 自绘，背景走深色 |
| `listbox` / `table`（内部是 ListView）/ `tree` | `SetWindowTheme(h, L"DarkMode_Explorer", NULL)`（Win10 1809+ 有效；更早的系统忽略） |

| ❌ **不跟随**（系统控件没有公开接口） | 说明 |
| --- | --- |
| `tabs` / `combo` 的下拉列表 / `monthcal` / `datepicker` / `statusbar` / `toolbar` / `slider` / 滚动条箭头 | 它们保持系统浅色 |

> 这条边界**不是我偷懒**：这些是 Windows 自带的经典控件，主题由系统内部绘制，公开 API 改不动。
> 想要"完全一致的深色"只能把它们全部重画 —— 那是另一个量级的工程。
> 文档里**必须**列清这张表（否则用户会以为深色是半成品 bug）。
> 画布上给 `app.dark` 勾选框时也要注明这一点。

行为细节：
- 深色**只改默认值**：控件自己写了 `fg`/`bg`（内联或命名样式）就**以它为准** —— 深色不做覆盖。
- `WM_THEMECHANGED`（用户改系统主题）时重新应用一次。
- `app.dark` 缺省 = 浅色（**不改变现有工程的外观**）。

## 4. 明确不做（v1）

- 不做完整 CSS：**没有选择器、没有继承、没有层叠、没有优先级计算** —— 只有"名字引用 + 局部覆盖"
  这一层（第一轮就否掉了 CSS 引擎，理由：为一个 `.btn:hover > .icon` 引入解析器+特指度计算，成本远大于收益）。
- 不做运行时改命名样式表（`styles{}` 是加载期读的；运行时要改单个控件仍用 `cg_widget_set_style` 那条路）。
- 不做每控件多套主题切换（只有"浅色 / 深色"两态）。
- 不重画系统控件（见 §3 的 ❌ 表）。
- 不做"深色下自动选对比色"之类的智能配色（用户写什么就是什么）。
- **画布不做深色预览**（画布自己是浅色 UI；预览等于把整个自绘皮肤在 Web 侧再实现一遍）。
  画布只提供：样式编辑器 + 控件的样式选择 + `app.dark` 勾选框 + 一句边界说明。

## 5. 域划分（互不重叠）

| 域 | 谁 | 文件 |
| --- | --- | --- |
| `.cgui` 解析/合并 + 深色落地 + 测试 + 探针 | **我** | `runtime/src/core/{cg_doc.c,cg_doc.h}`、`runtime/src/{skin,win32}/**`、`runtime/include/cstackgui.h`、`tests/unit/test_style.c(新)`、`tools/format-roundtrip.mjs`、`docs/ARCHITECTURE.md` |
| 画布 | 队友 A | `designer/src/**` |
| 示例 | 队友 B | `examples/**` |
| 文档 | 队友 C | `docs/CGUI-FORMAT.md`、`README.md` |

## 6. 各域门禁

- 我：`build-runtime.bat`（0 warning）→ `tools\run-tests.bat --memcheck` → `tools\check-format.bat`
  → **真机截图**（深色前后）
- 画布：`npx tsc --noEmit` + `npx vite build`
- 示例：`build-app.bat <名字>` + `<exe> --selftest <cgui>`
- 文档：`node tools\check-examples.mjs --docs` + 不碰 `docs/ARCHITECTURE.md`（我列新文件）
