---
name: piv-validate
description: 验证改动是否真正工作：编译、静态检查、行为测试。触发条件：piv-implement 完成后、用户说"验证一下"、"测试"、"编译看看"，或进入 piv-review-changes 前强制触发。
auto_invoke: true
argument-hint: [optional-validation-focus]
---

# 验证改动

**验证聚焦（可选）**：$ARGUMENTS

验证改动是否真正可用，而不是"看起来能编译"。

> **本工程的验证入口是 `.bat`，不是 `idf.py`**。四条命令覆盖全部：
> `build-all.bat`（编译 + 逐示例自检）、`tools\run-tests.bat`（C 层单测）、
> `tools\check-format.bat`（跨端契约）、`cd designer && npx tsc --noEmit && npx vite build`（画布）。
> 写法以 `sop` §3「门禁命令表」为准 —— 表里没有的形态一律以表纠正。
> 各有各的适用范围，见下面三级。

## 验证层级（从低到高，逐级通过）

### 第 1 级：编译与静态检查（必做）

```bat
:: 1. 工作区卫生
git diff --check            :: 空白错误、冲突标记
git status --short          :: 确认改动范围（本仓库会把示例 exe 一起改，属预期）

:: 2. 编译（本工程的唯一入口）
build-all.bat               :: 运行时 + examples\ 下全部示例，每个都跑一遍 --selftest
:: 只编一个工程： build-app.bat <名字>

:: 3. 改了画布（designer\）时 —— read_lints 报不出 TS 语法错误，必须真跑
cd designer && npx tsc --noEmit && npx vite build
```

**注意**：`build-app.bat` 是增量编译（按时间戳），且 `--release` 才去控制台；
改了 `runtime\` 之后它会自动刷运行时库，除非设了 `CSTACKGUI_SKIP_RUNTIME=1`。

### 第 2 级：静态分析与单测（尽量做）

```bat
tools\run-tests.bat              :: C 层单测（JSON / .cgui 解析 / 类型表 / 事件表 / 动作表）
                                 :: + cg_kit 基础能力冒烟（文件/配置/编码/注册表）
tools\run-tests.bat --memcheck   :: 再叠一层堆泄漏记账（ld --wrap malloc/free）
tools\check-format.bat           :: 格式契约：画布写的 .cgui 必须被 C 侧读懂（往返探针）
                                 :: + 支持库表 vs cstackgui.h + 文档口径 + 分层契约 + 运行时契约
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bat-utf8.ps1 -Mode fix
                                 :: 动过任何 .bat 之后跑（-Mode check 已并入 check-format）
```

`tools\check-format.bat` 是**跨端**的那道：画布与运行时之间"同一事实有两份"的地方
（类型表、事件表、字段语义、动作表）全靠它兜住。改了 `.cgui` 字段或控件类型必须跑。

### 第 3 级：行为验证（必做，核心）

按实现计划中的验证方法逐条执行。本工程没有示波器，但有更贴切的三种实测：

- **`--selftest`**（不开窗的端到端）：把每个绑了回调的控件合成一次事件并**真调**你的 C 函数，
  返回码非 0 = 有事件名在 C 侧没登记、或绑了该控件不支持的事件、或动作配置有错。
  `.cgui` 改了事件名 / 加了控件 / 加了动作之后必跑：
  `examples\gallery\gallery.exe --selftest examples\gallery\gallery.cgui`
- **`--trace`**：开窗前打印一遍文档树（控件树 + 动作表 + 每行菜单），核对是不是你以为的那样。
- **真机驱动**（GUI 程序唯一能证明"按键/点击真的发生了"的办法）：
  起真窗口 → 激活它 → 发键/鼠标事件 → **读回程序自己写出的文件**。
  已验证可用的形态（脚本**现场写**进 `build\`，gitignore 内；仓库不提供现成探针）：
  起探针 → `SetForegroundWindow` + 等它真的成为前台 →
  `SendKeys("^s")` / `{F10}{DOWN}{ENTER}` → 读 `hit.txt` 断言行数与内容；
  **先确认窗口真的在前台**，否则"加速键没生效"和"没测到"会被混为一谈。
  看外观用截图：`tools\shot.ps1`（PrintWindow 优先，不怕被别的窗口遮住）。

- **边界与异常**（本工程必查）：
  - 空 / 超长输入；UTF-8 与 GBK 的边界（方向写反是乱码但不报错）
  - 控件 id 拼错、事件名没登记、动作名引用不存在 → `--selftest` 的返回码
  - 高 DPI（125% / 150%）下的坐标与像素（`app->scale`）
  - 关窗路径：回调里不要立刻销毁控件（用 `cg_set_timer` 延后）
  - 串口 / 网口：超时、半包、断开（见 `docs\COMM.md`）；线程只搬字节，控件不是线程安全的
  - 在 `--selftest` 下：弹对话框之前先问 `cg_app_has_window()`

## 验证完整性（"跑过了" ≠ "验证过了"）

四条都是**本工程实测**踩出来的，每条都记下"当时是什么样"，别重复。

1. **验收命令必须"能失败"**：跑之前先问"**它有问题时会红吗**"。
   本工程实测：`tools\check-format.bat > build\x.log 2>&1 & echo EXITCODE=%errorlevel%`
   里那个 `%errorlevel%` 在 **cmd 解析整行时就展开**了 —— 打印出来的是**上一条命令**的 0，
   而门禁实际是红的（文档口径 9 处报错）。**判定必须读日志，不能信回显。**
   同一族陷阱：`| findstr` 之后的退出码是 `findstr` 的、不是原命令的（成功会被"过滤"掉）；
   GBK 管道里的中文 `grep` 恒为空 ⇒ 无论有没有问题都"通过"。
   自己写的门禁 / 探针要**正例反例各跑一次**（反例必须真的红），否则不承认它是门禁。
2. **用于决策的度量，先人工抽验 1~2 条真实样本**：别只看"共检查 N 条"里的 N。
   抽一条看它到底查了什么、查到的是不是那个东西。
3. **"应无行为变化"的改动要给等价性证明**：改注释 / 缩进 / 格式时，用词法剥离（正确处理
   字符串、字符字面量、行尾续行）后比较骨架是否逐字节相同；**不要用朴素正则**剥离注释
   （注释正文里出现 `*/` 时会提前截断，既假阳性也掩盖真缺陷）。
4. **批量编辑后立刻校验副作用**：收尾核对字节数 / 行数是否合理、有没有 0 字节或 0 新增行的
   文件，并 `git status --short` 逐条过一遍 —— 手写"我改了哪几个文件"的清单**必漏**。

## 验证通过标准

以下全部满足才算验证通过：

- [ ] `git diff --check` 无输出；`git status --short` 的每一项都是本次预期改动
- [ ] `build-all.bat` 通过（每个示例 `--selftest` 回 0）
- [ ] 改了 `runtime\` → `tools\run-tests.bat` 通过（必要时叠 `--memcheck`）
- [ ] 改了 `.cgui` 字段 / 控件类型 / 动作表 → `tools\check-format.bat` 全绿
- [ ] 改了画布 → `npx tsc --noEmit` **与** `npx vite build` 都通过
- [ ] 所有计划中的验证方法已执行且通过
- [ ] 边界/异常路径已覆盖（或明确记录为已知风险）
- [ ] **每条验收命令都能"失败"**（读过日志、不是靠回显；自定义门禁正反例都跑过）
- [ ] 用于决策的度量已人工抽验过样本
- [ ] 声明"仅注释/格式改动"时，已附骨架等价证明

## 验证失败时的处理

1. 定位：读取失败相关代码，理解原因（不要盲目修改）
2. 修复：小步修改，重新编译
3. 重验：重复直到通过
4. 如果验证方法本身有问题：更新计划中的验证方法，说明原因

## 输出

报告验证结果清单（每项：通过/失败/跳过+原因），失败的项必须附修复说明。

**贴实际输出**，不要写"已验证通过"这种没有证据的结论；判不出来的就说判不出来。

全部通过后才允许进入 `piv-review-changes`。
