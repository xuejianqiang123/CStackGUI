---
name: piv-commit
description: 为全部未提交改动创建原子化的、conventional-commit 风格的提交。触发条件：piv-review-changes 通过后、用户明确说"提交"、"commit"、"保存改动"时自动触发。
auto_invoke: true
---

# 创建提交

为全部未提交改动创建一个原子化的提交。

## 流程

### 1. 查看改动
```bash
git status
git diff HEAD
git status --porcelain
```
确认改动范围与内容。

### 2. 暂存
将改动文件加入暂存区（未跟踪文件也要加入）。

### 3. 撰写提交信息

采用 conventional-commit 风格：`<tag>: <summary>`

常用 tag：
- `feat` 新功能
- `fix` 修复 bug
- `refactor` 重构（不改变行为）
- `docs` 文档（含 .codebuddy/ 配置、SKILL.md）
- `test` 测试
- `chore` 构建/杂务

摘要要准确反映改动内容（面向开发者的 git log 读者）。

### 4. 提交前检查
- [ ] 是否包含无关改动？（如编译产物 build/、*.o 等应被 .gitignore 排除）
- [ ] 是否漏了相关文件？（如新增的头文件、测试、文档）
- [ ] 提交信息是否准确、简洁？

## 输出

提交成功后输出两个明确标注的摘要：

### 变更摘要
一段（3-6 句）说明这次提交解决的问题和关键改动文件，写给扫 git log 的开发者看。

### AI 层改动
仅当 `.codebuddy/` 下的文件有变动时输出（SKILL.md、global-rule.md、README.md 等）：
- 列出每个变更的 AI 层文件
- 一行说明改了什么、为什么

若无 `.codebuddy/` 改动，省略此节。
