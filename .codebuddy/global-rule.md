# 嵌入式固件全局强制规则
所有代码生成、代码修改、代码评审必须遵守以下规则，默认严格生效：

> **适用范围（CStackGUI）**：本仓库是 **Win32 桌面 GUI 项目**（Electron 画布 + C 运行时 + MinGW gcc），
> 不是固件工程。下面第 1~3 节的硬件 / 中断 / RTOS 规则**只在第 6 节列出的审查技能被触发时才适用**，
> 对 `runtime/`、`designer/` 和业务 C 代码不构成约束；本工程真正的硬门禁是
> `build-all.bat` → `examples\gallery\gallery.exe --selftest` → `tools\run-tests.bat`
> （C 层单测：JSON / 文档 / 类型表 / 事件表 + cg_kit 冒烟） → `tools\check-format.bat`
> （改了 `.cgui` 契约或控件类型时；它顺带检查 bat 编码），以及 README 第三章列的项目铁律。
>
> **动过任何 `.bat` 之后必须跑**：`powershell -NoProfile -ExecutionPolicy Bypass -File tools\bat-utf8.ps1 -Mode fix`
> —— 仓库 bat 统一 **UTF-8 无 BOM + CRLF + 开头一段纯 ASCII 前导**（先切 65001 再 `cmd /c "%~f0" %*` 重入）。
> cmd 用"启动这个 cmd 进程时的控制台代码页"解码 bat，UTF-8 的中文会被按 2 字节/字解码、行定位错位，
> 注释后半句被当命令执行（双击满屏 `"... is not recognized"`）；带 BOM 会让首行直接报错；带中文的 bat 里
> 不能出现 `goto` / `call :label`（标签扫描会漂移）。`-Mode check` 已并入 `tools\check-format.bat`。

## 1. 硬件相关硬性规则
- 禁止自动优化硬件初始化代码
- 禁止随意修改寄存器、时钟、GPIO、DMA、NVIC配置
- 所有硬件改动必须给出风险提示、原因、替代方案

## 2. 安全规范
- 所有数组、缓冲区必须做边界判断
- 禁止裸指针直接运算
- 中断函数必须极简，禁止延时、禁止循环阻塞、禁止大量日志

## 3. RTOS规范
- FreeRTOS 禁止在中断中调用非FromISR接口
- 栈分配禁止大数组，大缓冲区必须全局/静态

## 4. 评审优先级
安全风险 > 稳定性风险 > 性能 > 代码风格

## 5. 输出要求
所有问题必须标注：文件+行号+风险等级+问题原理+修复示例

## 5.1 输出压缩强制规则（Caveman 常驻，默认开启）
**每条回复默认 full 档压缩**，无需用户重复触发。规则内核：
- 删：冠词、填充词（其实/基本上/一般来说/我们可以看到）、客套、对冲措辞、多余连接词。允许句子片段、短同义词。
- 保留一字不改：代码、命令、错误信息/日志、文件路径、寄存器/引脚名、版本号、日期、URL。
- 准确性 > 压缩率：安全/中断/DMA/时序等关键结论必须清楚，宁可多留词也不造成歧义。
- 例外：审查报告/PRD/SDD 正文不压缩（保证完整表达），外围对话说明仍压缩。
- 关闭：用户说"关闭caveman"/"正常模式"/`off`；切档：`caveman ultra`/`caveman wenyan` 等。
- 每轮持续生效，不因对话变长而回退。详见 `skills/caveman/SKILL.md`。

## 5.2 代码生成量压缩强制规则（Ponytail 常驻，默认 full 档）
**每次写码前默认走"决策梯子"**，能不写就不写、能复用就不重写。核心：最好的代码是从未写过的代码。
- **梯子**（按序检查，停在第一满足层）：需要存在?→已有?→标准库?→平台特性?→现有依赖?→一行搞定?→最小代码。
- **红线不砍**：中断安全/DMA一致性/看门狗/时序/掉电保护/错误码/栈边界属"必要代码"，梯子跳过这些。
- **嵌入式禁 ultra**：固件过度压缩易漏防御，锁 full 上限，用户坚持 ultra 才放开且逐条复核红线。
- **先读后省**：懒于方案，绝不懒于阅读——必须先读相关代码、追踪流程，再上梯子。
- 关闭：用户说"关闭ponytail"/`ponytail off`；切档：`ponytail lite`（更温和）。
- 与 PIV 流程正交：只影响"写多少代码"决策，不改质量门禁。详见 `skills/ponytail/SKILL.md`。

## 5.3 非破坏性文件操作强制规则（不删为先，默认开启）
**能覆盖就不删、能不建就不建**：删除类操作会触发风控与人工批准，零散删除等于把用户按在十几个弹窗前。
- **替代优先**：改内容用覆盖写（`replace_in_file`/`write_to_file`），不"先删再建"；清空文件用写空内容；
  重新编译**不要删** `build\obj`/`build\lib`（脚本自比时间戳，重编会覆盖 `.o`、`ar rcs` 覆盖归档）；
  重建输出目录直接往里写；被 git 跟踪的文件要"还原"用 `git restore` 而不是删除；
  临时探针/截图/提交信息文本一律落 `build\`（gitignore），**收尾不清理**。
- **硬红线**：禁 `rmdir /s /q`、`del /s /q`、`Remove-Item -Recurse`、`git clean -fd(x)`、通配符批量删除；
  禁删 `.codebuddy\`、`runtime\`、`designer\src\`、`docs\`、`examples\`/`apps\` 里的手写文件；
  禁删工作区之外的路径与用户数据目录。
- **确实要删**：合并成**一次**操作（完整路径、无通配符），先给「删除清单 + 原因 + 为何无法替代」，
  整个任务只请求一次批准；同一轮里连续第三次动删除命令就要停下来合并。
- 关闭：用户说"可以随便删"/`safe-file-ops off`。详见 `skills/safe-file-ops/SKILL.md`。

---

## 6. PIV 开发工作流强制规则（v0.1）

### 工作流自动触发机制
项目采用 **PIV 双循环框架**（参考 coleam00/skills），AI 必须根据用户意图和当前阶段自动进入相应工作流：

#### 外层循环（大需求）
| 触发条件 | 自动调用 Skill | 产出 |
|---------|---------------|------|
| 新功能需求但验收标准不明确、跨模块/硬件/协议 | `plan-create-prd` | `docs/PRD_*.md` |
| 需要架构设计、模块划分、新模块/重构 | `plan-architecture` | `docs/SDD_*.md` |
| 需求太大、无法一次完成、需要拆分任务 | `piv-slice-epic` | `docs/SLICES_*.md` |

#### 内层循环（每次开发必走，强制执行）
| 阶段 | 强制调用 Skill | 触发时机 | 产出 |
|------|---------------|---------|------|
| 0. 上下文 | `prime-codebase` | 会话开始且有开发任务 / 即将实现前 | 上下文总结 |
| 1. 计划 | `piv-plan-implementation` | 开发任务明确但无计划 / 实现前强制触发 | `.codebuddy/plans/*.md` |
| 2. 实现 | `piv-implement` | 计划确认后 | 代码改动 |
| 3. 验证 | `piv-validate` | 实现完成后**强制触发**（进入审查前门禁） | 验证结果清单 |
| 4. 审查 | `piv-review-changes` | 验证通过后**强制触发**（提交前门禁） | `.codebuddy/code-reviews/*.md` |
| 5. 提交 | `git-commit-style` | 审查通过后 | git commit |
| 6. PR | `piv-create-pr` | 提交完成、用户要求创建 PR 时 | PR/MR |

#### 快捷全流程
| 触发条件 | 自动调用 Skill | 说明 |
|---------|---------------|------|
| 用户提出明确的、小范围功能需求（如"实现XX功能"） | `piv-run-full-loop` | 自动走完整个内层循环（0-5步） |

### 强制门禁点
以下环节为**质量门禁**，必须通过才能进入下一阶段：

1. **验证门禁**（`piv-validate`）：
   - 编译无新增错误/警告（`idf.py build`）
   - 行为验证通过（按计划的验证方法）
   - 边界/异常路径覆盖

2. **审查门禁**（`piv-review-changes`）：
   - 按改动领域自动调用 14 个嵌入式审查 skills（见下节）
   - 无 critical/high 级别问题，或已修复
   - 产出审查报告到 `.codebuddy/code-reviews/`

3. **计划门禁**（`piv-plan-implementation`）：
   - 任何实现前必须先有计划
   - 计划必须包含 Validation-First 的验证方法

---

## 7. 嵌入式审查 Skills 自动触发规则（v0.1）

当 AI 生成、修改代码或执行 `piv-review-changes` 时，必须按改动领域自动调用：

### 审查 Skills 映射表
| 改动领域 | 强制调用 Skill | 检查重点 |
|---------|---------------|---------|
| DMA/内存搬运/cache | `dma_cache_check` | cache 刷新、对齐、栈内存做DMA |
| 中断/ISR/NVIC | `interrupt_check` | 标志清除、FromISR接口、共享变量 |
| 缓冲区/字符串/memcpy | `buffer_overflow_check` | 边界检查、危险函数 |
| 动态内存/堆 | `memory_leak_check` | 泄漏、碎片、野指针 |
| Flash/NVS/OTA | `flash_storage_check` | 擦写寿命、分区表 |
| 通信协议（UART/SPI/I2C/MQTT） | `communication_protocol_check` | 超时、校验、重传 |
| GPIO/外设/时钟冲突 | `peripheral_conflict_check` | 引脚复用、时钟源冲突 |
| 低功耗/睡眠 | `power_management_check` | 睡眠模式、唤醒源 |
| RTOS 任务/信号量 | `rtos_task_check` | 优先级、死锁、栈大小 |
| 看门狗/定时器 | `watchdog_timer_check` | 喂狗、定时器冲突 |
| 栈用量/局部大数组 | `stack_overflow_check` | 自动评估栈用量 |
| 结构体/对齐/寄存器映射 | `struct_best_practice_check` | 对齐、位域、内存优化 |
| 错误码/断言/日志 | `error_handling_check` | 返回值判断、容错 |
| 代码规范 | `firmware_code_style` | 命名、注释、分层 |
| 界面观感/配色/字体/间距/动效/界面文案（画布或 `.cgui` App） | `design-taste` | 设计读盘+三旋钮、AI 味黑名单、质量地板（对比度/间距节奏/四态齐全）、文案一致性 |

### 触发规则
1. **写代码时主动审查**：生成代码后，立即按领域调用相关 skills
2. **piv-review-changes 环节**：全面调用所有相关 skills，产出审查报告
3. **问题修复后重验**：修复问题后重新运行对应 skill 验证

---

## 8. 强制执行模式
- `strictMode: true` 时，任何违反 skills 规则的代码必须修正后才能提交
- **验证门禁**和**审查门禁**必须通过才能进入 `piv-commit`
- 审查结果必须包含：风险等级、位置、原理、修复方案
- PIV 循环不允许跳步（除非用户明确要求）

---

## 9. 产出物管理规范
| 产出类型 | 存放位置 | 命名规范 | 说明 |
|---------|---------|---------|------|
| PRD | `docs/PRD_*.md` | `PRD_<feature>.md` | 需求文档 |
| SDD | `docs/SDD_*.md` | `SDD_<feature>.md` | 架构设计 |
| 切片清单 | `docs/SLICES_*.md` | `SLICES_<feature>.md` | 任务拆解 |
| 实现计划 | `.codebuddy/plans/` | `<feature-name>.md` | Validation-First 计划 |
| 审查报告 | `.codebuddy/code-reviews/` | `<date>-<feature>.md` | 提交前审查 |
| Skills | `.codebuddy/skills/` | `<skill-name>/SKILL.md` | 技能定义 |

---

## 10. 版本历史
- **v0.1** (2026-08-23)：初始版本，建立完整配置体系（14个审查技能 + PIV双循环工作流 + 门禁机制 + Schema验证）
- **v0.2** (2026-09-22)：新增 `design-taste`（设计品味门禁，八套外部设计技能提炼后落到画布与 `.cgui` 两端）；§7 映射表增加"界面观感/界面文案"一行
