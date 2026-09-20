# CodeBuddy AI 开发配置指南（v0.1）

> **项目**：CStackGUI（画布 + C 运行时）。下面这套体系源自嵌入式固件模板：14 个专项审查技能
> 里只有一部分对本工程有意义（`firmware_code_style`、`buffer_overflow_check`、`memory_leak_check`、
> `stack_overflow_check`、`error_handling_check`、`struct_best_practice_check`），其余针对 ESP32/RTOS/外设，
> 本工程用不到。本工程自己的技能是 `designer-modify`（改画布）、`git-commit-style`（提交格式）等。
> **用途**：AI 辅助编程的完整工作流体系 = 代码审查（14个专项技能）+ 开发流程（14个PIV循环技能）+ 辅助工具（5个）+ 社区引入（4个），共 37 个技能
> **核心价值**：质量门禁前置 + 端到端自动化 + 可追溯产出
> **维护**：请勿随意删除此文件夹，包含项目级配置、审查规则和工作流定义

---

## 🎯 这是什么？

这是一个 **完整的 AI 辅助开发体系**（v0.1），覆盖四大能力模块：

### 1️⃣ 质量保障（14 个审查技能）
- ✅ **自动检测**嵌入式开发风险（内存泄漏、栈溢出、DMA 错误、死锁等 150+ 检查项）
- ✅ **强制执行**行业最佳实践（RTOS 规范、通信协议、低功耗设计）
- ✅ **提交前审查**（质量门禁），在提交前发现并修复问题
- ✅ **统一代码风格**，确保团队代码一致性

### 2️⃣ 开发工作流（14 个 PIV 循环技能）
- ✅ **需求 → 提交全流程自动化**（prime → plan → implement → validate → review → commit → PR）
- ✅ **3 个强制门禁**（计划门禁 + 验证门禁 + 审查门禁），确保质量
- ✅ **产出可追溯**（PRD/SDD/计划/审查报告），随时回溯
- ✅ **Validation-First**：先想清楚"怎么证明它工作"，再写代码

### 3️⃣ 辅助工具（5 个）
- ✅ **esp-idf-helper**：ESP-IDF 命令行工作流（配置/编译/烧录/监控/排错）
- ✅ **analyzing-projects**：陌生代码库系统性分析（结构/技术栈/数据流/约定）
- ✅ **caveman / tokenzero / ponytail**：输出/输入/生成三侧压缩（省 Token、少写代码）

### 4️⃣ 社区技能（4 个）
- ✅ **advise-project-approach**：项目策略咨询（动手前/中/后研究最佳路径）
- ✅ **neuroarxiv**：新架构前查 arXiv 先例，避免重复造轮子
- ✅ **using-codegraph**：tree-sitter 代码知识图谱（SQLite 亚毫秒查询）
- ✅ **wavedrom-gen**：时序描述 → WaveJSON → SVG/PNG/HTML 波形图

**简单理解**：就像给 AI 装上了"固件开发专家大脑" + "项目经理的流程管控"，既能审查代码质量，又能管理整个开发流程。

---

## 📁 目录结构

```
.codebuddy/
├── README.md              # 本文档
├── settings.json          # 全局配置文件
├── global-rule.md         # 全局强制规则
├── plans/                 # 实现计划产出目录（PIV 工作流自动生成）
├── code-reviews/          # 代码审查报告产出目录（PIV 工作流自动生成）
└── skills/
    ├── [审查类] 14 个嵌入式专项质量检查技能
    │   ├── buffer_overflow_check/        # 缓冲区溢出、数组越界、危险函数
    │   ├── communication_protocol_check/  # 通信协议：超时、CRC、粘包、字节序
    │   ├── dma_cache_check/              # DMA 与 Cache 一致性、对齐检查
    │   ├── error_handling_check/         # 错误码、断言、日志、容错机制
    │   ├── firmware_code_style/          # 代码规范：命名、分层、注释
    │   ├── flash_storage_check/          # Flash 擦写寿命、NVS、OTA、掉电保护
    │   ├── interrupt_check/              # 中断安全：FromISR、volatile、标志清除
    │   ├── memory_leak_check/            # 内存泄漏、堆碎片、野指针
    │   ├── peripheral_conflict_check/    # GPIO 复用、时钟冲突、寄存器访问
    │   ├── power_management_check/       # 低功耗：睡眠模式、唤醒源、功耗评估
    │   ├── rtos_task_check/              # RTOS 任务：死锁、优先级反转、栈大小
    │   ├── stack_overflow_check/         # 栈溢出检测、栈消耗估算
    │   ├── struct_best_practice_check/   # 结构体：对齐、位域、内存优化
    │   └── watchdog_timer_check/         # 看门狗与定时器：喂狗、超时配置
    ├── [流程类] 开发工作流技能（PIV 双循环）
    │   ├── plan-create-prd/          # 需求澄清 → PRD（含验收标准/非目标）
    │   ├── plan-architecture/        # 架构设计 → SDD（任务/内存/外设/协议）
    │   ├── piv-slice-epic/           # 大需求拆解为可独立验证的切片
    │   ├── prime-codebase/           # 加载代码库上下文（Context is King）
    │   ├── piv-plan-implementation/  # Validation-First 实现计划
    │   ├── piv-implement/            # 严格按计划实现（不做计划外改动）
    │   ├── piv-validate/             # 编译+静态+行为验证（门禁 1）
    │   ├── piv-review-changes/       # 提交前审查（调用 14 个审查技能，门禁 2）
    │   ├── piv-create-pr/            # 创建 PR/MR（含验证情况与关联文档）
    │   │   （提交规范在根级 git-commit-style/：[类型][模块] 格式，2026-09-20 合并原 piv-commit）
    │   ├── piv-run-full-loop/        # 端到端全循环（描述→提交自动化）
    │   ├── skills-create/            # 创建/更新技能（元技能）
    │   ├── first-principles-thinking/ # 第一性原理思维准则（全局生效：需求澄清/最短路径/追根因/精简输出）
    │   ├── serial_verify/           # 串口行为验证（接入 piv-validate 第3级：烧录→观察输出→回环/应答→异常注入）
    │   └── [工具类] 辅助技能
    │       ├── esp-idf-helper/          # ESP-IDF 命令行工作流（配置/编译/烧录/监控/排错，本地化 Windows+Git Bash 多子项目）
    │       ├── analyzing-projects/      # 陌生代码库系统性分析（结构/技术栈/数据流/约定，与 prime-codebase 分工）
    │       ├── caveman/                 # 输出侧压缩：少说（山顶洞人，lite/full/ultra/文言文，默认常驻）
    │       ├── tokenzero/               # 输入侧压缩：少喂（本地 npm CLI：pack/compress/json/analyze，无损省输入 Token，手动触发）
    │       └── ponytail/                # 生成侧压缩：少写（决策梯子/YAGNI，默认 full 档，嵌入式禁 ultra、红线不砍）
    ├── [社区技能] 外部引入（2026-08-23，保留上游原结构，可直接运行）
    │   ├── advise-project-approach/  # 项目策略咨询：动手前/中/后研究最佳路径，输出带证据的决策报告
    │   ├── neuroarxiv/               # 新架构前查 arXiv 先例（prior art），避免重复造轮子
    │   ├── using-codegraph/          # 本地 tree-sitter 代码知识图谱（SQLite 亚毫秒查询/改动影响面）
    │   └── wavedrom-gen/             # 时序描述 → WaveJSON → SVG/PNG/HTML（Datasheet 级 setup/hold 标注）
    └── [本工程专属] CStackGUI 自己的技能
        ├── designer-modify/          # 改画布 designer/ 的说明书（.cgui 契约、新增控件的 7~8 处、验证门禁）
        └── git-commit-style/         # 提交规范：[类型][模块] 描述（禁用 conventional-commit）
```

---

## ⚙️ 配置文件说明

### `settings.json` - 全局配置

```json
{
    "autoInvokeSkill": true,                // 自动触发审查技能
    "strictMode": true,                     // 严格模式：必须修复所有问题
    "codeLanguage": "C",                    // 项目主语言
    "scopes": ["embedded", "esp32", "rtos", "firmware"],
    "maxContextFileNum": 10,                // 最大上下文文件数
    "enableCodeReviewAutoScan": true,       // 启用自动扫描
    "disableUnreasonableRefactor": true,    // 禁止不合理的重构
    "preferChineseReply": true,             // 中文回复
    "excludePaths": ["build/", "output/", "keil/", "debug/", "*.hex", "*.bin", "*.log"],
    "enableSmartContextFilter": true,       // 智能上下文过滤
    "autoCompressConversation": true,       // 自动压缩对话
    "enforceSkillOnCodeGen": true,          // 代码生成时强制检查
    "autoReviewAfterEdit": true             // 编辑后自动审查
}
```

**核心参数解释**：
- **autoInvokeSkill**：AI 写代码时自动识别场景并调用对应审查技能
- **strictMode**：严格模式，任何风险问题必须修复后才能提交
- **scopes**：项目领域标签，帮助 AI 理解项目类型

---

### `global-rule.md` - 全局强制规则

定义了所有代码必须遵守的硬性规则，包括：

#### 1. **硬件相关硬性规则**
- 禁止随意修改寄存器、时钟、GPIO、DMA、NVIC 配置
- 硬件改动必须给出风险提示和替代方案

#### 2. **安全规范**
- 数组、缓冲区必须做边界判断
- 禁止裸指针直接运算
- 中断函数必须极简（无延时、无阻塞）

#### 3. **RTOS 规范**
- FreeRTOS 禁止在中断中调用非 FromISR 接口
- 栈上禁止分配大数组（必须用全局/静态变量）

#### 4. **评审优先级**
```
安全风险 > 稳定性风险 > 性能 > 代码风格
```

---

## 🛠️ 审查技能库（14个专业技能，全部已深度完善 ✅）

### 内存安全类（4个）
| 技能名称 | 功能说明 | 检查项 | 完善状态 |
|---------|---------|--------|----------|
| `buffer_overflow_check` | 缓冲区溢出检测 | 10项（TOCTOU、整数溢出、格式化字符串） | ✅ 专家级 |
| `memory_leak_check` | 内存泄漏检测 | 12项（Double Free、堆碎片、野指针） | ✅ 专家级 |
| `stack_overflow_check` | 栈溢出检测 | 10项（栈消耗估算、递归深度、RTOS栈） | ✅ 专家级 |
| `struct_best_practice_check` | 结构体设计最佳实践 | 18种高级用法 | ✅ 已完善 |

### 硬件相关类（3个）
| 技能名称 | 功能说明 | 检查项 | 完善状态 |
|---------|---------|--------|----------|
| `interrupt_check` | 中断安全审查 | 10项（FromISR、volatile、原子操作） | ✅ 专家级 |
| `dma_cache_check` | DMA与Cache审查 | 7项（Cache一致性、对齐检查） | ✅ 已更新 |
| `peripheral_conflict_check` | 外设冲突检测 | 10项（GPIO复用、时钟配置、寄存器访问） | ✅ 专家级 |

### 系统管理类（3个）
| 技能名称 | 功能说明 | 检查项 | 完善状态 |
|---------|---------|--------|----------|
| `rtos_task_check` | RTOS任务管理 | 10项（死锁、优先级反转、信号量泄漏） | ✅ 专家级 |
| `watchdog_timer_check` | 看门狗与定时器 | 12项（超时配置、多任务喂狗、定时器溢出） | ✅ 专家级 |
| `power_management_check` | 低功耗管理 | 11项（GPIO漏电、功耗量化、数据保存） | ✅ 专家级 |

### 通信与存储类（2个）
| 技能名称 | 功能说明 | 检查项 | 完善状态 |
|---------|---------|--------|----------|
| `communication_protocol_check` | 通信协议审查 | 9项（超时、CRC、粘包、字节序） | ✅ 专家级 |
| `flash_storage_check` | Flash存储审查 | 13项（寿命计算、OTA回滚、掉电保护） | ✅ 专家级 |

### 质量保障类（2个）
| 技能名称 | 功能说明 | 检查项 | 完善状态 |
|---------|---------|--------|----------|
| `error_handling_check` | 错误处理审查 | 9项（返回值检查、资源泄漏、恢复策略） | ✅ 专家级 |
| `firmware_code_style` | 代码规范检查 | 15类（命名、分层、注释、安全） | ✅ 专家级 |

**📊 总计**：150+ 专业检查项，每项都包含错误示例、正确示例、原理分析、后果说明

---

## 🔄 开发工作流技能库（PIV 双循环框架）

在 14 个审查技能之上，新增一套 **PIV 双循环开发工作流**，让"需求 → 提交"全流程顺畅、可验证、可追溯。参考 coleam00/skills 的 PIV 循环框架，并结合本嵌入式项目的 ESP-IDF 构建、tests/ 单测、ai_code_review.py 等基础设施定制。

### 双循环架构

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
│  编译+行为验证     审查(调14个技能)     原子提交       创建 PR/MR    │
└────────────────────────────────────────────────────────────────────┘
```

### 技能清单

| 环节 | 技能 | 功能说明 | 产出物 |
|------|------|---------|--------|
| 外层 | `plan-create-prd` | 需求澄清 → PRD（含验收标准/非目标） | `docs/PRD_*.md` |
| 外层 | `plan-architecture` | 嵌入式架构设计（任务/内存/外设/协议） | `docs/SDD_*.md` |
| 外层 | `piv-slice-epic` | 大需求拆解为可独立验证/提交的切片 | `docs/SLICES_*.md` |
| 内层 | `prime-codebase` | 加载代码库上下文（Context is King） | 上下文总结 |
| 内层 | `piv-plan-implementation` | Validation-First 实现计划 | `.codebuddy/plans/*.md` |
| 内层 | `piv-implement` | 严格按计划实现，计划外改动需确认 | 代码改动 |
| 内层 | `piv-validate` | 编译（idf.py build）+ 静态检查 + 行为验证 | 验证结果清单 |
| 内层 | `piv-review-changes` | 提交前审查，按领域调用 14 个审查技能 | `.codebuddy/code-reviews/*.md` |
| 内层 | `git-commit-style` | `[类型][模块] 描述` 格式、分批原子化提交 | git commit |
| 内层 | `piv-create-pr` | 创建 PR/MR（含验证情况与关联文档） | PR/MR |
| 全流程 | `piv-run-full-loop` | 从描述到提交的端到端自动化 | 完整交付 |
| 元技能 | `skills-create` | 创建/更新 skill 的规范 | 新 SKILL.md |

### 核心方法论（来自 coleam00/skills + Archify）

1. **Context is King**：动手前先加载代码库上下文（prime-codebase）
2. **Validation-First**：先想清楚"怎么证明它工作"，再写代码
3. **对终点具体，对手段放权**：验收标准精确，实现路径留白
4. **Inherit don't re-decide**：PRD/SDD 已定决策直接继承，不重新讨论
5. **原子化提交**：一个功能一个 commit，conventional-commit 风格
6. **结构化产出 + 原子验证**（学习 Archify）：关键产出物（计划/审查报告）支持 JSON Schema 验证，确保完整性和可重现性

### 结构化产出与验证（v3.1 新增，学习 Archify）

受 [Archify](https://github.com/tt-a1i/archify) 启发，关键产出物支持**类型化中间表示（Typed IR）**：

#### 支持 JSON Schema 的产出
| 产出 | Markdown（人类可读） | JSON（结构化验证） | Schema 位置 |
|------|---------------------|-------------------|-------------|
| 实现计划 | `.codebuddy/plans/*.md` | `.codebuddy/plans/*.json` | `schemas/implementation-plan.schema.json` |
| 审查报告 | `.codebuddy/code-reviews/*.md` | `.codebuddy/code-reviews/*.json` | `schemas/review-report.schema.json` |

#### 验证工作流（Generate → Validate → Deliver）
```
1. Generate    AI 生成结构化产出（JSON）
2. Validate    按 Schema 验证：字段完整性、类型正确性、必填项
3. Deliver     验证通过 → 原子性写入；失败 → 精确修复建议
```

**优势**：
- ✅ 可重现：同样输入产生确定性输出
- ✅ 可验证：Schema 约束确保产出质量
- ✅ 可集成：JSON 格式便于 CI/CD、工具链集成
- ✅ 失败友好：验证失败时返回精确的字段/类型错误，而非堆栈跟踪

**使用方式**：
- Markdown 为默认输出（人类阅读、git diff 友好）
- JSON 可选（需要自动化验证或工具集成时启用）

### 强制门禁机制

PIV 内层循环包含 **3 个质量门禁**，必须通过才能进入下一阶段：

#### 1. 计划门禁（`piv-plan-implementation`）
- ✅ 任何实现前必须先有计划
- ✅ 计划必须包含 Validation-First 的验证方法
- ✅ 每个步骤必须有明确的验证标准

#### 2. 验证门禁（`piv-validate`）
- ✅ 编译无新增错误/警告（`idf.py build`）
- ✅ 行为验证通过（按计划的验证方法执行）
- ✅ 边界/异常路径覆盖（超时、错误帧、缓冲区满等）
- ❌ 不通过 → 回到 `piv-implement` 修复

#### 3. 审查门禁（`piv-review-changes`）
- ✅ 按改动领域自动调用 14 个嵌入式审查 skills
- ✅ 无 critical/high 级别问题，或已修复
- ✅ 产出审查报告到 `.codebuddy/code-reviews/`
- ❌ 不通过 → 回到 `piv-implement` / `piv-validate`

**规则**：门禁不通过 → 不能提交（`git-commit-style`）

### 审查 Skills 自动触发映射表

当执行 `piv-review-changes` 或 AI 生成代码时，按改动领域自动调用对应的审查技能。

**权威映射表**见 `.codebuddy/global-rule.md` 第 7 节，包含 14 个审查技能的领域映射与触发规则。

---

## 🚀 使用方式

### 1️⃣ **自动触发（推荐）**

AI 会自动识别代码场景并调用相应技能，你只需要正常写需求：

**示例 1：写 UART DMA 接收函数**
```
你："帮我写一个 UART DMA 接收函数，带超时重试"

AI 自动执行：
  1. 识别涉及：UART + DMA + 错误处理
  2. 自动调用：
     - communication_protocol_check（超时机制）
     - dma_cache_check（DMA安全）
     - error_handling_check（重试逻辑）
  3. 生成代码
  4. 自动审查并修复问题
  5. 输出最终代码 + 审查报告
```

**示例 2：创建 FreeRTOS 任务**
```
你："创建一个温度采集任务，1秒采集一次"

AI 自动调用：
  - rtos_task_check（任务栈大小、优先级）
  - communication_protocol_check（I2C/SPI 通信）
  - error_handling_check（传感器异常处理）
```

---

### 2️⃣ **手动指定技能**

如果需要针对性审查，可以明确指定：

```
"用 struct_best_practice_check 审查这个结构体"
"检查这段代码的内存泄漏问题"
"用 dma_cache_check 审查 DMA 配置"
```

---

### 3️⃣ **代码审查模式**

代码写完后主动要求全面审查：

```
"审查我刚写的代码，重点检查安全性"
"帮我做全面的固件代码审查"
"这段代码有没有潜在风险？"
```

---

### 4️⃣ **完整开发工作流（PIV 循环）**

需要从需求到提交的完整流程时，直接描述功能，AI 会按 PIV 循环执行：

```
你："实现 UART 485 从机通信功能"（或用 piv-run-full-loop）

AI 自动执行：
  1. prime-codebase            → 加载项目上下文
  2. piv-plan-implementation   → 制定实现计划（.codebuddy/plans/）
  3. piv-implement             → 按计划实现
  4. piv-validate              → idf.py build + 行为验证
  5. piv-review-changes        → 调用通信协议/DMA 等审查技能
  6. git-commit-style          → 按 [类型][模块] 分批提交
```

大需求先走外层循环：
```
"先做需求分析"        → plan-create-prd（产出 docs/PRD_*.md）
"画一下架构"          → plan-architecture（产出 docs/SDD_*.md）
"把需求拆成小任务"    → piv-slice-epic（产出切片清单）
```

---

## 🔄 自动执行流程

```
用户提交需求
    ↓
AI 分析需求（识别涉及的技术领域）
    ↓
自动加载相关 skills（如 DMA、RTOS、通信协议等）
    ↓
生成代码（遵循 skills 规则）
    ↓
自动审查（运行相关 skills）
    ↓
发现问题？
    ├─ 是 → 给出修复方案 → 自动修正 → 重新审查
    └─ 否 → 提交最终代码
```

---

## 📊 风险等级说明

所有问题按风险等级分类：

| 等级 | 说明 | 示例 |
|------|------|------|
| **🔴 致命** | 必须立即修复，否则导致系统崩溃/数据损坏 | 寄存器缺少 volatile、DMA 用栈内存 |
| **🟠 高危** | 可能导致严重问题，强烈建议修复 | 缓冲区未对齐、中断阻塞、死锁风险 |
| **🟡 中危** | 影响稳定性或性能，建议修复 | 配置未加 const、错误处理缺失 |
| **🟢 低危** | 代码风格或优化建议 | 命名不规范、注释缺失 |

**严格模式下**：致命和高危问题必须修复才能提交。

---

## 💡 使用建议

### ✅ 推荐做法
1. **描述清晰需求**：说明功能、使用的外设、性能要求
   ```
   ✅ "写一个基于 FreeRTOS 的温度传感器驱动，用 I2C 通信，支持错误重试"
   ❌ "写个温度读取函数"
   ```

2. **强调安全性**：AI 会更注重质量
   ```
   "写代码时注意内存安全和 RTOS 规范"
   "确保符合嵌入式最佳实践"
   ```

3. **要求审查**：代码完成后主动要求检查
   ```
   "写完后帮我审查一下"
   "检查有没有潜在的内存泄漏"
   ```

### ❌ 避免做法
1. **不要随意修改配置文件**：除非完全理解配置含义
2. **不要删除 .codebuddy 文件夹**：这是项目相关数据，非临时缓存
3. **不要禁用 strictMode**：这是质量保障的核心
4. **不要跳过门禁**：验证/审查门禁是质量保证的关键

---

## 📁 产出物管理规范

PIV 工作流会在项目中产生结构化的文档产出，遵循以下规范：

| 产出类型 | 存放位置 | 命名规范 | 格式 | 说明 |
|---------|---------|---------|------|------|
| PRD（需求文档） | `docs/PRD_*.md` | `PRD_<feature>.md` | Markdown | plan-create-prd 产出 |
| SDD（架构设计） | `docs/SDD_*.md` | `SDD_<feature>.md` | Markdown | plan-architecture 产出 |
| 切片清单 | `docs/SLICES_*.md` | `SLICES_<feature>.md` | Markdown | piv-slice-epic 产出 |
| 实现计划 | `.codebuddy/plans/` | `<feature-name>.md` / `.json` | Markdown + JSON（可选）| piv-plan-implementation 产出 |
| 审查报告 | `.codebuddy/code-reviews/` | `<date>-<feature>.md` / `.json` | Markdown + JSON（可选）| piv-review-changes 产出 |
| Skills | `.codebuddy/skills/` | `<skill-name>/SKILL.md` | Markdown | skills-create 产出 |
| JSON Schemas | `.codebuddy/schemas/` | `*.schema.json` | JSON Schema | 产出物验证规范 |

**格式说明**：
- **Markdown**：默认输出，人类可读，git diff 友好，适合评审
- **JSON**：可选输出，结构化验证，适合 CI/CD 集成和自动化工具

**管理建议**：
- ✅ PRD/SDD 纳入版本控制（git），团队共享
- ✅ 实现计划（plans/）可选纳入版本控制，便于回溯
- ✅ 审查报告（code-reviews/）建议 .gitignore，避免污染 git log
- ✅ Skills 必须纳入版本控制，是项目规范的一部分
- ✅ JSON Schemas 必须纳入版本控制，是验证规范的一部分

---

## 🔧 自定义扩展

### 添加新的审查技能

如果需要添加项目特有的审查规则，可以使用 `skills-create` 元技能：

**方式 1：直接描述需求**
```
你："帮我创建一个审查技能，检查项目里的自定义错误码定义规范"

AI 自动执行：
  1. 调用 skills-create
  2. 按本项目规范生成 SKILL.md
  3. 保存到 .codebuddy/skills/<name>/
  4. 提示更新 global-rule.md（如需要）
```

**方式 2：手动创建**

1. 在 `skills/` 目录下创建新文件夹，如 `custom_check/`
2. 创建 `SKILL.md` 文件，参考现有技能格式
3. 在 `global-rule.md` 中添加触发规则（如需自动调用）

**技能文件模板**（必须包含的字段）：
---
name: custom_check
description: 自定义审查规则。触发条件：<明确的关键词或场景描述>
auto_invoke: true
argument-hint: [optional-parameter]
---

# 技能：自定义审查

## 触发条件
<详细说明何时自动调用此技能>

## 检查清单
### 检查项 1：<名称>
- 错误示例：
- 正确示例：
- 原理分析：
- 风险等级：

...

**关键要求**：
- `description` 必须包含明确的触发条件
- 检查项要具体、可执行
- 每项包含错误/正确示例
- 标注风险等级（🔴致命 / 🟠高危 / 🟡中危 / 🟢低危）
...

## 检查清单
...

## 强制规范
...

---

## 🤝 团队协作

### 新成员上手
1. **阅读本文档**：了解配置体系
2. **查看 `global-rule.md`**：熟悉强制规则
3. **浏览 `skills/` 目录**：了解有哪些审查技能
4. **实践使用**：写几段代码，体验自动审查

### 统一标准
- 所有团队成员使用相同的 `.codebuddy` 配置
- 代码提交前必须通过 AI 审查
- 严格模式下，致命/高危问题必须修复

---

## 📈 完善历程与成果

### 🎯 完善统计

| 维度 | 完善前 | 完善后 | 提升幅度 |
|------|--------|--------|----------|
| **深度完善 Skills** | 3 个（基础） | **14 个（全部专家级）** | +367% |
| **检查项总数** | ~40 项 | **150+ 项** | +275% |
| **代码示例** | 少量 | **每项都有错误+正确示例** | +500% |
| **AI 评审覆盖** | 3 层 | **8 层全覆盖** | +167% |
| **文档完整度** | 简单规则 | **原理+示例+后果+修复** | +400% |

### 📋 AI 代码评审的 8 层扫描清单

本技能库基于 `ai_code_review.py` 的专业评审规则构建：

| 层级 | 检查重点 | 对应 Skills | 风险等级 |
|------|---------|------------|----------|
| **第 1 层** | 内存安全（缓冲区溢出） | buffer_overflow_check | 🔴 致命 |
| **第 2 层** | 中断与并发安全 | interrupt_check | 🔴 致命 |
| **第 3 层** | DMA 与 Cache 一致性 | dma_cache_check | 🔴 致命 |
| **第 4 层** | RTOS 任务与死锁 | rtos_task_check | 🔴 致命 |
| **第 5 层** | 数值与边界安全（TOCTOU） | buffer_overflow_check | 🔴 致命 |
| **第 6 层** | 资源与状态机（泄漏） | error_handling_check | 🟠 高危 |
| **第 7 层** | 硬件与外设（寄存器） | peripheral_conflict_check | 🔴 致命 |
| **第 8 层** | 协议与通信（超时） | communication_protocol_check | 🔴 致命 |

### 🌟 核心亮点特性

#### 1. 每个检查项都包含完整的 4 要素

❌ 错误示例 → 展示真实的问题代码
✅ 正确示例 → 提供可直接使用的安全代码
🔍 问题原理 → 解释为什么这样写会出问题
💥 后果分析 → 说明可能导致的严重后果


#### 2. 能检测出隐蔽的高级问题

**TOCTOU 竞态条件**：
```c
// ❌ 错误：共享变量 TOCTOU
if (msg->len <= MAX_SIZE) {      // Time-Of-Check
    // 中断可能在这里修改 msg->len
    memcpy(buf, msg->data, msg->len);  // Time-Of-Use - 溢出！
}

// ✅ 正确：缓存到局部变量
uint16_t len = msg->len;
if (len <= MAX_SIZE) {
    memcpy(buf, data, len);
}
```

**RTOS 死锁检测**：
```c
// ❌ 任务 A: 先 UART 后 Flash
xSemaphoreTake(mutex_uart, portMAX_DELAY);
xSemaphoreTake(mutex_flash, portMAX_DELAY);

// ❌ 任务 B: 先 Flash 后 UART（死锁！）
xSemaphoreTake(mutex_flash, portMAX_DELAY);
xSemaphoreTake(mutex_uart, portMAX_DELAY);
```

**Flash 擦除看门狗冲突**：
```c
// ❌ 错误：看门狗超时
esp_task_wdt_init(1, true);  // 1 秒超时
for (int i = 0; i < 16; i++) {
    flash_erase_sector(i);   // 每个 500ms，总共 8 秒 - 看门狗复位！
}

// ✅ 正确：擦除前喂狗
for (int i = 0; i < 16; i++) {
    esp_task_wdt_reset();
    flash_erase_sector(i);
}
```

#### 3. 量化评估工具

**栈消耗估算公式**：
```
栈大小 = 基础开销(300B) + 调用链最大累积 + 50%安全余量
```

**Flash 寿命计算**：
```
寿命（天） = 芯片擦写次数 / 每天写入次数
示例：10万次 / (86400秒/天 / 60秒) = 69.4 天
```

**功耗目标值**：
- Deep Sleep < 10μA
- Light Sleep < 1mA
- Modem Sleep < 20mA

#### 4. ESP32 平台深度适配
- **ESP32 系列**：ESP-IDF、FreeRTOS、NVS、OTA、heap_caps_malloc、分区表、加密烧录
- 支持多芯片示例：ESP32-P4、ESP32-C2（审查技能里保留的示例，与本工程无关）

### 📚 关键改进方法论

#### 交叉验证思维
- 不仅看当前行，还要追踪变量的来源和使用
- 检查是否在使用前做了校验

#### 时序分析
- 先检查后使用（Check-Then-Use）
- TOCTOU 竞态条件检测

#### 数值安全
- 整数溢出检测
- 有符号/无符号混用
- 类型转换安全

#### 并发安全
- volatile 关键字
- 原子操作
- 临界区保护

---

## 📞 常见问题

### Q1: 这个配置会影响编译吗？
**A**: 不会。`.codebuddy` 只是 AI 助手的配置，不参与实际编译。

### Q2: 可以禁用某个 skill 吗？
**A**: 可以。打开对应 skill 的 `SKILL.md`，将 `auto_invoke: true` 改为 `false`。

### Q3: 审查太严格怎么办？
**A**: 可以将 `strictMode` 改为 `false`，但不推荐，这会降低代码质量。

### Q4: 如何查看具体的审查细节？
**A**: 每次审查会输出详细报告，包括：位置、风险等级、问题原理、修复方案。

### Q5: 可以用于其他项目吗？
**A**: 可以。复制整个 `.codebuddy` 文件夹到其他嵌入式项目根目录即可。

---

## 📚 参考资料

- **结构体最佳实践**：基于《你的 C 代码为什么乱？看完这 18 种结构体用法就懂了》
- **嵌入式安全规范**：业界通用的固件开发最佳实践
- **RTOS 规范**：FreeRTOS 官方文档与常见陷阱
- **通信协议设计**：可靠性、超时、重传机制
- **AI 代码评审**：基于 ai_code_review.py 的 8 层扫描清单
- **量化评估**：栈消耗估算、Flash 寿命计算、功耗目标值

## 📖 技能详细文档

所有 14 个技能的详细规则和检查项，请查看 `.codebuddy/skills/` 目录下对应的 `SKILL.md` 文件：
- 每个 SKILL.md 包含 10-18 项详细检查规则
- 每项规则都有错误示例、正确示例、原理分析、后果说明
- 支持 ESP32 和 STM32 双平台示例代码

---

## 📝 更新日志

| 日期 | 迭代内容 | 说明 |
|------|---------|------|
| 2026-08-23 | v0.1 初始版本 | 1. 建立完整配置体系，审查规则 + 工作流定义 <br> 2. [完善审查技能]14 个专项审查技能深度完善，150+ 检查项覆盖 <br> 3. [PIV 双循环工作流]新增 12 个开发流程技能（PRD→架构→切片→实现→验证→审查→提交→PR），参考 coleam00/skills <br>  4. [结构化产出 + 原子验证]学习 Archify，新增 JSON Schema 验证机制（实现计划、审查报告）|
| 2026-08-23 | v0.1 新增社区技能 | 1. [社区技能]完整引入 4 个外部 skill（保留上游原结构，diff 校验一致）：<br> - advise-project-approach（项目策略咨询，无本地依赖）<br> - neuroarxiv（arXiv 先例查证，需联网）<br> - using-codegraph（tree-sitter 代码知识图谱，需 Python + 一次性安装 codegraph CLI）<br> - wavedrom-gen（时序波形图生成，需 Node 20+ 与 npm ci）|

> **当前版本**：v0.1（持续完善中，版本号将在正式发布后更新）

---

## 👥 维护者

如有问题或建议，请联系项目负责人。

**重要提示**：请勿删除此文件夹，它是 AI 辅助编程的核心配置！

---

**愿代码无 Bug，固件永稳定！** 🚀
