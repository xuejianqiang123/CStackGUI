# 实现计划：画布预览真实图片（`src` 所见即所得）

> 「向 py880 偷学」第四轮。前三轮：`py880-absorption.md`（多选+对齐）、`refs-rename.md`（引用改名）、
> `cgui-hot-reload.md`（外部变更热重载）。本文件是这一轮的**唯一契约**。

---

## 0. 为什么是它

`docs/ARCHITECTURE.md:706` 白纸黑字写着一条已知限制：

> 画布不预览真实图片（只显示 `src` 的文件名），运行时才画。

这是"所见即所得"承诺里最刺眼的一个洞：用户设了按钮背景图 / 图标 / 图片控件，**画布上只看到一个文件名小标签**，
必须编译跑一遍才知道比例对不对、会不会被拉变形。而且它和上一轮的热重载是同一条线：
**画布要相信磁盘上的东西**（上一轮相信 `.cgui` 文本，这一轮相信它引用的图片）。

### 非目标 / 被否掉的项

- **不改 `runtime/`**：预览是画布的事，运行时画法保持不变。
- **不做"图片编辑器"**（裁剪/缩放/滤镜）—— 那是另一个产品。
- **不做 `browser` 控件的网页预览**（`browser.src` 是 URL 不是图片；真嵌一个 webview 是另一个大件）。
- **不复刻"缺图占位框"的像素级外观**：运行时 `image` 画虚线框 + 文件名、`iconbutton` 画灰框 + 「图标」、
  `button` **静默回退纯色**（`cg_skin.c:415-451`）。画布保留自己的"读不到"提示（见 §1 决定 4），
  **故意比运行时更显眼** —— 编译前就要让人发现路径写错了，而不是等跑起来才看到占位框。
- **不假装运行时支持透明**：见 §1 决定 5。

## 1. 要什么 + 关键决定

### 要什么

`button.src` / `iconbutton.src` / `image.src` 三个字段 + `app.icon`，画布上**画出真图**，
缩放方式**逐条对齐运行时**；读不到时给出明确提示。

### 决定（每条都有运行时证据，不许凭感觉）

| # | 决定 | 依据 |
|---|---|---|
| 1 | **`button.src`：拉伸铺满（非等比）+ 按 `style.radius` 圆角裁切** | 运行时 `cg_skin.c:472-476`：`CreateRoundRectRgn` + `SetClipRgn`，再 `StretchBlt` 整幅铺满。CSS = `background-size: 100% 100%` + `overflow: hidden` + `border-radius` |
| 2 | **`iconbutton.src`：等比、居中、只缩不放、四周内边距 4px** | 运行时 `cg_win_ownldraw.c:33-54`：`maxw=box_w-8`，**只在超过可用区时才等比缩小**（小图保持原始像素）。CSS = 容器 flex 居中 + `padding: 4px` + `img { max-width: 100%; max-height: 100%; width: auto; height: auto }` |
| 3 | **`image.src`：拉伸铺满（非等比）** | 运行时 `cg_win_draw_canvas.c:144-147`：`StretchBlt` 到整个控件矩形 |
| 4 | **读不到时不静默**：画布上写「读不到图片」+ 文件名（比运行时显眼） | 运行时 `button` 是静默回退（`cg_skin.c:415-451`）、`image`/`iconbutton` 才画占位框。画布的价值在于**编译前**发现问题，所以统一显眼 |
| 5 | **路径解析：绝对路径原样；否则先按 `.cgui` 所在目录** | 运行时 `cgw_resolve_asset()`（`cg_win_util.c:241-257`）三段：原样(CWD) → **`.cgui` 目录** → exe 目录。设计器 `pickImage` 写入的 `rel` 也正是以 `.cgui` 目录为基准（`main.mjs:628-631`），两边口径一致。画布不做第三段（exe 目录对 Electron 没有意义） |
| 6 | **透明：画布按真透明显示，但明说这条与真机不一致** | 运行时转 HBITMAP 时传背景 `0x00FFFFFF`、绘制全走 `SRCCOPY`、全仓无 `AlphaBlend/TransparentBlt`（`cg_win_util.c:284`、`cg_skin.c:474`），**没有 alpha 合成**。画布要显示"这张图长什么样"，压平透明反而更误导；但差异必须在界面上说清 |
| 7 | **`app.icon` 也预览**（画布标题栏那个占位小方块） | 运行时 `LoadImageW(...IMAGE_ICON...)`（`cg_win_util.c:296-311`）应用到窗口类与 `WM_SETICON`。同一条读图通路，顺手 |

### 验收标准（每条都要能失败）

| # | 验收 | 怎么验（会红的方式） |
|---|---|---|
| A1 | `image.src` 在画布上画出真图、拉伸铺满 | 截图 `examples\gallery`：`#img_logo`（assets\logo.png）位置出现真图 |
| A2 | `iconbutton.src` 小图**不被放大**、居中、留 4px | 截图同上：`#icb_run`（assets\icon.png）是原始大小居中，不是撑满 52×52 |
| A3 | `button.src` 铺满 + 圆角 | gallery 里若有带 src 的按钮；没有就用临时 `.cgui` 造一个 |
| A4 | 路径按 `.cgui` 目录解析（相对 `assets\…` 能读到） | 同上截图（gallery 用的正是 `assets/logo.png` 相对路径） |
| A5 | 相对路径读到的是**当前文件**目录下的图（换文件不串图） | 缓存键带当前文件；临时两个工程各放一张同名不同色的图，切换后颜色必须变 |
| A6 | 读不到时提示「读不到图片」+ 文件名，不静默空白 | 临时把 `src` 改成一个不存在的文件 → 截图 |
| A7 | 浏览器模式不崩（无 Electron） | `bridge.onFileChanged` 同款：**必须写降级分支**；`npx tsc --noEmit` + 浏览器打开 dist 不报错 |
| A8 | 大图不把画布拖死 | 读图 IPC 有体积上限，超了返回"太大，不预览"并在画布上提示 |
| A9 | 画布与真机**逐控件对照**（最强的所见即所得证据） | `tools\shot-canvas.ps1 -File examples\gallery\gallery.cgui` 与 `tools\shot.ps1` 截 `gallery.exe`，两张图比对三个控件 |

## 2. 改动清单（每步自带验证）

| 步 | 文件 | 改什么 | 验证 |
|---|---|---|---|
| S1 | `designer/electron/main.mjs` | 新 IPC `cgui:readImage`：按 §1 决定 5 解析路径 → 读文件 → 返回 `{ ok, path, dataUrl }`（带体积上限、按扩展名给 mime） | A4/A6/A8 |
| S2 | `designer/electron/preload.cjs` | 加 `readImage`（invoke 通道，登记进白名单） | A1 |
| S3 | `designer/src/bridge.ts` | `Bridge` 加 `readImage`；Electron 分支接上；**浏览器降级返回 `{ ok: false }`** | A7 |
| S4 | `designer/src/imagecache.ts`（新） | 极小的图片缓存：`setImageBase()` / `peek()` / `request()` / `subscribe()`；键 = `base\u0000src` | A5 |
| S5 | `designer/src/components/WidgetView.tsx` | `useImage()` 钩子；button/iconbutton/image 按 §1 决定 1~4 画真图；缺图提示 | A1/A2/A3/A6 |
| S6 | `designer/src/components/Canvas.tsx` | 标题栏图标用 `app.icon` 的真图（读不到保持占位方块） | A9 |
| S7 | `designer/src/App.tsx` | `filePath` 变化时 `setImageBase()` | A5 |
| S8 | `designer/src/components/Inspector/{MediaSection,ContentSection}.tsx` | 改掉"画布上只显示文件名，运行时才画真图"的提示，并写明**透明差异** | A9 |
| S9 | `docs/ARCHITECTURE.md`、`docs/CGUI-FORMAT.md` | ①§8 已知限制里删掉"画布不预览真实图片"；②`src` 那节补一句画布会预览 + 透明差异 | 逐字核对 |

### 边界与异常

| 场景 | 预期行为 |
|---|---|
| 文档还没保存过（没有 `.cgui` 路径） | 没有基准目录 → 不请求、显示"保存后才能预览"占位 |
| `src` 是绝对路径 | 原样读（决定 5） |
| `src` 指向目录 / 二进制垃圾文件 | `GdipCreateBitmapFromFile` 之类会失败 → 画布按"读不到"处理（Chromium `<img>` 解码失败同理） |
| 文件超过体积上限（4 MB） | 不读，画布上提示「图片太大，未预览」 |
| 同一张图被多个控件引用 | 缓存命中，只读一次 |
| 换 `.cgui` 文件后同名相对路径 | 缓存键带 base → 重新读（A5） |
| 图片在外部被替换（配合上一轮热重载） | 本轮**不做**图片热更新（`.cgui` 没变就不会重新读图）→ 写进遗留 |

## 3. 回滚方式

改动集中在画布侧（`designer/src/**`、`designer/electron/**`）+ 文档。`.cgui` 契约、`runtime/` **零改动**。

## 4. 验证命令

```bat
cd designer && npx tsc --noEmit && npx vite build
tools\check-format.bat
tools\shot-canvas.ps1 -Out build\canvas-img.png -File examples\gallery\gallery.cgui
tools\shot.ps1  :: 截 gallery.exe 真机，与上一张逐控件对照
```

## 5. 进度

- [x] P0 上下文：调研运行时三种控件的画法与 `src` 解析三段逻辑（含透明与缺图行为）
- [x] P1 计划：本文件
- [x] P2 实现 S1~S9（新增 `imagecache.ts`；改主进程/preload/bridge/App/Canvas/WidgetView/两个 Inspector 面板/两份文档）
- [x] P3 验证：真机对照 + 三种缩放规则 + 读不到 + `tsc` + `vite build` + `check-format` = 全绿
- [x] P4 审查：`.codebuddy/code-reviews/2026-10-07-图片预览.md`（无 critical / high；一条已披露偏离）
- [x] P5 提交
- [x] P7 收敛：A1~A9 逐条对账，见下

### 验收对账（P7）

| # | 结果 | 证据 |
|---|---|---|
| A1 `image.src` 画真图、拉伸铺满 | **Converged** | `canvas-img3.png`；并与真机 `runtime-p1.png` 并排对照 |
| A2 `iconbutton.src` 不放大、居中、4px | **Converged** | 同上（150×150 框里图标仍是原始像素） |
| A3 `button.src` 铺满 + 圆角 | Converged | `canvas-img3.png`（文字压在图上、圆角可见） |
| A4 相对路径按 `.cgui` 目录解析 | **Converged** | 同一张截图（`assets/logo.png` → `build\assets\logo.png`） |
| A5 换文件不串图 | Converged（代码路径） | 缓存键 = `base\u0000src`；`setImageBase` 在 `filePath` 变化时换 base。**没做"两份工程各放同名异色图"的真机对照** |
| A6 读不到有明确提示 | **Converged** | `canvas-img3.png` 三处「读不到 …」 |
| A7 浏览器模式不崩 | Converged | 降级分支返回 `{ ok: false }`；`tsc --noEmit` 过（缺分支会直接报错） |
| A8 大图不拖死画布 | **偏离** | 上限（4 MB）照做、不读大图照做；但**画布提示与"读不到"合用一条**（计划里承诺的是单独提示"图片太大，未预览"）。理由与代价见审查报告 R2 #1 |
| A9 画布 vs 真机逐控件对照 | **Converged** | `runtime-p1.png`（gallery.exe）vs `canvas-p1.png`（画布），同一份 `.cgui` 同一页：`img_logo` 两边都铺满、`icb_run` 两边都居中不放大 |

### 真机验证抓到的 bug（静态门禁全绿照样不工作）

`useImage` 的请求 effect 原来只依赖 `src`。**React 子组件的 effect 先于父组件跑**：首次渲染时 App 还没把
基准目录交给缓存，`requestImage` 带着空 base 直接返回；等 App 补上 base 时 `src` 没变 → **永不重试**，
六张图全停在"没读到"。`tsc` / `vite build` / `check-format` 全绿，只有真机截图露馅（`canvas-img2.png`）。
修法：请求 effect 依赖 `[src, version]`，缓存广播时带正确 base 重试一次（命中缓存即返回，不成环）。

### 遗留

- 🟡 **图片文件本身被替换时画布不重读**：契约是"`.cgui` 变了才重载"，`assets/` 里的图换了要重新打开
  （或改一下 `src` 再改回来）。真要做图片热更新，得再开一套针对 `src` 目录的监视（且 `src` 可能是项目外绝对路径）。
- 🟡 **A8 的提示精度**：太大与读不到合用「读不到 + 文件名」，会让人去查路径。已把 `main.mjs` 的注释改成事实
  （它原先谎称渲染端会写"图片太大"）。
- 🟢 **A5 没做真机对照**（两份工程各放同名异色图）。
- 🟢 **`app.icon` 的 `.ico` 解码没单独验证**：红框探针证明了 `<img>` 元素与数据通路存在，
  但 14px 下"紫色 logo"与"紫色占位方块"长得一样，分不出来。
- 🟢 观测：`useImage` 的请求 effect 依赖 `version`，任何一张图读完都会让所有图片控件的 effect 重跑一次
  （命中缓存即返回，实测无卡顿）；模块级 `base` 是可变全局，F2 多文档时它要变成"每文档一份"。
- **下一轮候选**：F2 多窗体/窗体引用（跨端 epic，要独立 SDD）→ F8 生成代码的分区保留
  （让 `main.c` 能安全重新生成，而不是现在的"宁可不同步"）。
