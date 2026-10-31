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
- `README.md` —— 项目根说明：**先读它**，里面有一张"文档地图"和一张"排查表（现象 → 先查什么）"
- `docs\` —— 四份设计文档，各讲一块别指望一份读全：
  `ARCHITECTURE.md`（内部为什么这么设计、模块怎么分层）/ `CGUI-FORMAT.md`（`.cgui` 每个字段）
  / `COMM.md`（串口与网口 TCP/UDP）/ `M3-SKIN.md`（自绘外观怎么改）；
  另有 `docs\articles\` 对外教学文章（贴着 `examples\` 逐行讲）

### 2. 项目结构
- **三块，别混**：`designer\`（画布，Electron + React + TS）、`runtime\`（C 运行时库，Win32 后端）、
  `examples\`（仓库自带示例）与 `apps\`（用户自己的工程）
- **两条"真相所在"要记住**（判断一切"代码与文档对不上"时按代码）：
  `.cgui` 的**字段语义**在 `runtime\src\core\cg_doc.c`；
  **控件类型与每种类型支持的事件**在 `cg_doc_kind.c` 的 `k_kinds`。
  画布端 `designer\src\model.ts` 是跟着它们走的，不是反过来。
- `runtime\src\` 四层（目录即分层，各自在 include 路径上，乱 include 会编不过）：
  `core\`（后端无关：JSON / 文档 / 控件模型 / 事件）/ `win32\`（原生后端，14 个子模块，共用 `cg_win_internal.h`）
  / `skin\`（GDI+ 自绘外观层，换内核时整目录替换）/ `kit\`（平台服务：文件/配置/编码/剪贴板/串口/网口）
- 每个 `.c` **头部注释都写了它管什么**；`cg_widget_data.c` / `cg_win_draw.c` / `cg_kit.c` 是纯路标文件
  （按控件族/窗口类/子系统拆开了，别往里加实现）

### 3. 构建与验证入口
- **没有 CMake、没有 `idf.py`**：编译入口全是 `.bat` —— `build-all.bat`（运行时 + 全部示例，逐个自检）、
  `build-app.bat <名字>`（单个工程，会先刷运行时库）、`build-runtime.bat`
- 画布侧：`cd designer && npx tsc --noEmit && npx vite build`
  —— **`read_lints` 报不出 TS 语法错误**，改了画布必须真跑这两条
- 每级验证手段、"能失败"的口径、GUI 真机驱动怎么做：见 `piv-validate`

### 4. 测试与审查
- `tests\` 用极简自注册框架（`cg_test.h` 的 `CG_TEST_SUITE`），**新增测试文件不用改任何清单**；
  入口 `tools\run-tests.bat`（可叠 `--memcheck` 堆泄漏记账 / `--grep` 挑套件）
- `tools\check-format.bat` 是**跨端**那道门禁：画布写的 `.cgui` 必须被 C 侧读懂，
  顺带查支持库表 vs `cstackgui.h`、文档口径、分层契约、运行时契约
- 本工程没有独立的代码审查脚本；"审查"就是 `piv-review-changes` 按 `global-rule.md` §7
  调用审查技能

## 需要建立的认知

完成后你应该能回答：

1. 这个项目的代码风格是什么？（命名前缀按层分：core `cg_*` / 后端 `cg_win_*` / 外观 `cg_skin_*` /
   平台服务 `cg_kit_*`；函数注释用 Doxygen `@brief/@param/@return`）
2. 本功能涉及哪些模块？它们在哪一层？
3. 已有能力里有没有可复用的？（`cg_kit` 提供文件/配置/编码/剪贴板/串口/网口 —— 业务代码
   **只 include `cstackgui.h`**，不要为了读个配置去 include `windows.h`）
4. 构建与验证命令是什么？（见 §3，四条 `.bat` + 画布两条）
5. 提交规范？（本工程**不用** conventional-commit：`[类型][模块] 描述`，见 `git-commit-style`）

## 输出

在回复中给出**简明的上下文总结**（不要长篇大论），包括：

- 项目结构要点（一段话）
- 相关模块/组件位置列表（含"该去哪层放"的判断）
- 构建与验证命令
- 可复用能力提示（`cstackgui.h` / `cg_kit` 里已有的）
- 代码规范要点

如果聚焦方向明确（如"表格勾选"），则输出与聚焦点强相关的上下文，忽略无关部分。
