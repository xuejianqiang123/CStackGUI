# CStackGUI 的 AI 开发配置（.codebuddy）

> **适用范围**：本仓库是 **Win32 桌面 GUI 项目**（Electron 画布 + C 运行时 + MinGW gcc），**不是固件工程**。
> 技能的增删史与"为什么删"只在两处记录，本文件不再抄第二份：
> **编制现状与清理记录** → `.codebuddy/skills/INDEX.md` 头部；**版本演进** → `.codebuddy/global-rule.md` §10。
>
> **权威清单**：技能分类 / 触发条件 / 维护清单 → **`.codebuddy/skills/INDEX.md`**；
> 强制规则 → **`.codebuddy/global-rule.md`**。本文件是**导读**，同一事实不在这里重复维护。

---

## 1. 这套配置是什么

八团编制，共 **39 个技能**（38 成员 + 1 入口）：

| 团 | 成员数 | 使命 | 何时用到 |
|---|---|---|---|
| **T0 总控台** | 5 | ★ `sop`：拆阶段、排并行、控熔断与交付边界 | 跨 ≥2 阶段 / 一条龙 / 并行 / 改技能 / 排障 |
| **T1 需求与方案** | 5 | 把模糊请求变成可执行规格与方案 | 新功能 / 需求不清 / 架构选型 / PRD·SDD·切片 |
| **T2 架构与实现** | 6 | 代码落在正确的层，按计划实现，有测试兜底 | 写改代码 / 加控件 / 改画布 / 陌生模块 |
| **T3 质量审判** | 9 | 用"指控→辩护→裁定"把缺陷挡在提交前 | 写码后 / 提交前 / 界面观感 / 挑刺复核 |
| **T4 验证与实测** | 2 | 把"编译过"推进到"判据绿 + 真窗口跑过" | 编译 / 单测 / 门禁 / 串口回环 |
| **T5 交付** | 2 | 按规范分批提交，需要时开 PR | 审查通过后 / 提交 / 建 PR |
| **T6 事实与文档** | 4 | 文档与代码同源，结论有证据 | 下结论 / 文档冲突 / 正式文档 / 时序图 |
| **T7 常驻底座** | 5 | 所有团队的默认工作方式（不选而入） | 始终生效 |
| **入口** | 1 | ★ `expert-teams`：选团 + 最小加载集 + 交接物 | **任何开发类请求，先选团** |

**硬门禁**（不过不提交，业务/文档改动也一样）：

```bat
build-all.bat                          :: 编译全部 + 逐示例自检
tools\run-tests.bat [--memcheck]       :: C 层单测（切帧 / JSON / 文档 / 类型表 / 事件表 / kit 冒烟）
tools\check-format.bat                 :: 跨端契约（分层 / 支持库表 / .cgui 字段 / bat 编码）
node tools\check-examples.mjs --docs   :: 文档口径与代码一致
npx tsc --noEmit && npx vite build     :: 改了画布（designer/）时叠这两条（记得先 cd designer）
node tools\check-skills.mjs            :: 改了 .codebuddy/skills\ 之后（check-format.bat 已含，可单独跑）
node tools\check-routing.mjs           :: 路由用例（触发词 / 歧义 / 覆盖），改 description 或加技能必跑
python tools\check_roster.py           :: 八团编制（teams.toml ↔ 磁盘目录 ↔ INDEX 的 ROSTER），加/移动技能必跑
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
├── skills/              39 个技能（38 成员 + 1 入口），按 8 个专家团物理分层
│   ├── INDEX.md         ★ 技能索引（八团编制 / ROSTER / 触发条件 / 维护清单）
│   ├── expert-teams/    ★ 团队入口：选团 / 最小加载集 / 交接物（先看它）
│   ├── _registry/       编制真值（teams.toml / routing_cases.json），不是技能
│   ├── t0-orchestration/  5：sop · piv-run-full-loop · team-parallel-delivery · skills-create · systematic-debugging
│   ├── t1-requirements/   5：request-spec · plan-create-prd · plan-architecture · piv-slice-epic · multi-perspective-review
│   ├── t2-architecture/   6：piv-implement · prime-codebase · analyzing-projects · piv-plan-implementation · test-driven-development · designer-modify
│   ├── t3-quality/        9：piv-review-changes · adversarial-review · buffer_overflow_check · memory_leak_check · communication_protocol_check · error_handling_check · struct_best_practice_check · firmware_code_style · design-taste
│   ├── t4-verification/   2：piv-validate · serial_verify
│   ├── t5-delivery/       2：git-commit-style · piv-create-pr
│   ├── t6-documentation/  4：evidence-discipline · doc-truth-sync · humanizer · wavedrom-gen
│   └── t7-foundation/     5：first-principles-thinking · adversarial-thinking · caveman · ponytail · safe-file-ops
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

**版本历史唯一权威 = [`global-rule.md`](global-rule.md) §10**（当前已到 v0.11）。

本节原本也维护一份，2026-11-01 实测已停在 v0.5、与 `global-rule.md` 差了 6 个版本 ——
而 `check-skills.mjs` 校验的是"名字集合"，不校验版本叙述，所以**没有任何命令会因此变红**。
这正是本项目反复踩的那条：同一事实有两份，人一懒就分叉。

历次迭代（含每批技能增删的理由）一律看 `global-rule.md` §10 与 `skills/INDEX.md` 头部。
