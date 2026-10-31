---
name: skills-create
description: 创建或更新一个 CodeBuddy skill（SKILL.md），遵循本项目 skill 规范。触发条件：用户说"创建新skill"、"写个审查技能"、"更新XX skill"、"扩展工作流"时自动触发。
auto_invoke: true
argument-hint: [skill-name-and-purpose]
---

# 创建/更新 Skill

**新技能描述**：$ARGUMENTS

创建符合本项目规范的新 skill，或更新已有 skill。

## 规范总览

- **位置**：`skills/<团队目录>/<skill-name>/SKILL.md`
  —— 八团目录见 `_registry/teams.toml`；**新技能必须先登记进某个团**，不许再往 `skills/` 顶层加
- **命名**：小写 + 连字符（如 `buffer_overflow_check`、`piv-validate`）
- **格式**：YAML frontmatter + Markdown 正文（中文正文）
- **frontmatter 字段**：
  - `name`：技能名（与目录一致）
  - `description`：一句话说明用途 + 使用时机（agent 据此自动调用，必须写清楚"何时用"）
  - `argument-hint`：可选，声明接收参数，如 `[feature-description]`

## SKILL.md 模板

```markdown
---
name: <skill-name>
description: <用途一句话 + 明确的使用时机>
argument-hint: [<参数>]
---

# <标题>

**输入**：$ARGUMENTS

<一段话说明该技能做什么>

## 核心原则
- <1-3 条最重要的原则，宁缺毋滥>

## 流程/检查清单
### 步骤 1：<名称>
- <具体动作>
...

## 输出
<明确说明产出物是什么、保存到哪>
```

## 编写质量要求

1. **description 是灵魂**：必须包含"使用时机/触发条件"，否则 agent 不知道该何时调用。参考现有 skills：如 `communication_protocol_check` 写"审查串口、SPI、I2C、WiFi…自动触发"。
2. **可执行优先**：每个步骤要是可执行的动作，不要空话。
3. **清单化**：检查项用 `- [ ]`，让执行有勾稽感。
4. **有产出物**：明确输出文件路径与格式（本项目惯例：docs/、.codebuddy/plans/、.codebuddy/code-reviews/）。
5. **保持小而专注**：一个 skill 只做一件事。新流程若能复用现有 skill，就调用它而不是复制内容。

## 两类技能定位（本项目）

- **审查类**（已有 7 个）：C 代码与界面质量检查，如 `buffer_overflow_check`、`communication_protocol_check`（另有 `design-taste` 管界面观感）—— 被 `piv-review-changes` 按领域调用
- **流程类**（PIV 系列）：开发工作流，如 `piv-validate`、`git-commit-style` —— 定义"怎么做"的环节

## 创建流程

1. 确认需求：新技能解决什么问题？**归哪个团**（T0~T7，见 `expert-teams`）？
2. 检查是否已有类似技能（避免重复）
3. **先改编制**：在 `_registry/teams.toml` 对应团的 `skills` 列表里加名字
4. 用上述模板编写 SKILL.md，存到 `skills/<团队目录>/<name>/`
5. **补路由用例**：在 `_registry/routing_cases.json` 加一条（`node tools\check-routing.mjs` 会逼你补）
6. 运行 `python tools\check_roster.py --fix` 重渲染 INDEX 的 ROSTER；再跑
   `node tools\check-skills.mjs` + `node tools\check-routing.mjs`
7. 如涉及工作流变化，更新 `.codebuddy/README.md`
