# CodeBuddy AI 开发配置指南（v0.1）

> **项目**：CStackGUI（画布 + C 运行时）。这套体系源自嵌入式固件模板，**2026-09-27 已按本工程实际清理过**：
> 删掉 13 个用不到的技能（8 个纯固件审查、板子侧 `esp-idf-helper`、社区 3 个、输入侧压缩 `tokenzero`），
> 41 → **28 个**。留下的审查技能都是本工程真的会命中的：`firmware_code_style`（C 规范）、
> `buffer_overflow_check`、`memory_leak_check`、`error_handling_check`、`struct_best_practice_check`、
> `communication_protocol_check`（串口/网口是本工程核心能力），以及画布与 `.cgui` 的 `design-taste`。
> **用途**：AI 辅助编程的工作流体系 = 代码审查（7 个）+ 开发流程（PIV 双循环 13 个）+ 辅助工具（caveman / ponytail / analyzing-projects）+ 社区引入（1 个）+ 本工程专属（designer-modify / design-taste / git-commit-style / safe-file-ops / team-parallel-delivery），共 **28 个**技能目录（明细见下方目录树与 `INDEX.md`）
> **权威清单**：技能分类、触发条件与产出以 **`.codebuddy/skills/INDEX.md`** 为准（新增 / 改名 / **删除**技能必须同步该索引）。本文件的表格是**导读**，不重复维护细节 —— 2026-09-27 发现必经链路上 8 个技能残留上游模板内容（`idf.py build`、`ai_code_review.py`、错误的"本项目"模型），根因就是缺一份权威索引与维护清单。
> **核心价值**：质量门禁前置 + 端到端自动化 + 可追溯产出
> **维护**：请勿随意删除此文件夹，包含项目级配置、审查规则和工作流定义

---

## 🎯 这是什么？

这是一个 **完整的 AI 辅助开发体系**（v0.1），覆盖四大能力模块：

### 1️⃣ 质量保障（7 个审查技能）
- ✅ **自动检测** C 层与界面层的常见风险（缓冲区越界、内存泄漏、协议超时/粘包、错误路径漏处理、结构体对齐等）
- ✅ **强制执行**项目自己的规矩（`firmware_code_style` 的命名/注释/分层，`design-taste` 的界面观感与文案）
- ✅ **提交前审查**（质量门禁），在提交前发现并修复问题
- ✅ **统一代码风格**，确保跨端（`runtime/` + `designer/`）一致性

### 2️⃣ 开发工作流（13 个 PIV 循环技能）
- ✅ **需求 → 提交全流程自动化**（prime → plan → implement → validate → review → commit → PR）
- ✅ **3 个强制门禁**（计划门禁 + 验证门禁 + 审查门禁），确保质量
- ✅ **产出可追溯**（PRD/SDD/计划/审查报告），随时回溯
- ✅ **Validation-First**：先想清楚"怎么证明它工作"，再写代码

### 3️⃣ 辅助工具（3 个）
- ✅ **analyzing-projects**：陌生代码库系统性分析（结构/技术栈/数据流/约定）
- ✅ **caveman / ponytail**：输出侧 / 生成侧压缩（少说废话、少写代码）

### 4️⃣ 社区技能（1 个）
- ✅ **wavedrom-gen**：时序描述 → WaveJSON → SVG/PNG/HTML 波形图（协议/时序图用）

**简单理解**：就像给 AI 装上了"本工程的规范意识" + "项目经理的流程管控"，既能审查代码质量，又能管理整个开发流程。

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
    ├── [审查类] 6 个 C 代码质量检查技能（★ `design-taste` 列在下面[本工程专属]里）
    │   ├── buffer_overflow_check/        # 缓冲区溢出、数组越界、危险函数
    │   ├── communication_protocol_check/  # 通信协议：超时、CRC、粘包、字节序（本工程核心）
    │   ├── error_handling_check/         # 错误码、断言、日志、容错机制
    │   ├── firmware_code_style/          # 本工程 C 规范：命名、分层、Doxygen 注释、项目铁律
    │   ├── memory_leak_check/            # 内存泄漏、堆碎片、野指针
    │   └── struct_best_practice_check/   # 结构体：对齐、位域、内存优化
    ├── [流程类] 开发工作流技能（PIV 双循环）
    │   ├── plan-create-prd/          # 需求澄清 → PRD（含验收标准/非目标）
    │   ├── plan-architecture/        # 架构设计 → SDD（模块/数据流/接口契约）
    │   ├── piv-slice-epic/           # 大需求拆解为可独立验证的切片
    │   ├── prime-codebase/           # 加载代码库上下文（Context is King）
    │   ├── piv-plan-implementation/  # Validation-First 实现计划
    │   ├── piv-implement/            # 严格按计划实现（不做计划外改动）
    │   ├── piv-validate/             # 编译+静态+行为验证（门禁 1）
    │   ├── piv-review-changes/       # 提交前审查（按领域调用审查技能，门禁 2）
    │   ├── piv-create-pr/            # 创建 PR/MR（含验证情况与关联文档）
    │   │   （提交规范在根级 git-commit-style/：[类型][模块] 格式，2026-09-20 合并原 piv-commit）
    │   ├── piv-run-full-loop/        # 端到端全循环（描述→提交自动化）
    │   ├── skills-create/            # 创建/更新技能（元技能）
    │   ├── first-principles-thinking/ # 第一性原理思维准则（全局生效：需求澄清/最短路径/追根因/精简输出）
    │   ├── serial_verify/           # PC 侧真机行为验证（接入 piv-validate 第3级：起真窗口收发→回环/应答→异常注入）
    │   └── [工具类] 辅助技能
    │       ├── analyzing-projects/      # 陌生代码库系统性分析（结构/技术栈/数据流/约定，与 prime-codebase 分工）
    │       ├── caveman/                 # 输出侧压缩：少说（山顶洞人，lite/full/ultra/文言文，默认常驻）
    │       └── ponytail/                # 生成侧压缩：少写（决策梯子/YAGNI，默认 full 档，红线不砍）
    ├── [社区技能] 外部引入（2026-08-23 引入 4 个；2026-09-27 清理后只留 wavedrom-gen）
    │   └── wavedrom-gen/             # 时序描述 → WaveJSON → SVG/PNG/HTML（Datasheet 级 setup/hold 标注）
    └── [本工程专属] CStackGUI 自己的技能
        ├── designer-modify/          # 改画布 designer/ 的说明书（.cgui 契约、新增控件的 7~8 处、验证门禁）
        ├── design-taste/             # 设计品味门禁：设计读盘+三旋钮 / AI 味黑名单 / 质量地板 / 界面文案规则（画布与 .cgui 两端）
        ├── git-commit-style/         # 提交规范：[类型][模块] 描述（禁用 conventional-commit）
        ├── safe-file-ops/            # 非破坏性文件操作：能不删就不删，删除合并成一次并先说明（默认常驻，见 global-rule §5.3）
        └── team-parallel-delivery/   # 按文件域切分的并行小团队交付法（契约先行 / 逐条回执 / 按模块分批提交，见本工程四轮实践）
```

---

## ⚙️ 配置文件说明

### `settings.json` - 全局配置

```json
{
    "autoInvokeSkill": true,                // 自动触发审查技能
    "strictMode": true,                     // 严格模式：必须修复所有问题
    "codeLanguage": "C",                    // 项目主语言
    "scopes": ["desktop", "win32", "c", "cgui"],   // 2026-09-27 清理：原来是 ["embedded", "esp32", "rtos", "firmware"]
    "maxContextFileNum": 10,                // 最大上下文文件数
    "enableCodeReviewAutoScan": true,       // 启用自动扫描
    "disableUnreasonableRefactor": true,    // 禁止不合理的重构
    "preferChineseReply": true,             // 中文回复
    "excludePaths": ["build/", "output/", "release/", "designer/dist/", "node_modules/", "*.log"],
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

#### 1. **平台硬性规则**（本工程没有 MCU，2026-09-27 清理后只留通用项）
- 不改用户环境与系统级设置；系统符号运行期取（**零新增链接依赖**是本工程的硬约束）
- 平台行为以官方头文件/文档为准，不凭记忆下"系统不支持"的结论

#### 2. **安全规范**
- 数组、缓冲区必须做边界判断
- 禁止裸指针直接运算
- 接口签名必须与官方声明**逐参数一致**（运行期取符号 + 自制 typedef 是高危区）

#### 3. **资源与生命周期**（原 RTOS 规范，已换成非 MCU 版本）
- 句柄 / GDI 对象 / 窗口：创建即配对释放（`error_handling_check` 查资源泄漏）
- 栈上禁止放大数组（用全局/静态变量，或调用方提供的缓冲）

#### 4. **评审优先级**
```
安全风险 > 稳定性风险 > 性能 > 代码风格
```

---

## 🛠️ 审查技能库（14个专业技能，全部已深度完善 ✅）

### 内存与结构体类（3个）
| 技能名称 | 功能说明 | 检查项 | 完善状态 |
|---------|---------|--------|----------|
| `buffer_overflow_check` | 缓冲区溢出检测 | 10项（TOCTOU、整数溢出、格式化字符串） | ✅ 专家级 |
| `memory_leak_check` | 内存泄漏检测 | 12项（Double Free、堆碎片、野指针） | ✅ 专家级 |
| `struct_best_practice_check` | 结构体设计最佳实践 | 18种高级用法 | ✅ 已完善 |

### 协议与质量类（3个）
| 技能名称 | 功能说明 | 检查项 | 完善状态 |
|---------|---------|--------|----------|
| `communication_protocol_check` | 通信协议审查（串口/网口是本工程核心能力） | 9项（超时、CRC、粘包、字节序） | ✅ 专家级 |
| `error_handling_check` | 错误处理审查 | 9项（返回值检查、资源泄漏、恢复策略） | ✅ 专家级 |
| `firmware_code_style` | 代码规范检查 | 15类（命名、分层、注释、安全） | ✅ 专家级 |

### 界面类（1个）
| 技能名称 | 功能说明 | 检查项 | 完善状态 |
|---------|---------|--------|----------|
| `design-taste` | 界面观感与文案门禁（画布与 `.cgui` 两端） | 设计读盘+三旋钮、AI 味黑名单、质量地板 | ✅ 已完善 |

**📊 总计**：7 个审查技能约 70 项检查（缓冲区 10 / 内存 12 / 结构体 18 / 协议 9 / 错误处理 9 / C 规范 15 类），每项都含错误示例、正确示例、原理分析、后果说明

> **2026-09-27 清理**：原有 14 个专项审查技能里，8 个针对 MCU 的（DMA / 中断 / Flash / 外设 / 低功耗 / RTOS /
> 看门狗 / 栈用量）已删除 —— 本工程是 Win32/GDI+ 桌面工具包 + 画布，**没有 MCU**，只有"给板子写配套固件"
> 才用得到它们。要恢复：`git checkout <该次提交>^ -- .codebuddy/skills/<技能名>`。

---

## 🔄 开发工作流技能库（PIV 双循环框架）

在 7 个审查技能之上，新增一套 **PIV 双循环开发工作流**，让"需求 → 提交"全流程顺畅、可验证、可追溯。参考 coleam00/skills 的 PIV 循环框架，并结合本工程既有的基础设施定制：`build-all.bat`（编译 + 逐示例自检）、`tools\run-tests.bat`（C 层单测）、`tools\check-format.bat`（跨端契约回归）、画布的 `tsc --noEmit` + `vite build`。

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
│  编译+行为验证     审查(调审查技能)     原子提交       创建 PR/MR    │
└────────────────────────────────────────────────────────────────────┘
```

### 技能清单

| 环节 | 技能 | 功能说明 | 产出物 |
|------|------|---------|--------|
| 外层 | `plan-create-prd` | 需求澄清 → PRD（含验收标准/非目标） | `docs/PRD_*.md` |
| 外层 | `plan-architecture` | 架构设计（模块 / 数据流 / 接口契约） | `docs/SDD_*.md` |
| 外层 | `piv-slice-epic` | 大需求拆解为可独立验证/提交的切片 | `docs/SLICES_*.md` |
| 内层 | `prime-codebase` | 加载代码库上下文（Context is King） | 上下文总结 |
| 内层 | `piv-plan-implementation` | Validation-First 实现计划 | `.codebuddy/plans/*.md` |
| 内层 | `piv-implement` | 严格按计划实现，计划外改动需确认 | 代码改动 |
| 内层 | `piv-validate` | 编译（`build-all.bat`）+ 静态检查 + 行为验证（`--selftest` / 真机实测） | 验证结果清单 |
| 内层 | `piv-review-changes` | 提交前审查，按领域调用 7 个审查技能 | `.codebuddy/code-reviews/*.md` |
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
- ✅ 编译无新增错误/警告（`build-all.bat`；改了画布叠 `npx tsc --noEmit` + `npx vite build`）
- ✅ 行为验证通过（按计划的验证方法执行）
- ✅ 边界/异常路径覆盖（超时、错误帧、缓冲区满等）
- ❌ 不通过 → 回到 `piv-implement` 修复

#### 3. 审查门禁（`piv-review-changes`）
- ✅ 按改动领域自动调用 审查 skills（本工程适用的 7 个）
- ✅ 无 critical/high 级别问题，或已修复
- ✅ 产出审查报告到 `.codebuddy/code-reviews/`
- ❌ 不通过 → 回到 `piv-implement` / `piv-validate`

**规则**：门禁不通过 → 不能提交（`git-commit-style`）

### 审查 Skills 自动触发映射表

当执行 `piv-review-changes` 或 AI 生成代码时，按改动领域自动调用对应的审查技能。

**权威映射表**见 `.codebuddy/global-rule.md` 第 7 节，包含 7 个审查技能的领域映射与触发规则。

---

## 🚀 使用方式

### 1️⃣ **自动触发（推荐）**

AI 会自动识别代码场景并调用相应技能，你只需要正常写需求：

**示例 1：写串口收发 + 按帧取数据**
```
你："帮我写一个串口收发，按帧取数据，带超时重试"

AI 自动执行：
  1. 识别涉及：串口协议 + 缓冲区 + 错误处理
  2. 自动调用：
     - communication_protocol_check（超时机制、粘包、字节序）
     - buffer_overflow_check（缓冲区边界）
     - error_handling_check（重试与失败路径）
  3. 生成代码
  4. 自动审查并修复问题
  5. 输出最终代码 + 审查报告
```

**示例 2：改画布 / 加控件**
```
你："给日志视图加一个清空按钮，样式跟上边那排一致"

AI 自动调用：
  - design-taste（按钮四态、间距节奏、文案）
  - designer-modify（.cgui 契约：新增控件要动的 7~8 处）
  - error_handling_check（回调里的失败路径）
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
  4. piv-validate              → build-all.bat + 行为验证（再叠 run-tests / check-format）
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
   ✅ "给串口视图加一个按帧显示的开关，带超时重试"
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

### 📋 审查扫描清单（按改动领域）
| 层级 | 检查重点 | 对应 Skills | 风险等级 |
|------|---------|------------|----------|
| **第 1 层** | 内存安全（缓冲区溢出、整数溢出） | buffer_overflow_check | 🔴 致命 |
| **第 2 层** | 内存生命周期（泄漏、野指针、double free） | memory_leak_check | 🔴 致命 |
| **第 3 层** | 协议与通信（超时、粘包、校验、字节序） | communication_protocol_check | 🔴 致命 |
| **第 4 层** | 资源与错误路径（返回值、句柄/GDI 释放、恢复） | error_handling_check | 🟠 高危 |
| **第 5 层** | 结构与对齐（位域、pack、函数指针） | struct_best_practice_check | 🟠 高危 |
| **第 6 层** | 代码规范（命名、分层、Doxygen 注释） | firmware_code_style | 🟡 中 |
| **第 7 层** | 界面观感与文案（画布 / `.cgui`） | design-taste | 🟡 中 |

> 2026-09-27 更正：原表第 2/3/4/7 层是中断 / DMA / RTOS / 外设（MCU 专项），随那 8 个固件技能一起删去；
> 原分层依据 `ai_code_review.py` 也不是本工程的东西。

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

#### 4. 本工程适配
- **跨端一致**：一份 `.cgui` 描述同时驱动画布（Electron + React + TS）与 C 运行时（Win32/GDI+）
- **零新增链接依赖**：Win32 / WLAN / 蓝牙符号运行期取（`GetProcAddress`），用户项目不用改链接命令

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
- **C 层代码规范**：本工程 `firmware_code_style`（命名、分层、Doxygen 注释）
- **通信协议设计**：可靠性、超时、重传、切帧（`cg_frame_*`）
- **界面品味**：本工程 `design-taste`（设计读盘、AI 味黑名单、质量地板）

## 📖 技能详细文档

全部 **28 个**技能的权威清单（分类、触发条件、产出）见 `.codebuddy/skills/INDEX.md`；
规则正文在各技能目录的 `SKILL.md` 里（每项规则都有错误示例、正确示例、原理分析与后果说明）。

---

## 📝 更新日志

| 日期 | 迭代内容 | 说明 |
|------|---------|------|
| 2026-08-23 | v0.1 初始版本 | 1. 建立完整配置体系，审查规则 + 工作流定义 <br> 2. [完善审查技能]14 个专项审查技能深度完善，150+ 检查项覆盖 <br> 3. [PIV 双循环工作流]新增 12 个开发流程技能（PRD→架构→切片→实现→验证→审查→提交→PR），参考 coleam00/skills <br>  4. [结构化产出 + 原子验证]学习 Archify，新增 JSON Schema 验证机制（实现计划、审查报告）|
| 2026-08-23 | v0.1 新增社区技能 | 1. [社区技能]完整引入 4 个外部 skill（保留上游原结构，diff 校验一致）：<br> - advise-project-approach（项目策略咨询，无本地依赖）<br> - neuroarxiv（arXiv 先例查证，需联网）<br> - using-codegraph（tree-sitter 代码知识图谱，需 Python + 一次性安装 codegraph CLI）<br> - wavedrom-gen（时序波形图生成，需 Node 20+ 与 npm ci）|
| 2026-09-22 | v0.2 新增设计品味技能 | 1. [本工程专属]新增 `design-taste`：提炼 anthropics/frontend-design、pbakaus/impeccable(craft-floor)、Leonxlnx/taste-skill、nextlevelbuilder/ui-ux-pro-max、Owl-Listener/designer-skills、ibelick/ui-skills、davila7/claude-code-templates、carmahhawwari/ui-design-brain 八套设计技能，删去 web 专有部分后落到本项目的画布 UI 与 `.cgui` 桌面 App 两端<br> 2. [门禁]global-rule §7 映射表增加"界面观感/界面文案"一行，写界面即自动触发 |
| 2026-09-27 | v0.3 技能清理（41 → 28） | 1. [清理]删掉 13 个本工程用不到的技能：8 个纯固件审查（DMA/中断/栈/RTOS/Flash/外设/低功耗/看门狗）、板子侧 `esp-idf-helper`、社区 3 个（`neuroarxiv`/`using-codegraph`/`advise-project-approach`）、输入侧压缩 `tokenzero` <br> 2. [同步]`INDEX.md` 计数与分类、`global-rule.md` §7 映射表、本文件导读、5 个技能正文里的引用与举例一并更新（无断链）<br> 3. [依据]本工程是 Win32/GDI+ 桌面工具包 + 画布，没有 MCU（恢复：`git checkout <该次提交>^ -- .codebuddy/skills/<技能名>`） |

> **当前版本**：v0.1（持续完善中，版本号将在正式发布后更新）

---

## 👥 维护者

如有问题或建议，请联系项目负责人。

**重要提示**：请勿删除此文件夹，它是 AI 辅助编程的核心配置！

---

**愿代码无 Bug，固件永稳定！** 🚀
