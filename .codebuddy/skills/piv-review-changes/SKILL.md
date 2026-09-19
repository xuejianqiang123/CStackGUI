---
name: piv-review-changes
description: 提交前对全部改动做技术审查（质量门禁），产出审查报告到 .codebuddy/code-reviews/。触发条件：piv-validate 通过后、用户说"审查代码"、"code review"、"检查质量"，或准备提交（git-commit-style）前强制触发。会自动调用本项目的 14 个嵌入式审查 skills。
auto_invoke: true
---

# 提交前代码审查（质量门禁）

对全部未提交改动执行技术审查，发现问题在提交前修复。

## 第一步：确认改动范围

```bash
git status
git diff HEAD
git diff --stat HEAD
git ls-files --others --exclude-standard   # 新增文件
```

明确：改了哪些文件、新增哪些文件、删了哪些文件。

## 第二步：加载审查上下文

- 阅读 `.codebuddy/global-rule.md` 与相关 `docs/`（PRD/SDD）
- 阅读改动涉及的组件/模块既有代码风格
- 读取每个改动文件**全文**（不要只看 diff），理解完整上下文

## 第三步：按层级审查（本项目特色）

对每个改动文件，按以下层级依次执行：

### 通用技术审查（任何改动）
- 逻辑错误：边界条件、off-by-one、错误处理缺失、竞态
- 性能：不必要的计算、内存浪费、重复读取
- 代码质量：DRY、命名、复杂度、注释完整性

### 嵌入式专项审查（调用对应 skills）
按改动涉及的领域，加载并执行对应的审查技能。

**权威映射表**见 `.codebuddy/global-rule.md` 第 7 节"嵌入式审查 Skills 自动触发规则"，包含 14 个审查技能的完整领域映射与检查重点。

> 规则：改动涉及哪类代码，就加载对应 skill 执行其完整检查清单，而不是凭经验泛泛而过。

### 与项目标准的符合性
- 命名规范、头文件组织、日志规范（参考 `firmware_code_style`）
- 是否遵循 docs/ 中约定的设计

## 第四步：验证问题真实性

对每个发现的问题：
- 确认不是误报（读取相关代码验证）
- 能复现的 bug 优先修
- 标注严重级别：critical / high / medium / low

## 输出报告

保存到 `.codebuddy/code-reviews/<日期>-<功能名>.md`（Markdown）和可选的 `.json`（结构化）：

### Markdown 格式（默认，人类可读）
```markdown
# 代码审查报告：<功能名>（<日期>）

## 统计
- 修改文件：N / 新增：N / 删除：N
- 净增行数：+N / 净删行数：-N

## 审查发现
### [severity: critical|high|medium|low]
- 文件：<路径>
- 位置：<函数/行>
- 问题：<一句话描述>
- 详情：<为什么是问题>
- 建议：<怎么修>

（无问题时写：审查通过，未发现技术问题。）

## 审查范围
- 覆盖的审查 skills 列表
- 未覆盖项/已知风险
```

### JSON 格式（可选，结构化验证）
遵循 `.codebuddy/schemas/review-report.schema.json`，包含：
- `metadata`：功能名、日期、审查者、版本
- `statistics`：文件/行数统计
- `findings`：问题列表（severity/category/file/location/issue/detail/suggestion）
- `coverage`：已调用和跳过的 skills
- `summary`：passGate（是否通过门禁）+ 问题计数 + 结论

**验证机制**（学习 Archify）：
- 生成 JSON 后，按 schema 验证
- 验证失败 → 返回精确的修复建议（字段缺失/类型错误）
- 验证通过 → 原子性写入文件
- Markdown 作为人类可读的主要输出，JSON 用于自动化工具集成

## 重要

- **具体**：给文件+函数/行号，不要泛泛而谈
- **聚焦真 bug**：不做风格吹毛求疵
- **给方案**：指出问题同时给修复建议
- **安全类问题标记 CRITICAL**
- 审查发现问题后，先修复再提交（回到 `piv-implement` / `piv-validate`）
