# CStackGUI 的 AI 开发配置（.codebuddy）

> **适用范围**：本仓库是 **Win32 桌面 GUI 项目**（Electron 画布 + C 运行时 + MinGW gcc），**不是固件工程**。
> **2026-09-27 清理**：按本工程实际删掉 13 个用不到的技能（8 个纯固件审查、板子侧 `esp-idf-helper`、
> 社区 3 个、输入侧压缩 `tokenzero`），技能 **41 → 28**；`settings.json` 的项目范围、`global-rule.md`
> 的 §1~§3 与 §7 映射表、本文件、以及 5 个技能正文里的引用一并同步。
> **2026-10-07 追加 v0.4**：新增 `sop` 总控（把散装的技能串成一条 SOP：**28 → 29**），
> 落地 `.codebuddy/code-reviews/` 目录，并把各技能里"引用不存在的路径 / 14 项审查 / 缺 `cd designer` /
> 固件模板残留"等口径问题一并纠正。
>
> **权威清单**：技能分类 / 触发条件 / 维护清单 → **`.codebuddy/skills/INDEX.md`**；
> 强制规则 → **`.codebuddy/global-rule.md`**。本文件是**导读**，同一事实不在这里重复维护。

---

## 1. 这套配置是什么

六类能力，共 **35 个技能**：

| 类 | 数量 | 作用 | 何时用到 |
|---|---|---|---|
| **总控** | 1 | ★ `sop`：三入口路由 + 八段流水线 + **对抗性闸门** + 唯一权威门禁命令表 + 收敛与中断恢复 | **任何开发类请求，先过它** |
| 审查类 | 7 | 代码与界面质量门禁（由 `piv-review-changes` 按领域调用） | 写码后 / 提交前 |
| 流程类（PIV 双循环） | 14 | 需求 → 提交的工作流 + 3 道门禁（审查门禁执行协议 = `adversarial-review`） | 每次开发 |
| 规程与纪律 | 4 | `designer-modify`（画布规程）+ `evidence-discipline` / `doc-truth-sync` / `request-spec` 三条工程纪律 | 改画布 / 下结论 / 写文档 / 开工前 |
| 约束与工具 | 8 | 压缩（caveman / ponytail）、思维准则（本质 + **对抗** + 多视角）、文件安全、真机实测、陌生库分析 | 始终生效 / 按需 |
| 社区引入 | 1 | `wavedrom-gen`：时序描述 → 波形图 | 按需 |

**硬门禁**（不过不提交，业务/文档改动也一样）：

```bat
build-all.bat                          :: 编译全部 + 逐示例自检
tools\run-tests.bat [--memcheck]       :: C 层单测（切帧 / JSON / 文档 / 类型表 / 事件表 / kit 冒烟）
tools\check-format.bat                 :: 跨端契约（分层 / 支持库表 / .cgui 字段 / bat 编码）
node tools\check-examples.mjs --docs   :: 文档口径与代码一致
npx tsc --noEmit && npx vite build     :: 改了画布（designer/）时叠这两条（记得先 cd designer）
node tools\check-skills.mjs            :: 改了 .codebuddy/skills\ 之后（check-format.bat 已含，可单独跑）
```

> **命令写法以 `sop` §3「门禁命令表」为唯一出处** —— 别处出现不同形态（缺 `cd designer`、
> `build-all` 不带 `.bat`、`tools/run-tests.bat` 用正斜杠）一律以该表纠正。

> 改了 `.bat` 之后必须跑 `powershell -NoProfile -ExecutionPolicy Bypass -File tools\bat-utf8.ps1 -Mode fix`
> （UTF-8 无 BOM + CRLF + ASCII 前导；详见 global-rule 开头）。

---

## 2. 目录结构

```
.codebuddy/
├── README.md            本文件（导读）
├── global-rule.md       强制规则（唯一权威）
├── settings.json        AI 助手配置（autoInvokeSkill / strictMode / scopes / excludePaths …）
├── plans/               PIV 实现计划（入库）
├── code-reviews/        提交前审查报告（入库；2026-10-07 补建，此前只在文档里被引用）
├── schemas/             产出物的 JSON Schema（现状见 §8）
├── skills/              35 个技能
│   ├── INDEX.md         ★ 技能索引（分类 / 触发条件 / 维护清单）
│   ├── sop/             ★ 总控：三入口路由 + 八段流水线 + 对抗性闸门 + 门禁命令表（先看它）
│   ├── [审查类] 6 个 C 代码检查：buffer_overflow / communication_protocol / error_handling /
│   │                firmware_code_style（本工程 C 规范）/ memory_leak / struct_best_practice
│   ├── [流程类] 14 个：plan-create-prd / plan-architecture / piv-slice-epic / prime-codebase /
│   │                piv-plan-implementation / piv-implement / piv-validate / piv-review-changes /
│   │                piv-create-pr / piv-run-full-loop / git-commit-style / skills-create /
│   │                team-parallel-delivery / adversarial-review
│   ├── [规程与纪律] 3 个：evidence-discipline / doc-truth-sync / request-spec
│   ├── [约束与工具] 8 个：caveman / ponytail / safe-file-ops / serial_verify /
│   │                first-principles-thinking / adversarial-thinking / analyzing-projects /
│   │                multi-perspective-review
│   ├── [本工程专属] design-taste（界面观感，属审查类）/ designer-modify
│   └── [社区] wavedrom-gen
├── memory/              项目记忆（.gitignore，不入库）
└── teams/               并行交付的运行态（.gitignore，不入库）
```

---

## 3. 强制规则（`global-rule.md` 摘要）

| 节 | 内容 | 要点 |
|---|---|---|
| §1 | 平台硬性规则 | 零新增链接依赖（系统符号运行期取）；接口签名与官方声明**逐参数一致**；平台行为以官方文档为准 |
| §2 | 安全规范 | 缓冲区必须判边界；禁止裸指针运算；字节流按帧切（别假设"读一次 = 一帧"） |
| §3 | 资源与生命周期 | 句柄 / GDI 对象 / 窗口创建即配对释放；栈上不放数组，大缓冲用静态或调用方提供 |
| §4 | 评审优先级 | 安全风险 > 稳定性 > 性能 > 风格 |
| §5 | 输出要求 | 问题必须带：文件 + 行号 + 风险等级 + 原理 + 修复示例 |
| §5.1 | caveman（常驻 full） | 回复压缩；代码/命令/路径/错误信息一字不改 |
| §5.2 | ponytail（常驻 full） | 写码前走决策梯子；红线（缓冲区边界 / 资源释放 / 错误码 / 时序 / 协议一致性）不砍 |
| §5.3 | safe-file-ops | 能不删就不删；删除攒成一次、给清单、只请求一次批准 |
| §5.4 | ★ `adversarial-thinking`（常驻，不可关） | 输出结论前**先攻击自己的结论**：验证缺陷真实存在、允许否掉自己、区分"修"与"观测"、对"不修"写理由 |
| §6 | PIV 工作流（入口 `sop`） | 三入口路由 + 八段流水线，3 道门禁 + 4 道对抗闸门，不许跳步 |
| §7 | 审查映射表 | 改动领域 → 调哪个审查技能（**7 行**，本工程适用） |
| §8 | 强制执行模式 | `strictMode` 下违反规则的代码必须改完才能提交 |
| §9 | 产出物管理规范 | 路径与命名规范（见 §7 表） |
| §10 | 版本历史 | v0.1 → v0.6 |

---

## 4. 审查技能库（7 个）

按改动领域自动触发（映射表在 `global-rule.md` §7），也可以点名调用（见 §6）：

| 技能 | 改动领域 | 检查重点 | 规模 |
|------|---------|---------|------|
| `buffer_overflow_check` | 缓冲区 / 字符串 / memcpy | 越界、整数溢出、危险函数、格式化字符串 | 10 项 |
| `memory_leak_check` | 动态内存 / 堆 | 泄漏、碎片、野指针、double free | 12 项 |
| `communication_protocol_check` | 串口 / TCP / UDP / 帧协议 | 超时、校验、粘包、字节序、重传、切帧 | 9 项 |
| `error_handling_check` | 错误码 / 断言 / 日志 | 返回值判断、资源释放、故障恢复 | 9 项 |
| `struct_best_practice_check` | 结构体 / 位域 / 函数指针 / 柔性数组 | 对齐、pack、18 种用法 | 18 种 |
| `firmware_code_style` | 新增 / 修改 `.c` `.h` | 命名、分层、Doxygen 注释、项目铁律 | 15 类 |
| ★ `design-taste` | 画布与 `.cgui` App 的界面观感 / 配色 / 字体 / 间距 / 动效 / 文案 | 设计读盘 + 三旋钮、AI 味黑名单、质量地板 | — |

每项检查都带 4 要素：❌ 错误示例 → ✅ 正确示例 → 🔍 原理分析 → 💥 后果说明。
（技能为什么只剩这 7 个、被删的 13 个叫什么 —— 见 `skills/INDEX.md` 的"2026-09-27 清理记录"。）

---

## 5. PIV 双循环开发工作流

在审查技能之上，把"需求 → 提交"做成可验证、可追溯的循环（内层循环每次开发必走）：

```
┌─────────────────── 外层循环（低频，大需求时走） ───────────────────┐
│  plan-create-prd ──→ plan-architecture ──→ piv-slice-epic          │
│  需求澄清→PRD        架构设计→SDD           拆解为可交付切片          │
└────────────────────────────────────────────────────────────────────┘
                          ↓ 每个切片进入内层循环
┌─────────────────── 内层循环（高频，每次开发必走） ──────────────────┐
│  prime-codebase → piv-plan-implementation → piv-implement          │
│  加载上下文        Validation-First 计划        按计划实现            │
│       ↓                                                           │
│  piv-validate→piv-review-changes→git-commit-style→piv-create-pr    │
│  编译+行为验证     审查(调审查技能)     原子提交       创建 PR/MR    │
└────────────────────────────────────────────────────────────────────┘
```

| 环节 | 技能 | 功能说明 | 产出物 |
|------|------|---------|--------|
| 外层 | `plan-create-prd` | 需求澄清 → PRD（含验收标准 / 非目标） | `docs/PRD_*.md` |
| 外层 | `plan-architecture` | 架构设计（模块 / 数据流 / 接口契约） | `docs/SDD_*.md` |
| 外层 | `piv-slice-epic` | 大需求拆解为可独立验证 / 提交的切片 | `docs/SLICES_*.md` |
| 内层 | `prime-codebase` | 加载代码库上下文（Context is King） | 上下文总结 |
| 内层 | `piv-plan-implementation` | Validation-First 实现计划 | `.codebuddy/plans/*.md` |
| 内层 | `piv-implement` | 严格按计划实现，计划外改动需确认 | 代码改动 |
| 内层 | `piv-validate` | 编译（`build-all.bat`）+ 静态检查 + 行为验证（`--selftest` / 真机实测） | 验证结果清单 |
| 内层 | `piv-review-changes` | 提交前审查（第四步强制 `adversarial-review` 三角色对抗裁定） | `.codebuddy/code-reviews/*.md` |
| 内层 | `git-commit-style` | `[类型][模块] 描述` 格式、分批原子化提交 | git commit |
| 内层 | `piv-create-pr` | 创建 PR/MR（含验证情况与关联文档） | PR/MR |
| 全流程 | `piv-run-full-loop` | 从描述到提交的端到端自动化 | 完整交付 |
| 元技能 | `skills-create` | 创建 / 更新技能的规范 | 新 `SKILL.md` |
| 并行 | ★ `team-parallel-delivery` | 按文件域切分的并行交付（契约先行 / 逐条回执 / 按模块分批提交） | 多模块交付 |

**核心方法论**：Context is King（先读再写）· Validation-First（先想怎么证明，再动手）· 对终点具体、对手段放权 · 已定决策直接继承 · 原子化提交。

### 3 道门禁

| 门禁 | 由谁把关 | 通过标准 |
|---|---|---|
| 计划门禁 | `piv-plan-implementation` | 实现前必须先有计划，且计划含验证方法与每步验证标准 |
| 验证门禁 | `piv-validate` | 编译无新增错误/警告 + 行为验证通过 + 边界与异常路径覆盖 |
| 审查门禁 | `piv-review-changes` | 按领域调用审查技能，无 critical/high 未修复项，报告落到 `.codebuddy/code-reviews/` |

**规则**：门禁不过 → 不能提交（`git-commit-style`）。

---

## 6. 使用方式

### 1️⃣ 自动触发（推荐）

照常提需求即可，AI 按改动领域加载技能，并在写完后自动审查：

```
你："帮我写一个串口收发，按帧取数据，带超时重试"
     → communication_protocol_check（超时、粘包、字节序）
     → buffer_overflow_check（缓冲区边界）
     → error_handling_check（重试与失败路径）

你："给日志视图加一个清空按钮，样式跟上边那排一致"
     → design-taste（按钮四态、间距节奏、文案）
     → designer-modify（.cgui 契约：新增控件要动的 7~8 处）
     → error_handling_check（回调里的失败路径）
```

### 2️⃣ 手动指定技能

需要针对性审查时点名（完整清单见 `skills/INDEX.md`）：

```
"用 struct_best_practice_check 审查这个结构体"
"检查这段代码的内存泄漏问题"
"用 communication_protocol_check 审查这段收发逻辑"
```

### 3️⃣ 代码审查模式

```
"审查我刚写的代码，重点检查安全性"
"帮我做全面的代码审查"
"这段代码有没有潜在风险？"
```

### 4️⃣ 完整开发工作流

```
你："给串口视图加 RS485 半双工收发，带帧间隔判定"（或用 piv-run-full-loop）

AI 自动执行：
  1. prime-codebase            → 加载项目上下文
  2. piv-plan-implementation   → 制定实现计划（.codebuddy/plans/）
  3. piv-implement             → 按计划实现
  4. piv-validate              → build-all.bat + 行为验证（再叠 run-tests / check-format）
  5. piv-review-changes        → 调用按领域的审查技能（协议 / 缓冲区 / 错误处理 …）
  6. git-commit-style          → 按 [类型][模块] 分批提交
```

大需求先走外层循环：`"先做需求分析"` → `plan-create-prd`；`"画一下架构"` → `plan-architecture`；`"把需求拆成小任务"` → `piv-slice-epic`。

### 风险等级

| 等级 | 说明 | 示例 |
|------|------|------|
| **🔴 致命** | 必须立即修复，否则崩溃 / 数据损坏 | 缓冲区越界、整数溢出导致内存被破坏 |
| **🟠 高危** | 强烈建议修复 | 内存泄漏、句柄 / GDI 未释放、粘包导致错帧 |
| **🟡 中危** | 影响稳定性或性能 | 配置未加 const、错误处理缺失 |
| **🟢 低危** | 风格或优化建议 | 命名不规范、注释缺失 |

`strictMode` 下：致命与高危必须修复才能提交。

---

## 7. 产出物管理规范

| 产出 | 位置 | 命名 | 入库 | 由谁产出 |
|------|------|------|------|---------|
| PRD | `docs/PRD_*.md` | `PRD_<feature>.md` | ✅ | `plan-create-prd` |
| SDD | `docs/SDD_*.md` | `SDD_<feature>.md` | ✅ | `plan-architecture` |
| 切片清单 | `docs/SLICES_*.md` | `SLICES_<feature>.md` | ✅ | `piv-slice-epic` |
| 实现计划 | `.codebuddy/plans/` | `<feature>.md` | ✅ | `piv-plan-implementation` |
| 审查报告 | `.codebuddy/code-reviews/` | `<日期>-<feature>.md` | ✅（**未忽略**：要共享就入库，不想入库就加进 `.gitignore`，别含糊） | `piv-review-changes` |
| 技能 | `.codebuddy/skills/` | `<name>/SKILL.md` | ✅ | `skills-create` |
| 项目记忆 / 并行运行态 | `.codebuddy/memory/`、`teams/` | — | ❌（已 .gitignore） | 工具运行时 |

---

## 8. 扩展与维护

- **加技能** → 用 `skills-create`（位置 / 命名 / frontmatter / 模板 / 质量要求都在它的 `SKILL.md` 里，本文件不重复）。
- **改 / 删 / 改名技能** → 按 `skills/INDEX.md` 的**维护检查清单**逐条过：同步 INDEX（含顶部计数与日期）+ `global-rule.md`（§6 流程 / §7 映射）+ 本文件；跨技能引用不许留断链。
- **产出物 Schema**（诚实说明）：`.codebuddy/schemas/` 下有 `implementation-plan.schema.json` 与
  `review-report.schema.json`，但**没有任何工具自动校验**（`tools/` 里没有 validator）。
  2026-10-07 的处置：不删、也不假装它生效 —— `sop` §2 把两个 schema 要求的字段**内联成每段的 DoD**
  （计划要"验证方法 + 边界清单 + 回滚方式"、报告要"等级 + 位置 + 原理 + 修复示例"），
  由人和 AI 按 DoD 写；schema 文件保留为**结构出处**。要真正机器化，就写一个 validator 接进
  `tools\check-format.bat` —— 在那之前别把它算进门禁。

---

## 9. 常见问题

- **会影响编译吗？** 不会。`.codebuddy` 只是 AI 侧配置，不参与编译与运行。
- **能禁用单个技能吗？** 能：把该 `SKILL.md` 的 `auto_invoke` 改成 `false`（复核时记得为什么禁的）。
- **审查太严怎么办？** 可把 `strictMode` 改 `false`，但不推荐 —— 门禁是这套体系的价值所在。
- **能用于其他项目吗？** 这套流程类技能（PIV 系列）与 `safe-file-ops` / `git-commit-style` 是可移植的；
  审查类里 `firmware_code_style`、`design-taste`、`designer-modify` 是本工程专属，别照搬。

---

## 10. 更新日志

| 日期 | 迭代 | 说明 |
|------|------|------|
| 2026-08-23 | v0.1 初始版本 | 建立配置体系：审查规则 + PIV 双循环工作流 + 门禁 + Schema 机制（当时 14 个嵌入式审查技能，源自固件模板） |
| 2026-08-23 | v0.1 社区技能 | 引入 4 个外部技能（保留上游结构）：`advise-project-approach` / `neuroarxiv` / `using-codegraph` / `wavedrom-gen` |
| 2026-09-22 | v0.2 设计品味 | 新增 `design-taste`（八套外部设计技能提炼后落到画布与 `.cgui` 两端）；`global-rule` §7 增加"界面观感 / 文案"一行 |
| 2026-09-27 | v0.3 技能清理 | 删 13 个用不到的技能（**41 → 28**）：8 个纯固件审查、`esp-idf-helper`、社区 3 个、`tokenzero`；同步 INDEX / §7 映射表 / `settings.json` / 本文件 / 5 个技能正文；§1~§3 换成平台与资源规则 |
| 2026-09-27 | v0.3 导读精简 | 本文件 681 → 约 300 行：去掉上游模板的"完善统计"话术、重复的 SKILL 模板、Q&A 骨架；改成本工程口径并补齐入库政策与 schema 现状 |
| 2026-10-07 | v0.4 SOP 总控 | 新增 ★ `sop`（**28 → 29**）：单一入口 = 路由 + 七段流水线（每段带 DoD）+ 唯一权威门禁命令表 + 审查路由表 + 收尾沉淀 + 中断恢复；补建 `.codebuddy/code-reviews/`；纠正 10 类口径不一致（`npx tsc` 缺 `cd designer`、审查技能数 14→7、报告目录三套归一、`build\accel-verify.ps1` 断链、frontmatter 缺 `auto_invoke` / 触发条件）与固件模板残留（`esp_*` / `HAL_*` / 中断 DMA / 外设 / NVS 语境） |
| 2026-10-07 | v0.5 对抗性思维 | 新增 ★ `adversarial-thinking`（**29 → 30**，常驻不可关，写入 global-rule §5.4）；`sop` 升级为三入口（建功能 / 修 bug / 评估想法）+ 八段（加 **P7 收敛**）+ **四道对抗性闸门** + 三角色审查；吸收 Spec Kit（独立入口、converge、诊断/修复/验证分离、"没有验证的修复不算修复"）与 vibe-coding-cn（计划三硬要求、进度留痕、新会话先读再动手、Debug 三件套） |
