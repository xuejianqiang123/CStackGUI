---
name: piv-plan-implementation
description: 为单个功能/切片制定详细实现计划，写入 .codebuddy/plans/。触发条件：用户提出明确的开发任务但尚未有计划、用户说"先做计划"、"怎么实现"，或即将进入 piv-implement 但缺少计划时强制触发。
auto_invoke: true
argument-hint: [feature-description-or-slice-name]
---

# 实现计划（Validation-First）

**功能描述**：$ARGUMENTS

动手写代码之前，先写一份**以验证为导向**的实现计划。

## 核心原则

- **Validation-First**：先想清楚"怎么证明它工作了"，再想"怎么实现"。
- **对终点具体，对手段放权**：目标文件、接口、验证方法明确；中间路径留白给实现者。
- **Inherit don't re-decide**：PRD/SDD/切片清单里已定的决策，直接继承，不要重新讨论。
- **计划要有"退路"**：每个步骤标明如果验证失败怎么排查。

## 计划模板

保存到 `.codebuddy/plans/<feature-name>.md`：

```markdown
# 实现计划：<功能名称>

## 目标
- 一句话描述要交付的行为

## 前置依赖
- 依赖的 PRD/SDD/切片、组件、硬件
- 是否已有上下文？（prime-codebase 是否已跑）

## 实现步骤（按序）
### 步骤 1：<动作>
- 改动文件：<路径>
- 改动内容：<要点>
- 验证方法：<如何证明>

### 步骤 2：<动作>
...

## 验证清单（Validation-First 核心）
- [ ] 编译通过且无新增警告
- [ ] <行为验证 1>
- [ ] <行为验证 2>
- [ ] 边界/异常路径验证（超时、错误帧、空数据）

## 风险与排查路径
- 风险：<可能失败的点>
- 排查：<如何定位>
```

## 计划质量检查

完成计划后自查：

1. 每个步骤都有**明确的验证方法**吗？（没有验证方法的步骤必须补充）
2. 改动文件路径是否明确？
3. 是否继承了 PRD/SDD 的既有决策而非重新发明？
4. 边界/异常路径是否覆盖？（嵌入式必查：超时、校验失败、缓冲区满、重入）

## 输出

### Markdown 格式（默认，人类可读）
保存到 `.codebuddy/plans/<feature-name>.md`，使用前述计划模板。

### JSON 格式（可选，结构化验证）
遵循 `.codebuddy/schemas/implementation-plan.schema.json`，用于自动化验证和工具集成。

**验证机制**（学习 Archify）：
- 计划生成后，按 schema 验证结构完整性
- 必须包含：每个步骤的验证方法、边界条件覆盖
- 验证失败 → 精确提示缺失字段
- 验证通过 → 才能进入 `piv-implement`
