---
name: piv-create-pr
description: 基于已提交的改动创建 Pull Request / Merge Request，包含清晰的描述与关联信息。触发条件：git-commit-style 提交完成后、用户说"创建PR"、"发起MR"、"提交评审"、"push 并创建 PR"时自动触发。
auto_invoke: true
argument-hint: [optional-pr-title]
---

# 创建 Pull Request / Merge Request

**PR 标题（可选）**：$ARGUMENTS

将已提交的改动创建为 PR/MR，供他人评审。

## 流程

### 1. 前置检查
- [ ] 改动已提交（`git-commit-style` 流程已完成）
- [ ] 当前分支命名符合项目规范（如 `dev_*` / `feature_*` / `fix_*`）
- [ ] 已确认目标分支（如 `dev_v1.2.0.2` 或主开发分支）
- [ ] 本地分支已推送到远端

### 2. 收集信息
- 本次改动的 commit 列表（`git log <base>..HEAD --oneline`）
- 改动文件清单与统计（`git diff <base>...HEAD --stat`）
- 相关 PRD/SDD/计划文档路径

### 3. 撰写 PR 描述

```markdown
## 概述
<本 PR 解决什么问题，一句话>

## 改动内容
- <功能/修复点 1>
- <功能/修复点 2>

## 关键文件
- <路径>：<改动说明>

## 验证情况
- [ ] 编译通过（`build-all.bat`，无新增警告；改了画布再叠 `cd designer && npx tsc --noEmit && npx vite build`）
- [ ] 行为验证（<验证方式>：`--selftest` / `--trace` / 真机驱动，见 `piv-validate` 第 3 级）
- [ ] 单测（`tools\run-tests.bat`；改了 `.cgui` 字段或控件类型叠 `tools\check-format.bat`）
- [ ] AI 审查通过（`piv-review-changes` 产出的审查报告路径）

## 关联文档
- PRD: docs/PRD_xxx.md
- SDD: docs/SDD_xxx.md
- 实现计划: .codebuddy/plans/xxx.md

## 风险与注意事项
- <已知风险/需人工确认项>
```

### 4. 创建 PR
- 按项目使用的平台创建（GitLab MR / GitHub PR / 其他）
- 标题：简洁，能概括改动
- 填写描述，勾选验证项
- 添加 Reviewer（如项目有约定）

## 输出

- PR/MR 链接
- 简要总结：改了什么、验证了什么、需要评审关注什么
