---
name: prime-codebase
description: 加载代码库上下文（Context is King）。触发条件：会话刚开始且用户提出开发任务、用户说"先了解项目"、"看看代码结构"、"熟悉代码库"，或即将进入 piv-implement 前自动触发以建立上下文。
auto_invoke: true
argument-hint: [optional-focus-area]
---

# 加载代码库上下文

**聚焦方向（可选）**：$ARGUMENTS

在动手改代码之前，先建立对代码库的完整认知。**Context is King** —— 了解项目模式是做好一切工作的前提。

## 必读文件（按优先级）

### 1. 项目规范与文档
- `.codebuddy/global-rule.md` —— 项目全局规则（必须遵守）
- `.codebuddy/README.md` —— skills 工作流说明
- `README.md` —— 项目根说明
- `docs/` —— PRD、SDD 文档（了解既有设计）

### 2. 项目结构
- 确认当前工作子项目：`proj_c2l_mainboard_p4` / `esp32_c2_slave`
- 查看对应子项目目录结构与 `main/` 入口
- 查看 `CMakeLists.txt`（根 + 子项目）理解组件依赖
- 查看 `components/` 中与本功能相关的组件（驱动、协议、工具库）

### 3. 构建系统
- 确认是 ESP-IDF 构建（`idf.py`）还是独立 CMake
- 查看 `sdkconfig` / `sdkconfig.defaults` 了解使能的外设和配置项

### 4. 测试与审查
- `tests/` 目录：现有单测怎么写的？用什么框架？（了解后新代码照此模式补测试）
- `ai_code_review.py`：了解 CI 审查如何工作（AI-REVIEW-SKIP 标记等）

## 需要建立的认知

完成后你应该能回答：

1. 这个项目的代码风格是什么？（命名、头文件组织、错误处理惯例）
2. 本功能涉及哪些模块？它们在哪？
3. 已有组件里有没有可复用的东西？（避免重复造轮子）
4. 构建/测试命令是什么？
5. 项目的 git 分支与提交规范？（当前分支、conventional commit 风格）

## 输出

在回复中给出**简明的上下文总结**（不要长篇大论），包括：

- 项目结构要点（一段话）
- 相关模块/组件位置列表
- 构建与验证命令
- 可复用组件提示
- 代码规范要点

如果聚焦方向明确（如"UART 驱动"），则输出与聚焦点强相关的上下文，忽略无关部分。
