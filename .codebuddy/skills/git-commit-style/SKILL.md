---
name: git-commit-style
description: 本项目唯一的提交规范与提交流程。格式固定为 [类型][模块] 描述（用户指定，不用 conventional-commit）。覆盖：提交信息的类型/模块标签、按逻辑分批的原子化提交、提交前检查、变更摘要输出。触发条件：piv-review-changes 通过后、用户说"提交"、"commit"、"保存改动"时自动触发。
auto_invoke: true
argument-hint: [可选：说明这次提交的范围，例如 "只提交画布改动"]
---

# 提交规范与流程（git-commit-style）

> 本技能是本项目**唯一**的提交规范，已吸收原 piv-commit 的流程（2026-09-20 合并）。
> PIV 循环里"提交"这一环就是执行本技能。

## 提交信息格式（用户指定，固定不变）

```
[类型][模块] 一句话说清这次提交做了什么
```

- **类型标签**：`新增` / `修复` / `重构` / `优化` / `文档` / `构建` / `发布`
- **模块标签**：`运行时` / `画布` / `示例` / `构建` / `发布` / `文档` / `技能`，
  也可以用具体工程名（如 `hello`、`gallery`）或子模块（如 `事件模型`）
- 描述用简体中文、一行说清；一行放不下就空一行写正文，正文解释"为什么"而不是"改了什么"

示例：

```
[新增][运行时] 事件按控件特性分配，绑错会警告并在 selftest 中报错
[修复][画布] 便携版找不到 C 项目根，改用 PORTABLE_EXECUTABLE_DIR 自动识别
[文档][发布] 使用者说明补齐从双击 exe 到出 exe 的流程
```

**禁用** conventional-commit 风格（`feat:` / `fix:` …）——两种风格混进 git log 会很难看。

## 流程

### 1. 查看改动

```bat
git --no-pager status --short
git --no-pager diff HEAD
```

确认改动范围与内容，然后**按逻辑分组分批提交**（运行时 / 画布 / 示例 / 构建 / 文档 / 技能
各归各类），不要一把梭塞一个 commit。每一批单独 `git add <文件>` + 单独 commit。

### 2. 暂存

- 把该批次的文件（含未跟踪的新文件）`git add` 进暂存区，其它批次别混进来。
- 生成物不入库：`build\`、`release\`、`node_modules\`、`designer\dist\`、
  各工程里的 exe / lib / build-app.bat（已在 .gitignore）。

### 3. 提交前检查

- [ ] 是否包含无关改动？（如编译产物 *.o、截图等应被 .gitignore 排除的）
- [ ] 是否漏了相关文件？（如新增的头文件、示例、文档）
- [ ] 改了 `.codebuddy\` 的 SKILL/规则？单独一个 `[文档][技能]` 批次
- [ ] 每条信息都是 `[类型][模块] 描述` 格式？提交后 `git log --oneline` 复查

### 4. 验证前置（piv-review-changes 通过后进入本技能时默认已过）

改动若涉及行为，提交前确保构建与自检是绿的：

```bat
build-all.bat
examples\gallery\gallery.exe --selftest examples\gallery\gallery.cgui
tools\run-tests.bat       :: C 层单测（JSON/文档/类型表/事件表）+ cg_kit 冒烟
tools\check-format.bat    :: 改了 .cgui 字段/控件类型时（顺带查 bat 编码）
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bat-utf8.ps1 -Mode fix
                          :: 动过任何 .bat 之后必跑：UTF-8 无 BOM + ASCII 前导，否则双击时注释会被当命令跑
```

## 输出

提交成功后输出两个明确标注的摘要：

### 变更摘要

一段（3-6 句）说明这些提交解决的问题和关键改动文件，写给扫 git log 的开发者看。
分批提交时逐条列出 commit 号与信息（`git log --oneline` 的格式即可）。

### AI 层改动

仅当 `.codebuddy/` 下的文件有变动时输出（SKILL.md、global-rule.md、README.md 等）：

- 列出每个变更的 AI 层文件
- 一行说明改了什么、为什么

若无 `.codebuddy/` 改动，省略此节。
