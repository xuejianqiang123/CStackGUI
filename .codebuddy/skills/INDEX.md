# 技能索引（SKILL INDEX）——唯一权威清单

> 本文件是 `.codebuddy/skills/` 下全部技能的**单一权威索引**。
> 新增 / 删除 / 改名技能时：**先改本文件**，再改 `global-rule.md`（触发规则）与 `README.md`（导读）。
> 维护约定：本文件只写"是什么 / 何时触发 / 产出"，具体规则一律以对应 `SKILL.md` 正文为准，不在此重复。
>
> 更新：2026-09-27（清理：删 13 个本工程用不到的技能，41 → 28）｜ 技能总数：**28** ｜ 口径：`ls .codebuddy/skills/` 的目录数
> **★ = 本工程专属**（其余是从嵌入式模板继承来的，见每类下的适用说明）

---

## 分类总览

| 类 | 数量 | 作用 | 何时被用到 |
|---|---|---|---|
| **A 审查类** | 7 | 代码质量门禁（由 `piv-review-changes` 按领域调用，映射表见 `global-rule.md` §7） | 写码后 / 提交前 |
| **B 流程类（PIV 双循环）** | 13 | 需求 → 提交的开发工作流与 3 道门禁 | 每次开发 |
| **C 本工程专属规程** | 1 | `designer-modify`：改画布的说明书 | 改 `designer/` 时 |
| **D 正交约束与工具** | 6 | 压缩、思维准则、文件安全、真机实测、辅助 | 始终生效 / 按需 |
| **E 社区引入** | 1 | 外部技能（保留上游结构，勿改正文） | 按需显式调用 |

> 7 + 13 + 1 + 6 + 1 = **28**。
> `designer-modify` 单独成类，是因为它既不是"审查"也不是"流程步骤"；其余几个 ★ 技能
> 按功能归在上面各自的类里。
>
> **2026-09-27 清理记录**：删掉 13 个本工程用不到的技能 —— 8 个纯固件审查（`dma_cache_check` /
> `interrupt_check` / `stack_overflow_check` / `rtos_task_check` / `flash_storage_check` /
> `peripheral_conflict_check` / `power_management_check` / `watchdog_timer_check`）、板子侧索引
> `esp-idf-helper`、社区技能 3 个（`neuroarxiv` / `using-codegraph` / `advise-project-approach`）、
> 输入侧压缩 `tokenzero`。口径：本工程是 Win32/GDI+ 桌面工具包 + 画布，**没有 MCU**，
> 被删的都是"给板子写固件"才用得到的。恢复办法：`git checkout <该次提交>^ -- .codebuddy/skills/<技能名>`。

---

## A. 审查类（7）—— 质量门禁执行体

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

## B. 流程类（PIV 双循环，13）

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
| `piv-run-full-loop` | 小范围明确功能需求，未要求分步 | 自动走完内层循环 0→5 |
| ★ `team-parallel-delivery` | 并行 / 多任务 / 需求落在多个互不重叠的域 | 按文件域切分的并行交付（契约先行、逐条回执、按模块分批提交） |
| `skills-create` | 创建 / 更新技能 | 技能规范与模板（元技能） |

---

## C. 本工程专属规程（1）

| 技能 | 触发条件 | 关键约束 |
|---|---|---|
| ★ `designer-modify` | 改 `designer/` 下的任何代码 | 画布（Electron + React + TS）的完整说明书：文件地图、`.cgui` 契约、新增控件要动的 7~8 处、验证门禁 |

---

## D. 正交约束与工具（6）

| 技能 | 作用层 | 触发 |
|---|---|---|
| `first-principles-thinking` | 决策层（始终生效） | 任何需求 / 方案 / 排查 / 输出 |
| `caveman` | 输出侧（少说） | 常驻；`lite/full/ultra/wenyan/off` |
| `ponytail` | 生成侧（少写） | 常驻 full；`lite/full/ultra/off` |
| ★ `safe-file-ops` | 文件操作（非破坏） | 准备删除 / 清理 / 覆盖时 |
| `serial_verify` | 验证侧（真机实测） | `piv-validate` 第 3 级；GUI / 收发 / 交互类改动 |
| `analyzing-projects` | 认知侧（陌生代码库） | 接手新项目 / 陌生子模块 / "这块怎么工作的" |

> 两侧压缩正交：`ponytail` 少写代码 ｜ `caveman` 少说废话。（输入侧压缩 `tokenzero` 随 2026-09-27 清理移除；
> 板子侧参考 `esp-idf-helper` 同样已删 —— 本工程不含固件。）

---

## E. 社区引入（1）—— 保留上游结构，勿改正文

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
| **"同一事实有两份"的地方必须机器同步**：控件类型表（`k_kinds` ↔ `KINDS`）、事件表、`.cgui` 字段语义、动作表 —— 靠 `tools\check-format.bat` 兜 | 项目根 `README.md` §3.4 + `tools/check-format.bat` |
| **文档与代码不一致时按代码改文档**，并把该补的测试补上 | 项目根 `README.md` |
| **技能文本会随上游模板腐坏**：2026-09-27 实测 8 个必经技能里残留 `idf.py build` / `ai_code_review.py` / 错误的"本项目"模型（含 `prime-codebase`、`piv-validate`） | 本文件 + `skills-create` 的维护清单 |
| 并行成员的"可失败验收 / 报告丢了什么 / 文件所有权互斥 / 逐条回执" | `team-parallel-delivery` |

---

## 维护检查清单（改技能时逐条过）

- [ ] 新增 / 改名 / 删除技能 → 同步更新**本文件** + `global-rule.md`（§6 流程 / §7 映射）+ `README.md`
      —— **已机器化**：`node tools\check-skills.mjs`（并入 `tools\check-format.bat`）校验"目录 /
      本文件的总数 / 导读目录树 / 各 SKILL.md 的 frontmatter / `settings.json` 的 scopes"五处一致，
      并在别处引用**已删技能**时报红。人工还要管的是它查不到的：分类归属、映射表里的领域措辞。
- [ ] `description` 必须含**触发条件**，否则 agent 不知道何时调用
- [ ] 审查类技能 `auto_invoke: true`，且出现在 `global-rule.md` §7 映射表中
- [ ] 技能之间只写**引用名**（不复制正文）；跨技能引用的名字必须真实存在（无断链）
- [ ] **同一事实只允许一处权威定义**，其它地方引用路径
- [ ] 社区技能正文不改，只允许改必要适配
- [ ] 本文件顶部的**计数与日期**更新（各类数字之和 = `ls .codebuddy/skills/` 的目录数；
      目录数已由 `check-skills.mjs` 机器校验，分类数字仍需人工核对）
- [ ] **正文里的命令 / 路径 / 版本 / 数字逐条核对** —— 尤其"引用别处的现状"：
      数字最易腐坏，而**别处的命令会直接消失**（本文件 F 节最后一条就是实测）。
      **本工程没有的东西一律不得出现在技能正文里**：`idf.py`、`ai_code_review.py`、
      `CMakeLists.txt`、`sdkconfig`、多板型 / 子模块、`proj_*` 工程名。
      核对口径：命令必须能在本仓库的 `*.bat` / `tools\` / `package.json` 里找到。
