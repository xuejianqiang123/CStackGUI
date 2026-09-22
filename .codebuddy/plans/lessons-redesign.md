# 实现计划：控件例程重新设计（**方案 B：按控件分类成课**，供 CSDN 系列文章）

## 目标

把 `examples/lesson01~05` 重做成**按控件分类的五个专题**，每课 **自成一篇文章**：
一课只讲一类控件、课内每个控件都能单独截一段图/抄一段代码，五课合起来覆盖全部 **42 种**控件。

交付：五课（`.cgui` + `main.c` + 说明）+ gallery 补齐 + 文档索引表 + 一条能证明"42 种都有落点"的机械闸门。

**为什么是 B（用户决定，理由已给）**：要做 CSDN 文章 —— 文章需要"一课一类、主题单一、截图自洽"，
而不是"从零到交付"的项目式路线（方案 A）。B 的代价照原样接受：每课控件多、01 不再是"最小闭环"，
`教学使用说明.md` 里的路线叙事要重写（不再是"递进上手"，而是"控件分类手册"）。

## 设计读盘 + 三个旋钮（design-taste）

- **读作**：读者是**照着文章抄代码的人**（一边看文章一边看窗口，可能只截其中一段）；
  语气克制、以"看清单"为主，落在既有 M3 语言（单一强调色 `#6750A4` + 圆角 + 底部状态栏）。
- **旋钮**：`VAR 2`（严格栅格：目录页要"整整齐齐"，不玩非对称）/ `MOTION 2`（只有状态切换）/
  **`DENSITY 6`**（一屏要放下 8~11 个控件，比业务 App 默认 5 密一档，但不到仪表盘的 8）。
- **一处明确偏离黑名单并说明理由**：每个演示控件旁边放一个**小号名字标签**（如 `button 按钮`）——
  黑名单里"eyebrow 标签"是装饰性废话，这里是**目录页的内容**（读者靠它对照文章里的段落），
  所以允许；但只允许这一类，不放其它说明文字（其余解释都写进文章与 `main.c` 注释）。
- 其余照旧守：单一强调色、一套圆角、间距 4px 基数（组内 8 / 组间 24）、正文 ≥12px、可点目标 ≥24×24、
  每课底部一个 `sb_main` 当唯一"结果出口"。

## 五课 = 五篇文章（42 种全覆盖；顺序沿用目录表的分组顺序）

| 课 | 文章主题 | 控件（id 前缀） | 数量 |
| --- | --- | --- | --- |
| **lesson01** | 基础与容器：能点的、能显示的、能装东西的 | `button` `iconbutton` `label` `link` `switch` `badge` `tag` `group` `box` `card` `separator` | 11 |
| **lesson02** | 输入与选择：表单怎么做 | `edit`（单行/多行/数值框/12 档输入方式/6 档边框一线讲全）`richtext` `datepicker` `monthcal` `checkbox` `radio` `listbox` `combo` | 8 |
| **lesson03** | 数值与指示：把数字显示出来 | `slider` `progress` `progressring` `meter` `led` `rate` `pagination` | 7 |
| **lesson04** | 数据与日志：组织、刷新、看得见 | `tabs` `tree` `table` `statusbar` `terminal` `chart` `splitter` `timer` | 8 |
| **lesson05** | 命令与图形：菜单、画板、媒体 | `menu` `menubutton` `toolbar` `image` `shape` `canvas` `colorpicker` `browser` | 8 |

- 合计 11+8+7+8+8 = **42** ✓（`splitter` 不在 README 现有表里 —— 那张表漏了它，顺带修）。
- 每课窗口统一 **宽 760**（文章配图尺寸一致），高按内容 320~620；底色沿用 `#F3EDF7`；底部 `sb_main`。
- 每课**只有一个第一落点**：目录式排布（左列控件、右列它的状态/结果），视线从左上到右下扫一遍就看完。

## 文章友好约定（本计划的硬规矩）

1. **每个演示控件一个专用回调**：`on_<id>_<动作>`（如 `on_btn_primary_click`、`on_sld_level_change`）——
   文章里"这个控件 → 这段代码"能一一对上。**"多控件共用一个回调"只在 lesson02 里作为进阶技巧演示一次**
   （原 lesson01/02 的 `on_form_change` 分流写法），并在注释里标明"这是省代码的写法，不是必须"。
2. **id 语义化 + 类型前缀**：`btn_*` `edt_*` `chk_*` `sld_*` `cmb_*` `lst_*` `tre_*` `tbl_*` `lab_*` `sb_*`……
   （文章正文里直接引用 id，读者能对上）。
3. **每课都填"非默认状态"**：滑块 60、进度 70%、灯亮、表格 3 行、图表有数据 —— 截图不能是空壳；
   每课另示范 **1 个禁用控件**（文章里讲 disabled 那一段）。
4. **`main.c` 头部注释 = 文章大纲**：本课控件清单 + 每个控件"关键 API 一句话" + 一句"想深入看哪一课"；
   正文注释按控件分节（`/* ---- button：能点的 ---- */` 这类分节标题保留）。
5. **每课都要能 `--selftest`**（这是 CI 闸门），并且 `main()` 骨架与现有示例逐字一致
   （`cg_app_desc_t` + `k_bindings` + `auto_locate_cgui` + `on_ready`）。
6. 不引新字段、不改 runtime/designer（只重排示例与文档）。

## 实现步骤（按序，每步都有验证方法）

### 步骤 1：机械闸门 `tools/check-examples.mjs`（先做，用来"证明差距"）
- 改动文件：新增 `tools/check-examples.mjs`（纯 Node，无依赖，风格对齐 `tools/gen-cc.mjs`）。
- 内容：从 `runtime/src/core/cg_doc_kind.c` 的 `k_kinds` 取全部类型（排除伪类型 `window`）；
  扫 `examples/**/*.cgui` 收集"类型 → 命中文件"；**没有落点的类型 → 打印清单并非 0 退出**。
  另给 `--table`：按类型打印 Markdown 映射表（用来生成文档索引表，避免手抄）。
- 验证：先跑一次，应报出当前缺口（现状 6 种：`box/splitter/terminal/chart/led/meter`）。
  *这一步不并入 `check-format.bat`* —— 等步骤 5 覆盖率补齐后再并，否则日常检查会一直红。

### 步骤 2：五课按新映射重写（每课一个原子交付）
顺序 lesson01 → 05，每课独立走一遍"改 → 验 → 截图"：
- 改动文件：`examples/lessonNN_*/lessonNN_*.cgui` + `main.c`（`.cgui` 内容重排、`main.c` 注释与回调重写）。
- 每课落地内容：上表的控件清单，按"左列控件 / 右列状态"的栅格摆放；每个控件一个回调；
  底部 `sb_main`；跑一次覆盖全部回调的 `--selftest`。
- 目录名与文件名保持不变（`lesson01_edit_button` 这类名字会与新主题不符 → **一并改名**：
  `lesson01_basics` `lesson02_input` `lesson03_values` `lesson04_data` `lesson05_commands`，
  同步改 `build-app.bat` 里的自身名字、`教学使用说明.md`、README 的引用 —— 改名前先 `git mv` 保历史）。
- 验证：`build-app.bat <课>` + `<课>.exe --selftest`（未登记回调 0、事件不支持 0）+ `tools\shot.ps1` 截图肉眼过。

### 步骤 3：gallery 补齐成 42 种（保持"一页看全"这句话是真的）
- 现状 33 种，缺 9 种：`box/splitter/terminal/chart/led/meter/radio/combo/separator`。
- 改动文件：`examples/gallery/gallery.cgui` + `main.c`（补页/补位）。
- 验证：`gallery.exe --selftest` + 截图 + 步骤 1 的闸门单看 gallery 也全绿。

### 步骤 4：文档
- `README.md` §1.2 控件表：三行输入（单行/多行/数字微调）→ **一行 `edit`** 并注明"多行 = `multiline`、
  数值框 = `spin`"；补上漏掉的 `splitter`；分组与五课映射对齐。
- `examples/教学使用说明.md`：重写成"控件分类手册"的路线（五个专题 + 每课控件清单 + **索引表**：
  42 行 `类型 | 哪一课 | 关键 API`，用 `check-examples.mjs --table` 生成）。
- `packaging/README.release.md`、`examples/gallery`（如带说明）里的"42 种都能在 gallery 找到"按事实改。
- 验证：索引表逐行与 `.cgui` 对照（用工具的 `--table` 输出 diff 出来）。

### 步骤 5：把闸门并进日常检查
- 改动文件：`tools/check-format.bat`（并联调用 `check-examples.mjs`）。
- 验证：`tools\check-format.bat` 全绿；故意删掉一个控件类型的使用应报红（反过来验闸门有效）。

### 步骤 6：全量回归
- `build-all.bat`（9 示例全编 + 自检）、`tools\check-format.bat`、`tools\run-tests.bat`。

## 验证清单（Validation-First 核心）

- [ ] `node tools/check-examples.mjs`：42 种**全部**有落点（这是本次"重新设计"的验收标准）
- [ ] `build-all.bat`：9 个示例全编、各 `selftest OK`、`[fail]` 0 次
- [ ] `tools\check-format.bat` / `tools\run-tests.bat` 全绿
- [ ] 五课各一张截图肉眼过：黑名单（无纯黑/渐变/卡片套卡片/装饰性色条）、地板（间距 4px 基数、
      正文 ≥12px、点选 ≥24×24）、**每课一个第一落点**、每课都有 1 个禁用态示范
- [ ] 五课键盘：Tab 顺序 = 视觉顺序、主按钮 Enter 生效、输入框聚焦紫环可见
- [ ] 文档索引表 42 行与 `.cgui` 实际一致（工具生成，不手抄）
- [ ] `main.c` 头部注释与实际演示控件一致（文章大纲 = 代码事实）

## 风险与排查路径

- **风险：改名（`lesson01_edit_button` → `lesson01_basics`）漏引用** → 编译报找不到目录 / 自检读不到 `.cgui`；
  用 `git grep lesson01_edit_button` 全仓库扫一遍再改。改名务必 `git mv`（保历史）。
- **风险：DENSITY 6 导致窗口放不下** → 每课先按"两列栅格"排版；超了就减容器内边距、再不行降 DENSITY 到 5。
- **风险：回调漏改** → `--selftest` 报表（未登记/绑定出错/事件不支持）直接指出 id 与函数名。
- **风险：gallery 变大后超高** → 现有上限 900×860，新页控件控制在 18 个内，超了拆两栏。
- **风险：把"工具跑绿"当设计验收** → 本计划强制**截图**（`tools\shot.ps1`；细节用 `tools\shot-widget.ps1`）。
- **排查顺序**：`--selftest` 报表 → 截图 → `check-examples.mjs` → 文档对表；前两步不读 C 代码。
