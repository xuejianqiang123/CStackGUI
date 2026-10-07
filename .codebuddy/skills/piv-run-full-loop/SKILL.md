---
name: piv-run-full-loop
description: 端到端自动开发一个完整功能：加载上下文 → 制定计划 → 实现 → 验证 → 审查 → 提交。触发条件：用户提出明确的、小范围的功能需求（如"实现XX功能"、"添加XX驱动"），且未明确要求分步执行时，自动触发全流程。
auto_invoke: true
argument-hint: [feature-description]
---

# 端到端功能开发（PIV 全循环）

**功能描述**：$ARGUMENTS

本 skill 串联 PIV 内循环的全部核心环节，实现"从描述到提交"的自动化开发。

> 前置建议：若该功能属于较大需求，先走外层循环（`plan-create-prd` → `plan-architecture` → `piv-slice-epic`）再进入本循环；若功能小而明确，可直接进入本循环。

---

## 步骤 1：加载上下文

执行 `prime-codebase` skill 的工作流，建立代码库认知，并明确：

- 本功能落在哪一块、哪一层（`runtime/src` 的 core / win32 / skin / kit、`designer/src`，还是 `examples/` / `apps/`）
- 相关组件与可复用代码
- 构建与验证命令

---

## 步骤 2：制定实现计划

执行 `piv-plan-implementation` skill 的工作流（Validation-First）：

- 产出计划文件 `.codebuddy/plans/<feature-name>.md`
- **记下功能名**（下一步要用）

若已有 PRD/SDD/切片清单，直接继承其决策，不重新讨论。

---

## 步骤 3：实现

执行 `piv-implement` skill 的工作流：

- 严格按计划逐步实现
- 每步完成立即验证，不攒到最后

---

## 步骤 4：验证

执行 `piv-validate` skill 的工作流：

- 编译 + 静态检查（`git diff --check`、`build-all.bat` 无新增警告；
  改了画布再加 `cd designer && npx tsc --noEmit && npx vite build`）
- 行为验证（按计划的验证方法）
- 边界/异常路径覆盖

验证失败则回到步骤 3 修复，直到全部通过。

---

## 步骤 5：提交前审查

执行 `piv-review-changes` skill 的工作流：

- 对全部改动做通用技术审查
- 按改动领域加载对应的审查 skills（**7 个**，映射表见 `global-rule.md` §7 / `sop` §4；
  本工程没有 MCU，没有 DMA/中断那一族）
- 产出审查报告到 `.codebuddy/code-reviews/`

发现需要修复的问题 → 回到步骤 3/4，修复并重验。

---

## 步骤 6：提交

执行 `git-commit-style` skill 的流程（本项目唯一提交规范）：

- `[类型][模块] 描述` 格式，按逻辑分批原子化提交
- 输出变更摘要 +（如有）.codebuddy/ 层改动说明

---

## 最终总结

全部步骤完成后输出：

### 功能开发完成

- **原始需求**：$ARGUMENTS
- **功能名**：<计划中的名称>
- **执行步骤**：
  1. ✅ 加载上下文
  2. ✅ 计划已创建（.codebuddy/plans/<name>.md）
  3. ✅ 实现完成并验证
  4. ✅ 审查通过（审查报告路径）
  5. ✅ 已提交（commit hash）

**产出物**：
- 计划：`.codebuddy/plans/<name>.md`
- 审查报告：`.codebuddy/code-reviews/<date>-<name>.md`
- 改动文件：<列表>
- 提交：<hash>

**后续步骤**：
- `piv-create-pr` 创建 PR/MR（如需要）
- 推送远端：`git push`
- 继续下一个功能/切片
