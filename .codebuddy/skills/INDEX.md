# 技能索引（SKILL INDEX）——唯一权威清单

> 本文件是 `.codebuddy/skills/` 下全部技能的**单一权威索引**。
> 新增 / 删除 / 改名技能时：**先改本文件**，再改 `global-rule.md`（触发规则）与 `README.md`（导读）。
> 维护约定：本文件只写"是什么 / 何时触发 / 产出"，具体规则一律以对应 `SKILL.md` 正文为准，不在此重复。
>
> 更新：2026-10-31（**两批共评估 20 个高星外部技能，只补 3 个真缺口：35 → 38**）｜ 技能总数：**39** ｜ 口径：`skills/` 下的技能目录数（**38 成员 + 1 入口**，成员在 `skills/<团队目录>/<技能名>/`）
> **2026-10-31 外部技能吸收记录**：评估了 GitHub 上 10 个高星 AI Skill 仓库（真实星标已用 API 核实，
> 2026-10-31 快照）：`obra/superpowers`(30.9万) / `mattpocock/skills`(30.8万) / `multica-ai/andrej-karpathy-skills`(22.7万) /
> `anthropics/skills`(18.5万) / `DietrichGebert/ponytail`(18.1万) / `nextlevelbuilder/ui-ux-pro-max-skill`(14.3万) /
> `Graphify-Labs/graphify`(14.0万) / `JuliusBrussee/caveman`(11.6万) / `addyosmani/agent-skills`(11.2万) / `Leonxlnx/taste-skill`(10.6万)。
> **只新建 2 个**（真缺口）：`systematic-debugging`（四阶段根因定位，补 `sop` §2A 第一段"只有框架没有方法"）、
> `test-driven-development`（C 层 RED→GREEN→REFACTOR，落到 `tests/unit/` + `cg_test.h` + `tools\run-tests.bat`）。
> **其余 8 个判定为已覆盖或不适配**，只做增量补强，不新建同名技能：
> ① `ponytail` / `caveman` / `ui-ux-pro-max-skill` / `taste-skill` → 本工程已有**更细的本地化版**
> （`ponytail` 有决策梯子+本工程红线，`caveman` 有 6 档位，`design-taste` 正文已注明综合了后两者）；
> ② `superpowers` 的 brainstorm / write-plan / review / verify 四步 → 由 `request-spec` + `piv-plan-implementation`
> + `piv-review-changes` + `piv-validate` 覆盖，不重复建；
> ③ `mattpocock/skills` 的需求反问 / 架构 / bug 诊断 → `request-spec` / `plan-architecture` / 新增的 `systematic-debugging` 覆盖；
> ④ `andrej-karpathy-skills`（单文件 CLAUDE.md 规则）→ 已被 `global-rule.md` + `ponytail` + `first-principles-thinking` 覆盖；
> ⑤ `anthropics/skills` 的 docx/pptx/xlsx/pdf → **平台级 skill 已内置**（不在本目录，无需复制）；
> ⑥ `addyosmani/agent-skills` 25 个工程规范 → 接口设计/代码审查/简化/调试/测试等已覆盖；未采纳 `frontend-ui-engineering`、
> `browser-testing-with-devtools`（本工程画布已由 `designer-modify` + `design-taste` 管）、`deprecation-and-migration`
> （无历史包袱）、`observability`（桌面单机，无服务端指标）；
> ⑦ `graphify` 的"先图后读"理念 → **并入 `analyzing-projects` 步骤 3.5 关系图**（不新建技能：
> 它本质是 Python AST 工具，本工程的图靠 LSP + 符号定位 + `文件:行` 证据落 md）。

> **2026-10-31 第二批（第 11~20 名，37 → 38）**：
> `Egonex-AI/Understand-Anything`(8.9万) / `pbakaus/impeccable`(9.7万) / `ComposioHQ/awesome-claude-skills`(7.8万) /
> `mvanhorn/last30days-skill`(6.6万) / `blader/humanizer`(6.3万) / `cathrynlavery/diagram-design`(6.3万) /
> `ayghri/i-have-adhd`(6.0万) / `coreyhaines31/marketingskills`(5.8万) /
> `Imbad0202/academic-research-skills`(5.5万) / `K-Dense-AI/scientific-agent-skills`(5.5万)。
> **只新建 1 个**：`humanizer`（文字去 AI 味，补"文档/README 的 AI 腔没人管"这个缺口——
> 此前 `design-taste` 只管**界面**文案与观感，`caveman` 只管**长度**，两者都不管正式文档的**语感**）。
> **未采纳**：① `Understand-Anything` 与 `graphify` 同类（代码→可交互知识图谱），理念已并入 `analyzing-projects` §3.5；
> ② `impeccable` 早已被 `design-taste` 吸收（其正文第 12 行注明来源 `pbakaus/impeccable`(craft-floor)）；
> ③ `awesome-claude-skills` 是 **awesome 清单**，不是技能本体，无法沉淀（且未声明许可证）；
> ④ `last30days-skill`（Reddit/X/HN 近 30 天综述）、`marketingskills`（CRO/SEO/增长）、
> `academic-research-skills`（论文 research→review→finalize）、`scientific-agent-skills`（177 个生物化学医学技能 + 科学数据库）
> —— 四个都是**本项目不做的业务**（C 桌面工具包，不写论文、不做营销、不做科研），不采纳；
> ⑤ `i-have-adhd`（别把答案埋在长文里）已被 `caveman` 覆盖且更强（6 档位）；
> ⑥ `diagram-design`（45 种图表、自包含 HTML+SVG、反 Mermaid）不新建：本项目文档图以截图为主，
> 时序图已有 `wavedrom-gen`，通用图的视觉规则归 `design-taste`，新建只会与前两者抢活。

> **2026-10-31 第三批（方法论移植，技能数不变 38）**：从用户的 LiberLive 团队实践
> （`C:\Users\admin\.workbuddy\skills\` 的 `agent-engineering` 与 `esp32-embedded-dev`）移植 4 条通用纪律，
> **只改现有文件，不新建技能**：
> ① **AI 生成代码专项**（幻觉 API / 凭记忆的常量值 / 同名不同义 / 注释与实现不符）→
> `global-rule.md` §7 新增小节 + `piv-review-changes` 第三步新增清单。本工程形态：`cg_*` 签名少传参数、
> 凭记忆写 `WM_*` 的值、编造 `.cgui` 字段名——都是"编得过、跑得动、语义是错的"；
> ② **评审优先级改分级**（P0 正确性与安全 → P1 平台与跨端契约 → P2 健壮与性能 → P3 风格，**命中即打回**）→
> `global-rule.md` §4 由四级并列改为分级表，并写明"本工程无 MCU，中断/DMA/栈深度/看门狗不在审查范围"；
> ③ **协作铁律**（垂直委托·横向咨询 / 冲突升级仲裁"来回 2 次即升级" / 任务分级，不派需要自己拿主意的活）→
> `team-parallel-delivery` 新增一节；
> ④ **长任务三项硬约束**（轮数硬顶 10 轮、中间状态落盘"换个会话能接着干"、降级路径重试上限 2 次）→
> `piv-run-full-loop` 新增一节。
> **未采纳**：ReAct vs Plan-and-Execute 选型、Agent 四层架构自检（本工程不是 Agent 系统）、
> ESP32/DMA/中断/看门狗那一族（无 MCU）、模型分级配置（Task 工具的 subagent 模型不可选，已改写成"任务分级"）。

> **2026-10-31 第四批（架构移植，技能数不变 38）**：从上游 `D:\CPro\.codebuddy`
> （嵌入式专家 V0.3：49 技能 / 8 专家团 / 物理分层）吸收**"单一事实源 + 机械校验"**这一层架构，
> **不搬目录、不改技能名**：
> ① **ROSTER 归属唯一**：本文件加 `ROSTER:BEGIN … ROSTER:END` 机器可读块，`check-skills.mjs` 校验
> "每技能恰好一类 + 各类数量与分类总览表数字一致"（此前那句"分类数字仍需人工核对"已可删）；
> ② **路由用例**：`skills/_registry/routing_cases.json` + `tools/check-routing.mjs`，38 条覆盖 38 个技能，
> 判据 = 触发词必须真在 description 里 / 不许第二个技能同时命中 / 每个技能被点名；
> ③ **元数据独立命名空间**：`_registry/` 以 `_` 开头，脚本与技能扫描都跳过（沿用上游约定）；
> ④ 两道门禁都并入 `tools\check-format.bat`，`sop` §3 与 `.codebuddy/README.md` 的门禁表同步。
> **本批曾判"暂不做"的三项（物理分层 / hooks / Python 编制校验），同日第五批已按要求全部落地，见下。**

> **2026-10-31 第五批（分层与硬约束落地，38 → 39 个技能）**：把上一批暂缓的三项全部落地：
> ① **物理分层**：38 个成员从 `skills/<name>/` 迁入 `skills/<团队目录>/<name>/`，`skills/` 顶层只剩
> `INDEX.md` / `expert-teams/` / `_registry/` / 8 个团队目录；技能名与 `use_skill` 调用不变。
> **铁律**：团队目录内**不许**放 `SKILL.md`（技能扫描遇到它就停止下钻，整团会被吞成一个技能），
> 团队说明一律用 `README.md`（8 份由 `teams.toml` 渲染）。
> ② **团队入口**：新增 `expert-teams`（39 = 38 成员 + 1 入口），给选团判据、最小加载集、硬门禁、
> 交接物与跨团剧本；`sop` 退为"选完之后按什么步骤走"，两者分工写进各自正文。
> ③ **Python 编制校验**：`_registry/teams.toml`（单一事实源）+ `tools/check_roster.py`
> （TOML ↔ 磁盘 ↔ ROSTER 三处一致，`--fix` 可重渲染 ROSTER）；`check-skills.mjs` 相应退回
> frontmatter / 目录树 / scopes，并支持"入口 + 团队目录"两层扫描。
> ④ **hooks 落地**：`tools/hooks/` 三个脚本（`guard-command` 按段首拦破坏性命令 /
> `verify-on-stop` 只对回归回注 / `inject-context` 注入环境简报），四个分支均已自测通过；
> 接线需在 `.codebuddy/settings.json` 追加 `hooks` 段——该文件是受保护路径，**待人工确认一次**。
> **格式取舍**：`teams.toml` 用 TOML（Python 3.11+ 的 `tomllib` 是标准库，零依赖）；
> `routing_cases.json` 用 JSON（Node 原生）。**选格式的依据是"运行时原生支持"，不是好看**。
> **2026-10-17 上游同步记录**：来源 `D:\CPro\.codebuddy`（ESP32 固件项目的 ai-dev-kit，本工程 .codebuddy 的原始模板）。
> 本次只吸收**与本工程同构的通用规程**：`adversarial-review`（三角色对抗评审，补上审查门禁的执行协议）、
> `evidence-discipline`（取证纪律，伪证据清单已换成本工程实例）、`doc-truth-sync`（文档事实同步）、
> `request-spec`（请求规格化七元）、`multi-perspective-review`（三视角会诊）；同时把 `piv-run-full-loop` 的
> 自愈循环 / 熔断 / 假完成清单、`piv-implement` 的"禁止降判据换绿"、`piv-review-changes` 的三角色裁定
> 合并回来（均为上游同源技能里的通用机制）。
> **未采纳及原因**：① 固件专属技能（IDF 工作流 / 新架构分层 / 主循环模型 / POSIX 收口 / 配置面 / 子模块三件套 /
> 真机 HIL / 时延预算 / 韧性观测 / 8 个 MCU 审查）—— 本工程没有 MCU，2026-09-27 已清理同族，不恢复；
> ② git 提交红线与用户"提交定了就提"的既有协作方式及 `global-rule.md` §6 冲突，不采纳；
> ③ 上游 MR 评审 / 门禁运行 / 本地 CI 对齐 / 单测编写 / 模块脚手架的固件专属部分（CI 脚本、Makefile 登记、
> CMake 接线）本工程不存在，其通用纪律已由 `piv-validate` / `piv-review-changes` / `designer-modify` 覆盖；
> ④ 上游 hooks（guard-command / verify-on-stop 等）属 IDE 层行为变更，待用户拍板后另行评估。
> **★ = 本工程专属**（其余继承自上游模板并已适配；本次 5 个同步技能在各 SKILL.md 头部标注了来源）
> **入口**：任何开发类请求先看 `sop` —— 它负责路由、串流水线、兜门禁；本文件只管"有什么、何时触发"。

---

## 分类总览

| 团 | 使命 | 团长 | 成员数 | 何时点将 |
|---|---|---|---|---|
| **T0 🧭 总控台** | 拆阶段、排并行、控熔断与交付边界 | `sop` | 5 | 跨 ≥2 阶段、一条龙、多任务并行、改技能、排障 |
| **T1 🎯 需求与方案团** | 把模糊请求变成可执行规格与方案 | `request-spec` | 5 | 新功能、大需求、需求不清、架构选型、PRD/SDD/切片 |
| **T2 🏗️ 架构与实现团** | 代码落在正确的层，按计划实现，有测试兜底 | `piv-implement` | 6 | 写改代码、加控件/字段、改画布、接手陌生模块 |
| **T3 🔬 质量审判团** | 用"指控→辩护→裁定"把缺陷挡在提交前 | `piv-review-changes` | 9 | 代码写完、提交前、界面观感、挑刺复核 |
| **T4 🧪 验证与实测团** | 把"编译过"推进到"判据绿 + 真窗口跑过" | `piv-validate` | 2 | 编译、单测、门禁、起真窗口、串口回环 |
| **T5 🚚 交付团** | 按规范分批提交，需要时开 PR | `git-commit-style` | 2 | 审查通过后、提交、创建 PR |
| **T6 📚 事实与文档团** | 文档与代码同源，结论有证据 | `evidence-discipline` | 4 | 下"已实现"结论、文档冲突、写正式文档、画时序图 |
| **T7 ⚡ 常驻底座** | 所有团队的默认工作方式（不选而入） | `first-principles-thinking` | 5 | 始终生效 |
| **入口** | 选团 / 最小加载集 / 交接物 | `expert-teams` | 1 | 任何开发类请求，先选团 |

> 5 + 5 + 6 + 9 + 2 + 2 + 4 + 5 = **38 个成员** + **1 个入口** = **39 个技能**。
> **编制真值** = `skills/_registry/teams.toml`；**物理位置** = `skills/<团队目录>/<技能名>/`。
> 三处一致性（TOML ↔ 磁盘目录 ↔ 下方 ROSTER）由 `python tools\check_roster.py` 校验。

<!-- ROSTER:BEGIN
T0|sop,piv-run-full-loop,team-parallel-delivery,skills-create,systematic-debugging
T1|request-spec,plan-create-prd,plan-architecture,piv-slice-epic,multi-perspective-review
T2|piv-implement,prime-codebase,analyzing-projects,piv-plan-implementation,test-driven-development,designer-modify
T3|piv-review-changes,adversarial-review,buffer_overflow_check,memory_leak_check,communication_protocol_check,error_handling_check,struct_best_practice_check,firmware_code_style,design-taste
T4|piv-validate,serial_verify
T5|git-commit-style,piv-create-pr
T6|evidence-discipline,doc-truth-sync,humanizer,wavedrom-gen
T7|first-principles-thinking,adversarial-thinking,caveman,ponytail,safe-file-ops
X|expert-teams
ROSTER:END -->

> **ROSTER 块由 `teams.toml` 渲染、由 `python tools\check_roster.py` 校验**：
> 每个成员**恰好属于一个团**、每个团目录都在磁盘上且其中每个技能都有 `SKILL.md`、
> 团队目录里**不许**有 `SKILL.md`（技能扫描遇到它会停止下钻，整团被吞成一个技能）。
> `X|` 是入口，不属于任何团。改编制只改 TOML，再跑 `python tools\check_roster.py --fix` 重渲染本块。
> `designer-modify` 单独成类，是因为它既不是"审查"也不是"流程步骤"；本次同步来的三条纪律
> （取证 / 文档同步 / 请求规格）与它同归 C 类 —— 都是"本工程怎么做一件事"的规程，不是门禁执行体。
> `sop` 单独成类，是因为它是**编排器**：不重复任何技能正文，只回答"现在该加载谁、
> 这一段做完没有、门禁命令怎么写、断了怎么续"。
>
> **2026-09-27 清理记录**：删掉 13 个本工程用不到的技能 —— 8 个纯固件审查（`dma_cache_check` /
> `interrupt_check` / `stack_overflow_check` / `rtos_task_check` / `flash_storage_check` /
> `peripheral_conflict_check` / `power_management_check` / `watchdog_timer_check`）、板子侧索引
> `esp-idf-helper`、社区技能 3 个（`neuroarxiv` / `using-codegraph` / `advise-project-approach`）、
> 输入侧压缩 `tokenzero`。口径：本工程是 Win32/GDI+ 桌面工具包 + 画布，**没有 MCU**，
> 被删的都是"给板子写固件"才用得到的。恢复办法：`git checkout <该次提交>^ -- .codebuddy/skills/<技能名>`。

---

## 能力说明 · 总控

> 以下按**用途**分组说明各技能的触发条件与产出，**不带计数**；
> **归属与编制**以本节上方的八团总表 + ROSTER 为准，两者不一致时以编制为准。

| 技能 | 触发条件 | 关键内容 |
|---|---|---|
| ★ `sop` | 任何开发类请求（新功能 / 改 bug / 改画布 / 改技能 / 并行 / 提交前）；用户说"走流程""SOP""全流程""从需求到提交" | §0 常驻约束 · §1 入口路由（问答题不必走全流程）· §2 七段流水线 + 每段 DoD · §3 **唯一权威门禁命令表** · §4 审查路由表 · §5 收尾与沉淀 · §6 中断恢复 · §7 禁区（本工程没有的东西） |

> 为什么单独立一个：之前 28 个技能是"散装"的 —— 谁先谁后、门禁命令怎么写、报告落哪、
> 断了怎么续，分散在 6 个技能里且**互相不一致**（实测：`npx tsc` 有的带 `cd designer` 有的不带、
> 审查技能数有的写 7 有的写 14、报告目录有的写 `.codebuddy/code-reviews/` 有的另开一套）。
> `sop` 是这些问题的**单一裁判**。

---

## 能力说明 · 审查类（质量门禁执行体）

`global-rule.md` §7 给出"改动领域 → 调哪个"的映射表。2026-09-27 清理后，本类只留本工程真会命中的领域；
原先 8 个标 〔固件〕 的（DMA / 中断 / 栈用量 / RTOS / Flash / 外设 / 低功耗 / 看门狗）随 MCU 场景一起删除。

| 技能 | 改动领域 | 检查重点 |
|---|---|---|
| `buffer_overflow_check` | 缓冲区 / 字符串 / memcpy | 越界、整数溢出、危险函数、格式化字符串 |
| `memory_leak_check` | 动态内存 / 堆 | 泄漏、碎片、野指针、内存越界 |
| `communication_protocol_check` | 串口 / TCP / UDP / 帧协议 | 超时、校验、粘包、字节序、重传、重连 |
| `error_handling_check` | 错误码 / 断言 / 日志 | 返回值判断、容错、故障恢复 |
| `struct_best_practice_check` | 结构体 / 位域 / 函数指针 / 柔性数组 | 对齐、pack、18 种用法 |
| `firmware_code_style` | 新增 / 修改 `.c` `.h` | 命名、分层、Doxygen 注释、项目铁律 |
| ★ `design-taste` | 画布与 `.cgui` App 的界面观感 / 配色 / 字体 / 间距 / 动效 / 文案 | 设计读盘 + 三旋钮、AI 味黑名单、质量地板 |

> 调用规则：改动涉及哪类代码，就加载对应 `SKILL.md` 执行其**完整检查清单**，不得凭经验泛泛而过。

---

## 能力说明 · 流程类（PIV 双循环）

### 外层循环（低频，大需求）

| 技能 | 触发条件 | 产出 |
|---|---|---|
| `plan-create-prd` | 新功能但验收标准不明确、跨模块 / 协议 | `docs/PRD_*.md` |
| `plan-architecture` | 要架构设计 / 模块划分 / 重构 | `docs/SDD_*.md` |
| `piv-slice-epic` | 需求太大、需拆分 / 分阶段 | `docs/SLICES_*.md` |

### 内层循环（每次开发必走；含 3 道门禁）

| 阶段 | 技能 | 门禁 | 产出 |
|---|---|---|---|
| 0 上下文 | `prime-codebase`（双模式：本项目 / 陌生库） | — | 上下文总结 |
| 1 计划 | `piv-plan-implementation` | **计划门禁** | `.codebuddy/plans/*.md` |
| 2 实现 | `piv-implement` | — | 代码改动 |
| 3 验证 | `piv-validate`（真机那一级调 `serial_verify`） | **验证门禁** | 验证结果清单 |
| 4 审查 | `piv-review-changes` | **审查门禁** | `.codebuddy/code-reviews/*.md` |
| 5 提交 | ★ `git-commit-style` | — | git commit |
| 6 PR | `piv-create-pr` | — | PR / MR |

### 编排与元技能

| 技能 | 触发条件 | 说明 |
|---|---|---|
| `piv-run-full-loop` | 小范围明确功能需求，未要求分步 | 自动走完内层循环 0→5（含闭环三要素 / 自愈循环 / 熔断上报 / 假完成清单） |
| ★ `team-parallel-delivery` | 并行 / 多任务 / 需求落在多个互不重叠的域 | 按文件域切分的并行交付（契约先行、逐条回执、按模块分批提交） |
| **`adversarial-review`** | `piv-review-changes` 第四步（审查门禁的执行协议）；用户说"对抗式评审 / 三角色评审 / 互相审判 / 挑刺再复核"；critical 判定有争议 | **三角色互相审判**：挑刺者带 E1+ 证据指控（只指控不给方案）→ 辩护者默认"挑刺者是错的"、重读原文找反证，裁定成立/降级/驳回（**驳回必须附反证出处**）→ 改进者只修成立项并配**能变红**的回归判据；报告含三方裁定表与健康度（成立率 100% ⇒ 辩护者橡皮图章，重做） |
| `skills-create` | 创建 / 更新技能 | 技能规范与模板（元技能） |

### 内建纪律（不占流水线阶段，插进实现/排查里用）

| 技能 | 何时插入 | 铁律 |
|---|---|---|
| `test-driven-development` | 内层循环第 2 段「实现」写行为代码前；修 bug 的「修复」段前；用户说"加测试" | **没有先行失败的测试，不写生产代码**；先写后补的代码删掉重写；收尾跑全量 `tools\run-tests.bat` |
| `systematic-debugging` | 修 bug 旁路的第 1 段「诊断」（`sop` §2A）；任何"偶发 / 复现不了 / 崩了" | **没有根因调查，不许提修复**；一次只改一个变量；3 次修不好 = 质疑架构，不试第 4 次 |

---

## 能力说明 · 规程与纪律

| 技能 | 触发条件 | 关键约束 |
|---|---|---|
| ★ `designer-modify` | 改 `designer/` 下的任何代码 | 画布（Electron + React + TS）的完整说明书：文件地图、`.cgui` 契约、新增控件要动的 7~8 处、验证门禁 |
| `evidence-discipline` | **常驻生效**：要回答"某功能有没有做 / 用到了吗 / 旧结论对不对 / 谁是对的"；即将给出任何"已实现 / 未实现 / 已修复"判断；需要为结论、审查、报告提供证据 | 文档一律视为**待证假设**；判"已实现"须过**三段取证**（代码存在 → 已登记 → 已接线）；11 条伪证据清单（grep 计数 ≠ 被使用、退出码 0 ≠ 跑过、测试全绿 ≠ 覆盖、中文 grep 恒空、`%errorlevel%` 回显骗人…）；门禁全绿前先确认其**扫描域包含**改动文件；新用判据先问"**它出错时会红吗**" |
| `doc-truth-sync` | 改需求 / 状态 / 新增删除组件；文档之间或文档与代码互相矛盾；用户问"以哪个为准 / 文档是不是过期了"；准备写"已完成 / 未完成 / 缺失"结论前；批量改动后收口（P7） | 代码是事实源、文档是缓存；**一个事实只留一处权威**，其它地方放指针；冲突裁决按"代码实测 → 机械判据 → 生成物"顺序，禁止投票 / 取新 / 折中；人工能发现的腐化**应升级为门禁**；内含本工程权威分布表（字段语义 / 类型表 / 支持库 / 技能索引各归各） |
| `request-spec` | **常驻生效**：请求是一句话且缺路径 / 文件 / 判据；即将开工但请求未成型；输出质量差、反复改稿时回头定位缺哪一元 | 动手前补全**七元规格**（角色 / 任务 / 素材 / 结构 / 格式 / 约束 / 交付场景）；**可推断的先声明假设并继续**，只问"猜错会返工"的阻塞项且 ≤3 个；只做分发，规则一律以被转交的领域 skill 为准 |

---

## 能力说明 · 正交约束与工具

| 技能 | 作用层 | 触发 |
|---|---|---|
| `first-principles-thinking` | 决策层（始终生效） | 任何需求 / 方案 / 排查 / 输出 |
| ★ `adversarial-thinking` | 结论层（始终生效） | **输出任何"N 条结论/建议"之前**；用户说"对抗性思考""你确定吗""帮我挑挑毛病"；`piv-validate` 判门禁、`piv-review-changes` 出报告前 |
| `multi-perspective-review` | 决策层（多视角会诊方案） | 方案定稿前（SDD / 接口契约 / 跨层改动 / `.cgui` 契约变更）；用户说"多视角评估 / 专家会诊 / 换个角度看看"；`plan-architecture` 产出后自检 |
| `caveman` | 输出侧（少说） | 常驻；`lite/full/ultra/wenyan/off` |
| `humanizer` | 输出侧（说话像人） | 按需；正式产出物正文（README / docs / 教学说明 / PR 描述），**对话不用** |
| `ponytail` | 生成侧（少写） | 常驻 full；`lite/full/ultra/off` |
| ★ `safe-file-ops` | 文件操作（非破坏） | 准备删除 / 清理 / 覆盖时 |
| `serial_verify` | 验证侧（真机实测） | `piv-validate` 第 3 级；GUI / 收发 / 交互类改动 |
| `analyzing-projects` | 认知侧（陌生代码库） | 接手新项目 / 陌生子模块 / "这块怎么工作的" |

> 输出侧三者正交：`ponytail` 少写代码 ｜ `caveman` 少说废话（管**长度**，用于对话）｜
> `humanizer` 说人话（管**语感**，用于正式文档）。**同一段文字只用一把尺**：`caveman` 已声明
> "正式产出物正文不压缩"，那块地归 `humanizer`，不要叠两层。（输入侧压缩 `tokenzero` 随 2026-09-27 清理移除；
> 板子侧参考 `esp-idf-helper` 同样已删 —— 本工程不含固件。）
> 三个思维正交：`first-principles-thinking` 从本质出发（不假设需求）｜ `adversarial-thinking` 攻击自己
> （结论可能是错的）｜ `multi-perspective-review` 三视角会诊方案（架构 / 实现 / 系统三个权衡函数平行评估，
> 每视角强制给替代方案与优缺点对比，最后汇总裁决）。

---

## 能力说明 · 社区引入（保留上游结构，勿改正文）

| 技能 | 用途 | 外部依赖 |
|---|---|---|
| `wavedrom-gen` | 时序描述 → WaveJSON → SVG / PNG / HTML | Node 20+ + `npm ci` |

> 2026-09-27 清理：`advise-project-approach` / `neuroarxiv` / `using-codegraph` 三个在本工程从未用过，已删。

---

## F. 经验沉淀 —— 哪条教训住在哪

| 教训 | 住所 |
|---|---|
| **验收命令必须"能失败"**：判定读日志，不信 `%errorlevel%` 回显（cmd 在解析整行时就展开了它）；`\| findstr` 后的退出码是 findstr 的；GBK 管道里的中文 grep 恒空 | `piv-validate`「验证完整性」 |
| 用于决策的度量先人工抽验样本；"仅注释/格式"改动要给骨架等价证明；批量编辑后查副作用 | `piv-validate`「验证完整性」 |
| **"同一事实有两份"的地方必须机器同步**：控件类型表（`k_kinds` ↔ `KINDS`）、事件表、`.cgui` 字段语义、动作表 —— 靠 `tools\check-format.bat` 兜 | 项目根 `README.md` §3.4 + `tools\check-format.bat` → 已并入 `tools\gate.bat` |
| **文档与代码不一致时按代码改文档**，并把该补的测试补上 | 项目根 `README.md` |
| **技能文本会随上游模板腐坏**：2026-09-27 实测 8 个必经技能里残留 `idf.py build` / `ai_code_review.py` / 错误的"本项目"模型（含 `prime-codebase`、`piv-validate`） | 本文件 + `skills-create` 的维护清单 |
| 并行成员的"可失败验收 / 报告丢了什么 / 文件所有权互斥 / 逐条回执" | `team-parallel-delivery` |
| **最贵的错误不是逻辑写错，是"以为验证过了"**：支持库条数文档写 202、真值 292 而门禁全绿；`%errorlevel%` 回显 0 而门禁实际红；"undefined reference"实为示例 exe 被残留进程锁住 | `evidence-discipline` 伪证据清单 |
| **文档腐化比代码腐化隐蔽**：不挡编译、不触门禁、只伤害读它的人；一个事实只留一处权威，其它地方放指针 | `doc-truth-sync` |
| **提议者不能兼任裁定者**：一次输出给完"问题+裁定+修复"是自证自话；三角色分三次输出，驳回必须附反证出处；成立率 100% ⇒ 橡皮图章 | `adversarial-review` |
| **结论的质量不可能高于请求的质量**：缺素材 ⇒ 必然虚构；可推断的声明假设并继续，只问"猜错会返工"的阻塞项 | `request-spec` |
| **单视角会把多行抹平成一行**：三视角互斥才产生信息量；只批评不给替代方案 = 没做完；没有汇总裁决 = 没做完 | `multi-perspective-review` |

---

## 维护检查清单（改技能时逐条过）

- [ ] 新增 / 改名 / 删除技能 → 同步更新**本文件** + `global-rule.md`（§6 流程 / §7 映射）+ `README.md`
      —— **已机器化**：`node tools\check-skills.mjs`（并入 `tools\check-format.bat`）校验"目录 /
      本文件的总数 / 导读目录树 / 各 SKILL.md 的 frontmatter / `settings.json` 的 scopes"五处一致，
      并校验 **ROSTER 归属唯一**（每技能恰好一类、各类数量与分类表数字一致），
      且在别处引用**已删技能**时报红。人工还要管的只剩：`global-rule` §7 映射表里的领域措辞。
- [ ] **新增技能必须补一条路由用例**（`_registry/routing_cases.json`）—— `node tools\check-routing.mjs`
      会检查"触发词真在 description 里 / 无第二技能同时命中 / 每个技能被点名"。不补 ⇒ 门禁红。
      这条缺口以前是静默的：description 少写一个触发词，用户那样说时永远选不中，谁也不会发现。
- [ ] **命令形态以 `sop` §3 为唯一出处**：别处出现不同写法（`npx tsc` 不带 `cd designer`、
      `build-all` 不带 `.bat`、`tools/run-tests.bat` 用正斜杠）一律改回表里的样子
      —— 门禁命令写错 = 跑了个假门禁，而**没有任何命令会因此变红**。
- [ ] **产出目录必须真实存在**：`.codebuddy/plans/`、`.codebuddy/code-reviews/` 都已在库里
      （`code-reviews/` 曾用 6 处文档当产出位置却从未落盘，2026-10-07 补 `.gitkeep`）。
      新增报告类目录时先建目录，再写进技能正文。
- [ ] `description` 必须含**触发条件**，否则 agent 不知道何时调用
- [ ] 审查类技能 `auto_invoke: true`，且出现在 `global-rule.md` §7 映射表中
- [ ] 技能之间只写**引用名**（不复制正文）；跨技能引用的名字必须真实存在（无断链）
- [ ] **同一事实只允许一处权威定义**，其它地方引用路径
- [ ] 社区技能正文不改，只允许改必要适配
- [ ] 本文件顶部的**计数与日期**更新（各类数字之和 = `ls .codebuddy/skills/` 的目录数；
      目录数与**分类数字**均已由 `check-skills.mjs` 的 ROSTER 校验兜住，手写的第二份错了会红）
- [ ] **正文里的命令 / 路径 / 版本 / 数字逐条核对** —— 尤其"引用别处的现状"：
      数字最易腐坏，而**别处的命令会直接消失**（本文件 F 节最后一条就是实测）。
      **本工程没有的东西一律不得出现在技能正文里**：`idf.py`、`ai_code_review.py`、
      `CMakeLists.txt`、`sdkconfig`、多板型 / 子模块、`proj_*` 工程名。
      核对口径：命令必须能在本仓库的 `*.bat` / `tools\` / `package.json` 里找到。
- [ ] **从上游同步技能时**：在本文件头部记录"同步了什么 / 未采纳什么 / 为什么"；
      未采纳的固件专属技能不因"上游有"就恢复；同步来的 `SKILL.md` 头部标注来源与适配说明。
