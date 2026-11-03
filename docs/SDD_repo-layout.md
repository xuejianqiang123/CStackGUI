# SDD：仓库目录布局整治

> 状态：**方案待批准**（本文只做分析，未动任何文件）
> 触发：2026-11-03「当前工程目录是不是最优的」
> 一句话结论：**骨架是对的，不重构。真正烂掉的不是目录，是那张描述目录的图。**

---

## 1. 先定判据：什么叫"最优"

目录布局没有绝对最优，只有"和你的目标函数匹配"。对 CStackGUI 这种"一个人 + AI 协作、Windows 桌面、要发发布包"的项目，四个目标函数按权重排：

| # | 目标 | 怎么算好 | 怎么算坏 |
|---|---|---|---|
| 1 | **可发现性** | 不看说明也能找到"串口例子在哪""门禁在哪" | 要 grep 才知道东西在哪 |
| 2 | **机械可验证性** | 结构错了有命令变红 | 只能靠人记得 |
| 3 | **变更局部性** | 改一个功能只动一个目录 | 改一个控件要碰 4 个地方 |
| 4 | **上手成本** | clone → 跑起来无歧义 | 要读 500 行 README 才敢动手 |

**没进这个表的东西**：文件数量整齐、目录对称美观、业界流行叫什么名字。这三样不影响交付速度。

---

## 2. 现状盘点（2026-11-03 实测）

### 2.1 顶层

| 目录 | 入库文件 | 职责 | 评价 |
|---|---|---|---|
| `runtime/` | 73 | C 运行时库，`src/` 按 `core` / `win32` / `skin` / `kit` 四层分 | **好**——这是真架构分层，不是按文件类型摊 |
| `designer/` | 70 | Electron + React 画布，分 `src` / `electron` / `scripts` | **好** |
| `examples/` | 125 | 5 组示例（`cppdemo` `gallery` `lessons` `suites` `toolbox`） | **结构好，文档没跟上**（见 L1） |
| `docs/` | 38 | 事实源：架构、格式规范、PRD/SDD、教程 | **好**，本轮刚做过目录归一 |
| `tools/` | 51 文件 | 门禁与开发期工具 | **可接受**（见 §3.2） |
| `tests/` | 37 | C 层单测，套件自注册 | **好** |
| `packaging/` | 7 | 发布物料模板 + 启动器 | **好** |
| `apps/` | 1（`README.md`） | 用户自己的工程，`.gitignore` 全忽略 | **好**——设计正确 |
| `common/` | 1 | 门禁夹具 `port_dlg.cgui` | **名字有问题**（见 L3） |

根目录：6 个 `.bat` 入口 + `README.md` + 5 个点配置（`.gitignore` `.gitattributes` `.editorconfig` `.clang-format` `.clangd`），无散落文件。**干净。**

### 2.2 一句话评价

这套布局比多数同类项目好：**没有 `src/` 大杂烩，没有 `utils/` 垃圾桶，没有按扩展名分目录**。`runtime/src` 的四层是真正的依赖方向（core 不依赖 win32），不是装饰。

**所以本文不提议重构目录。**

---

## 3. 明确不动的地方（连同理由，防止后人重提）

### 3.1 `packaging/splash/splash.exe` 入库 —— 是刻意的兜底，不是垃圾

```
designer/scripts/after-pack.cjs:115-126
  编译 splash.c 失败时：仓库里若已有编好的启动器就照用它，
  否则才报错。注释原文：「没有 gcc 也不能把包打坏」。
```

入库的 20KB 二进制是**无 gcc 环境下的 fallback**。删了会让"没装 MinGW 的机器打不出包"。**不动。**

### 3.2 `tools/` 51 个文件平铺 —— 前缀已经承担了分类

```
check-*  20 个    probe-*  6 个    gen-*  4 个
shot-*    4 个    suite-*  2 个    其余 15 个是单件工具
```

再分 `checks/` `probes/` `gen/` 子目录，收益是"目录更整齐"，代价是**每个引用这些工具的 bat/md 都多一层路径**（`check-format.bat` 已经引了 8 个）。当前 51 个文件在资源管理器里一屏放得下，不值得动。

### 3.3 `apps/*` 全忽略 —— 这是产品设计，不是遗漏

`apps/` 是"画布导出骨架的默认去处"，属于用户数据。只有 `README.md` 入库。正确。

### 3.4 没有 CI —— Windows + MinGW + GDI + WebView2，本地门禁更划算

`tools/gate.bat` 全量约 114s，已覆盖编译 + 自检 + 单测 + 跨端契约。搬到 GitHub Actions 要自己搭 MinGW 工具链、WebView2 运行时，维护成本远超收益。**除非将来要多机验证，否则不加。**

### 3.5 `examples/lessons/lessons/` 双层同名 —— 丑，但改它风险大于收益

`suites/studio/` 是一层（`studio/studio.cgui`），`lessons/lessons/` 是两层。不一致。

但 `lessons` 路径同时出现在 `build-all.bat`、`check-examples.mjs`、`make-release.bat:200` 的自检命令、以及多份文档里。拍平要同步改 5 处以上，收益只是"看起来对称"。**不动**，除非将来大规模改 `examples/`。

---

## 4. 问题清单（按代价排序）

### L1 【高】README 目录地图 19 条指向不存在的目录

**证据**（机器核对，脚本见附录 A）：

```
目录树条目 36 个：存在 15，找不到同名 21
其中 2 个是 build\ / release\（.gitignore 产物目录，正常）
净剩 19 条真过期
```

具体失效条目：

| README 行 | 写的 | 实际 |
|---|---|---|
| 425 / 426 / 428 | `suites\comm\` `suites\dev\` `suites\text\` | `suites\` 下只有 `studio\` 和 `sys\` |
| 433–448 | `examples\modbus\` `hextool\` `filecheck\` `traytool\` `shottool\` `syspanel\` `datarec\` `logfilter\` `dataview\` `envsnap\` `htmlreport\` `framedec\` `settings\` `encconv\` `drawpad\` `palette\`（16 个） | 全部已合并为模块：`suites/studio/modules/*.c`、`suites/sys/modules/*.c` |
| 449 | `toolbox\`（**第二次出现**，432 行已写过） | 重复条目 |
| 468 | `tools\gen-launcher.bat` | 文件不存在（已删或从未建） |

**真实代价**：新人或新会话按图找 `examples\shottool\`，得到"路径不存在"，然后要么以为仓库坏了，要么自己重新实现一遍。**文档骗人比没文档更贵。**

**根因**：这棵 98 行的目录树（`README.md:382-479`）**重复了磁盘事实**。任何重复的事实都会腐化，而这份没有任何命令检查它。

**改法**（见 §5 D1）：不修补 19 条，而是**把穷举下沉**，让 README 只画骨架。

---

### L2 【中】缺 LICENSE

根目录有 `.gitattributes` `.editorconfig` `.clang-format` `.clangd` `.gitignore`，唯独没有 `LICENSE`。

**为什么这是问题**：仓库有 `make-release.bat`、`packaging/README.release.md`、`CStackGUI-Setup.exe`——**是要对外交付的**。没有许可证，法律默认"保留所有权利"，拿到发布包的人不知道自己能不能用、能不能商用、能不能再分发。

**为什么排中不排高**：如果只自己用，影响为零。这条取决于你的分发意图（见 §6 决策 D4）。

---

### L3 【低】`common/` 单文件目录，名字与实质不符

`common/port_dlg.cgui` 是 `tools/format-roundtrip.mjs` 的 include 探针夹具，被这样引用：

```js
// tools/format-roundtrip.mjs:270
fs.existsSync(path.join(path.dirname(output), "..", "common", "port_dlg.cgui"))
// 默认 output = <root>/build/roundtrip_gallery.cgui
// 所以 ../common/ = <root>/common/ —— 位置刚性，必须与 build/ 同级
```

**位置不能动**（要测 `.cgui` 的 `../` 上级引用），**名字容易误解**（`common` 让人以为是公共代码库）。

历史事故：`plans/toolbox-launcher.md:58` 记录过它被当垃圾清理，导致 `check-format` exit=3。

**更值得改的不是名字，是行为**：

```js
// tools/format-roundtrip.mjs:276
console.log("[note] ../common/port_dlg.cgui 从输出目录解析不到，跳过 include 探针");
```

默认输出路径是确定的（`build/roundtrip_gallery.cgui`）。在这个路径下解析不到，说明**仓库结构坏了**，应该 fail。现在它 skip —— 这就是上一轮工作流硬化专门治的**伪绿灯**。

**改法**（原始提案）：默认输出路径 → 解析不到 fail；只有显式传了自定义输出目录才允许 skip。

**实际执行（2026-11-03）**：无条件 fail。查了两个调用方，**都显式传输出目录**
（`check-roundtrip-all.mjs:89`、`packaging/check-format.bat:47`），"只在默认路径失败"永远不会触发——
又是一种伪绿灯。真约束是"夹具必须在输出目录的上一级"，所以：探针解析不到就 exit 1，
同时 `make-release.bat` 把 `common\` 装进发布包，让 `release\build\` 也解析得到。详见 §9。

---

### L4 【低】`designer-dev.bat` 是孤立入口

19 行，做两件事：首次 `npm install`、然后 `npm run dev`。全仓 md/bat/mjs 里 **0 次提到它**。

不是死代码（手动双击能用），但**没写进任何文档**，等于"只有一个人才知道的门"。

**改法**：二选一——写进 `README.md` §2.5 常用命令，或删掉改在 README 里直接写 `cd designer && npm run dev`。倾向后者（少一个入口少一份维护）。

**已定（2026-11-03）**：保留 bat，写进 README §2.5。理由是它多做了一件容易忘的事——首次自动 `npm install`；
而 `npm install` 失败的表现是"画布起不来"，新人会去翻 electron 版本，而不是想到依赖没装。顺带把
`build-cstackgui.bat`（打包画布并同步到 `release\`）和 `make-release.bat` 也补进那张表：这两个在此之前同样 0 次出现在 README 里。

---

### L5 【低】`packaging/codebuddy/skills/` 与主技能库同名概念、不同内容

发布包带两份技能（`cstackgui-dev` 4068B、`cgui-edit` 4857B），主库 `.codebuddy/skills/` 下**没有同名文件**——说明是面向终端用户的精简版，不是重复维护。

**不是问题**，但两套都叫 "skills" 容易让人以为要同步。建议在 `packaging/README.release.md` 里加一句"这两份是给使用者看的精简版，与开发期技能库无同步关系"。

---

## 5. 关键决策

### D1 目录树的归属：README 只画骨架，穷举下沉

**三个选项：**

| 方案 | 做法 | 评价 |
|---|---|---|
| A. 修补 19 条 | 把过期条目改成 `suites/studio/modules/mb.c` | ❌ 明年还会烂。穷举清单必然腐化 |
| B. 删掉目录树 | README 不提结构 | ❌ 新人失去入口 |
| **C. 分层（推荐）** | README 只写**9 个顶层目录 + 一句话职责 + 入口**；每个目录自己的 `README.md` 写内部穷举 | ✅ 事实离它描述的东西最近 |

**C 的具体形状**：

- `README.md` §4.1 从 98 行压到 **约 25 行**：只留顶层 9 个目录、6 个入口 bat、`runtime/src` 四层、以及"示例清单见 `examples/README.md`"这一句指路。
- `examples/README.md`（已存在，2.6KB）承接 5 组示例的清单——它已经在了，只是 README 没指过去。
- 工具级清单已经在 `examples/suites/{studio,sys}/docs/<tool>.md`（20 份），**这些才是最新的**，不需要动。

**为什么不选 A**：A 修完只是把腐化推迟。C 是让"腐化"本身失去载体。

---

### D2 判据加在哪：扩 `check-docs.mjs`，不新建文件

`tools/check-docs.mjs` 现在管"产出物路径"（PRD/SDD/切片在 docs/）。目录结构判据与它同属"路径事实"，放一起。

**新增规则 D**（只查一件事）：

> `README.md` §4.1 代码块里，缩进 ≤4 且以 `\` 结尾的目录条目，必须在磁盘上存在同名目录。

- 只查**顶层 + 二级**（缩进 ≤4），因为深层职责已下沉，不再从 README 读
- 白名单：`build\` `release\`（`.gitignore` 产物目录）
- 不查 `tools/` 下的文件名（那是另一类事实，且变动频繁）

**为什么不做"README 提到的一切路径都必须存在"**：会误伤反例说明、历史记录、示例命令。判据窄才能长期活着——这条与第二轮否掉 J2 的结论一致。

---

### D3 `common/` 不改名，改行为

| 选项 | 评价 |
|---|---|
| 改名 `fixtures/` | ❌ 要同步 3 处代码 + 2 份文档；位置刚性不变，收益只是名字好懂 |
| 并入 `tools/fixtures/` | ❌ 破坏 `../` 上级引用的测试语义——它必须在输出目录的上一级 |
| **保留名字 + 修 skip 行为（推荐）** | ✅ 治真问题（伪绿灯），零迁移风险 |

配套：在 `README.md` §4.1 保留一行 `common\ 门禁夹具（不是公共代码）`，一句话消除误解。

---

### D4 LICENSE —— 需要你定，我给三个档

| 档 | 适用 | 内容 |
|---|---|---|
| **专有（推荐，若主要自用/客户交付）** | 不想让别人拿源码二次分发 | "Copyright (c) 2026 <你>. All rights reserved. 源码不公开；发布包可免费使用但不可反向工程/再分发" |
| MIT | 想让人自由用、包括商用 | 标准 MIT 全文（30 行） |
| Apache-2.0 | 想要专利授权条款 | MIT + 专利授权 + NOTICE 要求 |

**我倾向专有**：仓库含大量自研控件实现（`cg_skin*.c` 约 4 万行级别）、打包链路、以及一整套 AI 工作流——这些是你的竞争资产，MIT 等于白送。但这必须是你的决定，我不替你选。

**已定（2026-11-03，随"全部执行"落地）**：专有。`LICENSE` 用的是"可分发生成的应用、不可再分发本软件"的口径——
纯 All rights reserved 会让"用 runtime 编译自己的程序再卖"也变成侵权，等于把工具箱锁死；所以明确授予
「用本软件构建并分发你自己的应用（含商用、无需标注）」，只禁止再分发本软件本身、做同类框架、以及当 SDK 转手。
`README.md` §五 给了三句话版本，`LICENSE` 是唯一权威文本。

---

## 6. 改动清单（三批，可独立验证）

### 批次 1：README 目录地图重写（零代码风险）

| 动作 | 影响面 |
|---|---|
| `README.md:382-479` 压缩重写，只留顶层 + 四层 + 指路 | 单文件 |
| `README.md:449` 删掉重复的 `toolbox\` 条目 | 已被上条覆盖 |
| `README.md:468` 删掉不存在的 `gen-launcher.bat` | 已被上条覆盖 |
| 新增一行 `common\` 说明 | — |

**验收**：肉眼对比 9 个顶层目录全部真实存在；跑 `node tools\check-docs.mjs`（规则 D 上线后）绿。

**成本**：约 40 分钟。

---

### 批次 2：规则 D 上线

| 动作 | 影响面 |
|---|---|
| `tools/check-docs.mjs` 加规则 D + 注释说明"为什么只查缩进 ≤4" | 1 文件 |
| `tools/check-format.bat` 更新 echo 文案 | 1 行 |

**验收**：
- 正例：当前 README → `[ok] 目录地图：9 个顶层目录全部存在`
- 反例：临时在 README 加一行 `  _probe-missing\  x` → `[fail] README:NNN 目录地图里的 _probe-missing\ 不存在`，探针删掉后复绿

**成本**：约 50 分钟（含反例验证）。

---

### 批次 3：`format-roundtrip` 的 skip 改 fail

| 动作 | 影响面 |
|---|---|
| `tools/format-roundtrip.mjs:270-277` 按"是否显式传输出目录"分流 | 1 文件 |
| 若确认无其它调用方传自定义目录，可直接改成 fail | 需先查 `check-roundtrip-all.mjs` |

**验收**：正常跑 `check-format.bat` 绿；临时把 `common/port_dlg.cgui` 改名 → 必须 `[fail]` 而不是 `[note] ... 跳过`（验证完改回）。

**成本**：约 30 分钟。

---

### 待你拍板后才做的（**2026-11-03 已随"全部执行"落地，见 §9**）

- **D4 LICENSE**：选专有档，已生成 `LICENSE`（不是裸 All rights reserved，理由见 D4 末段）。
- **L4 `designer-dev.bat`**：保留并写进 README §2.5。

---

## 7. 不做什么（与 §3 呼应，集中列一次）

| 不做 | 理由 |
|---|---|
| 重构 `runtime/` `designer/` 分层 | 四层是对的，动了全是回归风险 |
| 给 `tools/` 分子目录 | 前缀已分类，加层级只增加路径长度 |
| 加 CI | 本地门禁 114s 已覆盖，Windows 工具链搬家不划算 |
| 删 `splash.exe` | 无 gcc 环境的 fallback |
| 拍平 `lessons/lessons/` | 5+ 处引用，收益只是对称 |
| 把 `apps/*` 纳入版本管理 | 那是用户数据 |
| 统一文件命名规范（如全小写下划线） | 现状是短横线 + 下划线混用，但没有判据能安全区分历史与现状，改了只会制造噪声 |

---

## 8. 验收标准

全部做完后，这三条必须同时成立：

1. `tools\gate.bat` 全量 → `[ok] gate passed (full)`
2. `node tools\check-docs.mjs` → `[ok]`，且**故意破坏时**会 `[fail]`
3. 一个没读过本项目的人，只看 `README.md` §4.1 就能说出"示例在哪、门禁在哪、运行时在哪"——不需要点开任何子目录

第 3 条是这次整治的唯一真实目标。前两条只是防止它再次腐化的保险。

---

## 9. 执行记录（2026-11-03）

三批全部落地。计划外多修了两处——都是同一类病（写死的数字 / 指向已消失路径），不属于范围扩张。

| # | 改动 | 文件 | 验证 |
|---|---|---|---|
| 1 | 目录地图 98 行 → 27 行，19 条失效路径随重写消失 | `README.md` | `check-docs` 规则 D 绿（25 条路径全部存在） |
| 2 | 新增规则 D，并用 5 个反例确认会真变红 | `tools/check-docs.mjs`、`tools/check-format.bat` | 反例 N1-N4 各自 `[fail]`，P0 复原后绿 |
| 3 | include 探针：静默跳过 → 硬失败 | `tools/format-roundtrip.mjs` | 正例（`build\`）真挂上并核对无损；反例（`build\sub\`）exit 1 |
| 4 | `common\` 装进发布包 | `make-release.bat` | 临时造 `build\`+`common\` 同级布局，探针解析成功 |
| 5 | **计划外**：发布自检引用的 `suites\text` 早已不存在 | `make-release.bat` | 改为 `suites\studio`（记事本模块 `nt.c` 在 studio，仍验证 `cg_kit`） |
| 6 | **计划外**：`check-examples.mjs` 硬编码"21 个工具并成 4 个" | `tools/check-examples.mjs`、`examples/README.md` | 改成从目录/`tabs.items` 生成 → 现在输出"21 个工具并成 2 个" |
| 7 | `LICENSE`（专有）+ README §五 三句话版本 | `LICENSE`、`README.md` | — |
| 8 | `designer-dev.bat` / `build-cstackgui.bat` / `make-release.bat` 进 §2.5 | `README.md` | — |

第 5 条值得单独说：`make-release.bat` 走到"发布包里编译 cg_kit 示例"这一步必然 `[fail]` 退出。
它不是文档腐化，是**构建链路上的死引用**——平时没人跑 `make-release.bat`，所以一直没暴露。
和 L1 同源：**引用一个已改名/已删除的路径，而没有任何判据在检查引用**。规则 D 只能治 README，
`make-release.bat` 里的路径没人管；这也是 §7 里"不加 CI"那一条的代价，先记在这里。

第 6 条是 L1 的镜像：文档写死的数字，生成器里也写死了一份，于是**重新生成也修不好**。
现在两处数字都来自 `examples\suites\` 的目录与 `tabs.items`，下次合并不会再漂。

---

## 附录 A：审计脚本（一次性，不入库）

```js
// tmp/audit-readme-tree.mjs —— 核对 README 目录树条目 vs 磁盘
// 判据故意放宽：只查"仓库里是否存在任何同名条目"，避免误判嵌套层级
// 输出：目录树条目 36 个：存在 15，找不到同名 21
```

## 附录 B：本次分析用到的实测数据

| 项 | 数值 |
|---|---|
| 入库文件总数 | 约 430 |
| 顶层目录 | 9 |
| 根入口脚本 | 6 |
| README 总行数 | 564 |
| README 目录树 | `382-479`（98 行） |
| 目录树失效条目 | 19 |
| `examples/` 工具文档 | 20 份（`suites/{studio,sys}/docs/*.md`） |
| 全量门禁耗时 | 约 114s |
