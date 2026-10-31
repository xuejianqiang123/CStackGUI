---
name: expert-teams
description: 【团队入口】把 38 个成员技能编成 8 个专家团（T0 总控 / T1 需求方案 / T2 架构实现 / T3 质量审判 / T4 验证实测 / T5 交付 / T6 事实文档 / T7 常驻底座），给选团判据、最小加载集、执行顺序、硬门禁与交接物。触发条件：任何开发/评审/验证/交付/文档任务（纯问答除外）先选团；不确定该用哪个技能；用户说"组个团队/会诊/一条龙/谁来负责"。
auto_invoke: true
argument-hint: [任务描述 | team:T0~T7]
---

# 专家团队总调度（技能层唯一入口）

**输入**：$ARGUMENTS

> 本技能**只做选人与加载**：判阶段 → 选团 → 给最小加载集与顺序 → 定义交接物。
> 流程阶段与门禁命令**一律看 `sop`**（§2 流水线 / §3 唯一权威门禁表）；本文件不复制规则正文，
> 冲突时以成员 `SKILL.md` 与 `sop` 为准。
> **编制单一事实源** = [`_registry/teams.toml`](../_registry/teams.toml)：
> 增删成员、换团长**先改它**，再跑 `python tools\check_roster.py`。

## 0. 30 秒用法（三步）

1. **判阶段**：这是"想清楚（方案）/ 写出来（实现）/ 证明它（验证）/ 挑毛病（审查）/ 送出去（交付）/ 写下来（文档）"里的哪一段？
2. **选团**：按 §1 总表选**一个主团**；只有明确需要接力时再拉第二个团。用户点名时优先服从：`team:T3`。
3. **最小加载**：只加载命中团卡里列出的技能，**按顺序**；不要整团全加载、不要预加载下游团。

> 一个任务同一时刻只允许 **1 个团长**对产出负责；跨团只交接，不并改同一文件。
> 适用边界：**会改文件、跑门禁、跨技能编排**的任务必须走本技能；纯问答 / 单文件只读查证可跳过。

## 1. 八团编制总表

| 团 | 使命 | 团长（入口） | 成员（按上场顺序） | 何时点将 |
|---|---|---|---|---|
| **T0 🧭 总控台** | 拆阶段、排并行、控熔断与交付边界 | `sop` | `sop` / `piv-run-full-loop` / `team-parallel-delivery` / `skills-create` / `systematic-debugging` | 跨 ≥2 阶段、一条龙、多任务并行、要增删技能、故障要定位根因 |
| **T1 🎯 需求与方案团** | 把模糊请求变成可执行规格与方案 | `request-spec` | `request-spec` / `plan-create-prd` / `plan-architecture` / `piv-slice-epic` / `multi-perspective-review` | 新功能、大需求、需求不清、架构选型、PRD/SDD/切片、方案会诊 |
| **T2 🏗️ 架构与实现团** | 代码落在正确的层，按计划实现，有测试兜底 | `piv-implement` | `piv-implement` / `prime-codebase` / `analyzing-projects` / `piv-plan-implementation` / `test-driven-development` / `designer-modify` | 写改代码、加控件/字段、改画布、接手陌生模块、重构 |
| **T3 🔬 质量审判团** | 用"指控→辩护→裁定"把缺陷挡在提交前 | `piv-review-changes` | `piv-review-changes` / `adversarial-review` / `buffer_overflow_check` / `memory_leak_check` / `communication_protocol_check` / `error_handling_check` / `struct_best_practice_check` / `firmware_code_style` / `design-taste` | 代码写完、提交前、界面观感评审、挑刺/复核/对抗式 |
| **T4 🧪 验证与实测团** | 把"编译过"推进到"判据绿 + 真窗口跑过" | `piv-validate` | `piv-validate` / `serial_verify` | 编译、单测、门禁、起真窗口、串口回环、日志视图 |
| **T5 🚚 交付团** | 按规范分批提交，需要时开 PR | `git-commit-style` | `git-commit-style` / `piv-create-pr` | 审查通过后、提交、保存改动、创建 PR |
| **T6 📚 事实与文档团** | 文档与代码同源，结论有证据，文字不像机器写的 | `evidence-discipline` | `evidence-discipline` / `doc-truth-sync` / `humanizer` / `wavedrom-gen` | 下"已实现/未实现"结论、文档冲突、写正式文档、画时序图 |
| **T7 ⚡ 常驻底座** | 所有团队的默认工作方式（不选而入） | `first-principles-thinking` | `first-principles-thinking` / `adversarial-thinking` / `caveman` / `ponytail` / `safe-file-ops` | 始终生效 |

## 2. 团队卡（点将后只读这一张）

> 成员技能物理位置：`skills/<团队目录>/<技能名>/SKILL.md`；技能名不变，`use_skill` 调用不受影响。

### T0 总控台 —— `t0-orchestration/`
- **最小加载集**：`sop` → 命中需求时再补 `piv-run-full-loop`（一条龙）/ `team-parallel-delivery`（并行）/ `skills-create`（改技能）/ `systematic-debugging`（排障）
- **硬门禁**：`sop` §3 的门禁表；改技能叠 `check-skills` + `check-routing` + `check_roster`
- **交接物**：`.codebuddy/plans/<feature>.md`（计划）→ 交 T2 或 T3
- **升级去向**：跨 ≥3 个团 ⇒ 停，先回 T1 把需求拆干净

### T1 需求与方案团 —— `t1-requirements/`
- **最小加载集**：`request-spec` → 按需 `plan-create-prd` / `plan-architecture` / `piv-slice-epic` → 定稿前 `multi-perspective-review`
- **硬门禁**：七元规格补齐；PRD 有验收标准与非目标；SDD 有模块边界
- **交接物**：`docs/PRD_*.md` / `docs/SDD_*.md` / `docs/SLICES_*.md`
- **升级去向**：验收标准仍含糊 ⇒ 回 `request-spec`，不要带猜进入 T2

### T2 架构与实现团 —— `t2-architecture/`
- **最小加载集**：`prime-codebase`（本项目）或 `analyzing-projects`（陌生模块）→ `piv-plan-implementation` → `piv-implement` + `test-driven-development`
- **硬门禁**：计划门禁（每步有验证方法）；改画布叠 `designer-modify` 的门禁
- **交接物**：代码改动 + `.codebuddy/plans/<feature>.md` 的完成标记
- **升级去向**：写行为代码前没有失败测试 ⇒ 回 `test-driven-development`，不许先写实现

### T3 质量审判团 —— `t3-quality/`
- **最小加载集**：`piv-review-changes` → 按改动领域加载专项（§7 映射表）→ **强制** `adversarial-review`
- **硬门禁**：无裁定成立的 critical/high；每条成立项有**能变红**的回归判据；报告落 `.codebuddy/code-reviews/`
- **交接物**：审查报告（含三方裁定表与健康度）
- **升级去向**：需要改实现 ⇒ 回 T2；需求本身就是错的 ⇒ 回 T1

### T4 验证与实测团 —— `t4-verification/`
- **最小加载集**：`piv-validate` → 行为验证需要真窗口/回环时 `serial_verify`
- **硬门禁**：编译零新增警告 + `tools\run-tests.bat` + `tools\check-format.bat`；判定读日志，不信 `%errorlevel%` 回显
- **交接物**：验证结果清单（含未验证项与原因）
- **升级去向**：红了 ⇒ 回 T2 修；判据疑似恒过 ⇒ 停，先证明"它出错时会红"

### T5 交付团 —— `t5-delivery/`
- **最小加载集**：`git-commit-style` → 需要时 `piv-create-pr`
- **硬门禁**：T4 + T3 都绿；`git status` 逐条枚举改动（不手写清单）
- **交接物**：分批 commit / PR
- **升级去向**：提交前发现漏验证 ⇒ 退回 T4

### T6 事实与文档团 —— `t6-documentation/`
- **最小加载集**：`evidence-discipline` → 文档冲突时 `doc-truth-sync` → 正式文档正文 `humanizer` → 时序图 `wavedrom-gen`
- **硬门禁**：下"已实现/未实现"结论前过三段取证（存在 → 登记 → 接线）；一个事实只留一处权威
- **交接物**：文档改动 / 取证底账
- **升级去向**：证据指向代码有问题 ⇒ 回 T3

### T7 常驻底座 —— `t7-foundation/`
- **不选而入**：`first-principles-thinking`（从本质出发）/ `adversarial-thinking`（出结论前攻击自己）/ `caveman`（少说）/ `ponytail`（少写）/ `safe-file-ops`（少删）
- **硬门禁**：无（它是工作方式，不是阶段）；但**输出结论前必须过 `adversarial-thinking`**

## 3. 跨团剧本（只写接力点，细节回各团）

| 剧本 | 路径 |
|---|---|
| **一条龙新功能** | T1 规格 → T2 实现 → T4 验证 → T3 审查 → T5 提交 |
| **只修 bug** | T0 `systematic-debugging` 诊断 → T2 写复现测试并修 → T4 验证症状消失 → T3 审查 → T5 提交 |
| **只评审** | T3（`piv-review-changes` + `adversarial-review`）→ 需要改则回 T2 |
| **只验证** | T4 |
| **方案选型** | T1（`multi-perspective-review`）→ 定稿 → T2 |
| **文档收口** | T6（`doc-truth-sync` + `humanizer`） |
| **改技能库** | T0（`skills-create`）→ 跑 `check-skills` / `check-routing` / `check_roster` |

## 4. 反模式（出现即指出）

- 整团全加载（38 个技能全塞进上下文）—— 正确做法是命中团的最小加载集
- 跳过 T1 直接写代码，事后再补 PRD
- T3 之前就提交（审查门禁形同虚设）
- 一个任务两个团长同时改同一文件
- 把 `sop` 的流程与本技能重复抄一份（**只引用，不复制**）

## 输出

- 选团结论：`这轮走 T<x>（<团名>），加载 <技能1> → <技能2>`
- 交接物路径
- 若跨团：写清"从哪个团交到哪个团、交的是什么"
