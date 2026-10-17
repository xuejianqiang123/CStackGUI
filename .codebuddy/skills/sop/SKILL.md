---
name: sop
description: CStackGUI 开发 SOP 总控（单一入口）：按请求类型路由到对应技能，串起「宪法→上下文→规格/计划→实现→验证→审查→提交→收敛」八段流水线，内置对抗性闸门，提供唯一权威的门禁命令表、审查路由表、修 bug 与想法评估两条旁路、收尾沉淀与中断恢复规程。触发条件：任何开发类请求（新功能 / 改 bug / 改画布 / 改技能 / 并行交付 / 提交前）自动生效；用户说"走流程""SOP""全流程""从需求到提交""按规矩来"时显式调用。
auto_invoke: true
argument-hint: [request]
---

# SOP 总控（CStackGUI 单一入口）

**请求**：$ARGUMENTS

> 本技能**不复制任何技能正文**，只做五件事：**路由**（该加载谁）、**串流水线**（阶段与 DoD）、
> **兜门禁**（唯一权威命令表）、**装对抗性闸门**（攻击自己的结论）、**收尾与恢复**。
> 具体规则一律到对应 `SKILL.md` 里读；技能全清单见 `.codebuddy/skills/INDEX.md`，
> 强制规则见 `.codebuddy/global-rule.md`。
>
> 本工程是 **Win32/GDI+ 桌面工具包 + Electron 画布（`.cgui`）**，没有 MCU、没有固件、没有 `idf.py`。

---

## §0 常驻约束（每轮默认过，不用用户提）

| 约束 | 档位 | 一句话 | 红线（不砍 / 不改） |
|---|---|---|---|
| `caveman` | full | 回复压缩 | 代码、命令、路径、错误信息、字段名一字不改 |
| `ponytail` | full | 写码前走决策梯子 | 缓冲区边界 / 资源释放 / 错误码 / 时序 / 切帧一致性 |
| `safe-file-ops` | 开 | 能覆盖就不删 | 禁 `rmdir /s /q`、`del /s /q`、`Remove-Item -Recurse`、`git clean`、通配符批量删 |
| `first-principles-thinking` | 开 | 先回到问题本质 | 不套模板、不把上一版的结论当前提 |
| **`adversarial-thinking`** | **开** | **输出结论前先攻击自己的结论** | **"已否掉的项"不许省；没验证的断言不许写成确定项** |
| `evidence-discipline` | 开 | 下"已实现 / 未实现 / 谁对"这类结论前先取证 | 文档不是证据是待证假设；三段取证（存在→登记→接线）；判据先问"它出错时会红吗" |
| `request-spec` | 开 | 开工前把请求补成七元规格 | 可推断的声明假设并继续；只问"猜错会返工"的阻塞项且 ≤3 个 |

关闭口令：`关闭caveman` / `ponytail off` / `safe-file-ops off`。**对抗性闸门不能关** —— 
它是唯一一个"防我自己说错"的环节。

---

## §1 入口路由（先分流，再动手）

三个入口**互相独立**，不是三段必走流程（学自 Spec Kit）：

| 请求长什么样 | 判定 | 走什么 | 明确跳过 |
|---|---|---|---|
| 提问 / "这块怎么工作的" / "有没有这个能力" | 只读 | 直接查代码回答；陌生子模块加 `analyzing-projects` | 全流程、计划、提交 |
| "这个想法值不值得做" / "要不要上 XX" | 评估 | **§2B 想法评估**（intake → research → decide） | 直接开工 |
| "XX 崩了 / 不对 / 报错" | 修 bug | **§2A 修 bug 三段式**（诊断 / 修复 / 验证分离） | 走完整 SDD 流程 |
| 改 1~2 个文件的小修（typo、注释、单处判空） | 微改 | 直接改 → §3 最小门禁 → 提交 | 计划文件（不必落 `plans/`） |
| 明确的小功能（"给 XX 加 YY"） | 标准 | §2 主流水线（C→P0→P7） | 外层循环（PRD/SDD/切片） |
| 验收标准含糊 / 跨模块 / 涉及协议 | 大需求 | 外层：`plan-create-prd` → `plan-architecture` → `piv-slice-epic`，切片再进 §2 | — |
| 改 `designer/`（画布） | 专属 | `designer-modify` + `design-taste`，门禁叠画布两条 | — |
| 改 `.codebuddy/skills/` | 元 | `skills-create` + §3 的 `node tools\check-skills.mjs` | 编译门禁（不影响产物） |
| 请求是一句话且缺路径 / 文件 / 判据 | 规格化 | `request-spec` 补七元规格，再转交领域技能 | 直接开工猜需求 |
| 需求落在多个不重叠目录 | 并行 | `team-parallel-delivery` | 我一人全揽 |
| "审查一下" / 提交前 | 单点 | `piv-review-changes`（按 §4 路由审查技能） | — |
| 时序图 / 波形 | 工具 | `wavedrom-gen` | — |

> 判断不了的，**按"标准"走** —— 走多了只是多一份计划，走少了会漏门禁。

---

## §2 主流水线（标准路径）

每段写清：**进入条件 → 加载谁 → 必做 → DoD（完成定义）**。DoD 不过不进下一段。
**★ 对抗闸门** = 这一段结束前必须对自己做一次 `adversarial-thinking`，具体动作见 §2.9。

### C 宪法（项目级，只在接手 / 存疑时读一次）
根 `README.md` §3.4 的四条铁律 + `global-rule.md` §1~§5。**已有决策直接继承，不重新讨论。**

### P0 上下文 —— `prime-codebase`
- **必做**：定位改动落在哪层（`runtime/src` 的 core/win32/skin/kit、`designer/src`、`examples/`、`docs/`）；找出可复用件；确认门禁命令。
- **DoD**：能说出"改哪几个文件、会牵动哪份跨端契约"。

### P1 规格与计划 —— `piv-plan-implementation`（**计划门禁**）
- **必做**：先写 **spec**（要什么、为什么、验收标准、非目标、**能改什么不能改什么**），再写 plan 与 tasks；
  产出 `.codebuddy/plans/<feature>.md`。
- **计划三硬要求**（学自 vibe-coding-cn）：**每步小而具体** · **每步自带验证方法** · **计划里不写代码**。
- **DoD**：计划含 ①改动清单 ②每条的验证方法（且每条都能失败）③边界/异常清单 ④回滚方式 ⑤禁止改动面。

### P2 实现 —— `piv-implement`
- **必做**：严格按计划；**每步完成立刻编译并由用户/我验证，不攒到最后**（学自 vibe-coding-cn："我验证通过前不要开始下一步"）；计划外改动先问。
- **DoD**：代码写完，`build-all.bat` 过。

### P3 验证 —— `piv-validate`（**验证门禁**，GUI/收发/交互类叠 `serial_verify`）
- **必做**：§3 命令按表全跑；贴实际输出；**每条验收命令都要能"失败"**（读过日志，不信 `%errorlevel%` 回显）。
- **DoD**：§3 表中"适用"列全绿 + 边界/异常已覆盖 + **门禁不是靠回显判的**。

### P4 审查 —— `piv-review-changes`（**审查门禁**）
- **必做**：`git diff HEAD` 确认范围 → 按 §4 路由加载审查技能 → **`adversarial-review` 三角色审查** → 报告落 `.codebuddy/code-reviews/<日期>-<功能名>.md`。
- **DoD**：无 critical/high 未修复项；**每条 issue 都附自证证据**；报告已落盘。

### P5 提交 —— `git-commit-style`
- **必做**：`[类型][模块] 描述`（模块：运行时/画布/示例/构建/发布/文档/技能）；按逻辑分批原子提交；
  **文档（计划/报告/README）跟着代码走、不单独提交**（用户 2026-10-14 指定，细则见 `git-commit-style`）。
- **DoD**：`git --no-pager log --oneline` 复核通过；动过 `.bat` 已跑 `bat-utf8.ps1 -Mode fix`。

### P6 PR —— `piv-create-pr`
- 用户要求时才走。

### P7 收敛（converge）—— **回到规格核对**
学自 Spec Kit：实现完 ≠ 收敛。做完 P2~P5 后回到 P1 的 spec 逐条核对：

- [ ] spec 里每条验收标准**逐条打勾**，附证据（命令输出 / 截图 / 单测名）
- [ ] 实现过程中偏离 spec 的地方**要么改回 spec、要么改 spec 并说明为什么**（不许默默不一致）
- [ ] 计划里"本轮不做 / 降级为观测"的项已写进遗留
- [ ] **文档收口**：本次改动的事实（新能力 / 状态变化 / 新组件）已按 `doc-truth-sync` 回写到**权威那处**，
      不留"同一事实两份"；文档与代码冲突时按代码改文档
- [ ] 输出 **Converged / 未收敛（差哪几条）**；未收敛就回到 P2 补，不靠"下次再说"

### §2.9 对抗性闸门（横切四道，必过）

| 时机 | 攻击什么 | 最小动作 |
|---|---|---|
| P1 出计划后 | 计划本身 | 每条验证方法：**它有问题时会红吗？** 不红的重写；"能改什么"边界是否写清 |
| P3 判门禁时 | "通过了"这个结论 | 读日志而不是看回显；自建探针正反例各跑一次；排除环境假象（exe 被锁 / 窗口没在前台 / GBK 管道） |
| P4 出报告时 | 报告里的每条 issue | 每条都要 grep 自证"真的会发生"；**拿不出证据的降级或删除**，不许凑数 |
| 任何"以下是 N 条…"输出前 | 自己的清单 | 逐条问"这条真的是问题吗"，**允许否掉自己**，并把否掉项写出来 |

> 反模式：**把自己刚列的建议当标准答案直接执行** —— 那是 `adversarial-thinking` 存在的唯一理由。

### §2A 修 bug 旁路（诊断 / 修复 / 验证三段分离）

**三段不许混在一次改动里**（学自 Spec Kit）：

1. **诊断（assess）**：先复现 → 定位 → **只输出"评估出的原因 + 证据"**，不动代码。
   最小复现三件套：**预期 vs 实际 + 最小复现步骤**（学自 vibe-coding-cn）。
2. **修复（fix）**：**只改评估出的那个原因**，不顺手扩大、不顺手重构。
3. **验证（test）**：**必须验证最初那个症状消失**，不是"我改的地方没报错了"。
   判定三选一：`verified` / `partial` / `failed`。
   **没有验证的修复不算修复**（missing verification is not a successful fix）。

### §2B 想法评估旁路（不做也可以，做了要有结论）

`intake`（一句话记下想法）→ `research`（**先 grep 仓库里有没有现成的**，别造轮子）→
`define`（写清要什么、成本、风险）→ `decide`：

- **go** → 转 §2 标准路径
- **needs-clarification** → 列出待澄清项，回头问用户
- **kill** → **写清为什么砍掉**（有理由的"不做"是有用产出，不是失败）

未知项通过**直接编辑已有的那份 markdown** 解决，不要重跑整个阶段。

---

## §3 门禁命令表（唯一权威）

命令写法以此表为准，别处出现别的形态以此纠正。

```bat
:: —— 0 卫生（每次提交前）——
git diff --check                       :: 空白错误 / 冲突标记
git --no-pager status --short          :: 逐条确认改动范围是本次预期

:: —— 1 编译 + 逐示例自检（必做）——
build-all.bat                          :: 运行时 + examples\ 全部，每个跑一遍 --selftest
build-app.bat <名字>                   :: 只编一个工程（增量，按时间戳）

:: —— 2 C 层单测（改了 runtime\ 必做）——
tools\run-tests.bat                    :: 单测 + check-comments gate + cg_kit 冒烟
tools\run-tests.bat --memcheck         :: 再叠堆泄漏记账（动 malloc/free 时）
tools\run-tests.bat --no-kit           :: 跳过 kit 冒烟（只改纯 core 时提速）

:: —— 3 跨端契约（改了 .cgui 字段 / 控件类型 / 动作表 / 支持库表 / 文档口径 必做）——
tools\check-format.bat                 :: 内含：.cgui 往返探针、支持库对表、check-examples、
                                       :: check-layers、check-dispatch、bat-utf8 check、check-skills

:: —— 4 画布（改了 designer\ 必做，两条都要）——
cd designer && npx tsc --noEmit && npx vite build

:: —— 5 行为验证（GUI / 事件 / 动作类必做）——
examples\gallery\gallery.exe --selftest examples\gallery\gallery.cgui
<exe> --trace                          :: 开窗前打印控件树 + 动作表，核对是不是你以为的那样

:: —— 6 动了任何 .bat 之后（必做）——
powershell -NoProfile -ExecutionPolicy Bypass -File tools\bat-utf8.ps1 -Mode fix

:: —— 7 改了 .codebuddy/skills\ 之后（必做）——
node tools\check-skills.mjs            :: 目录 / INDEX 总数 / 导读目录树 / frontmatter / scopes 五处一致

:: —— 8 目视取证（观感 / 像素类改动）——
tools\shot.ps1                         :: 截业务 exe 整窗（PrintWindow，不怕被遮）
tools\shot-canvas.ps1                  :: 截画布窗口
tools\shot-widget.ps1                  :: 截单个控件放大图
```

**三条实测铁律（本工程踩出来的，别重踩）**：

1. **验收命令必须"能失败"**：cmd 在**解析整行时**就展开 `%errorlevel%`，回显的是上一条命令的退出码；
   `| findstr` 之后的退出码是 `findstr` 的；GBK 管道里 grep 中文恒空。→ **判定读日志，不信回显**；
   自建探针要正反例各跑一次，反例必须真的红。
2. **"两条命令"要写成两条**：`npx tsc --noEmit && npx vite build` 缺 `cd designer` 会跑在错目录，且
   `read_lints` 报不出 TS 语法错误 —— 必须真跑。
3. **同一事实有两份的地方靠机器兜**：控件类型表、事件表、`.cgui` 字段语义、动作表、支持库表
   → 全靠 `tools\check-format.bat`。手改其中一处而没跑它 = 埋雷。

---

## §4 审查路由与三角色审查

映射表权威定义在 `global-rule.md` §7，此处是执行口径：**涉及哪个领域就加载哪个，跑完整清单，不泛泛而过。**

| 改了什么 | 加载 | 一眼可判的典型问题 |
|---|---|---|
| 缓冲区 / 字符串 / memcpy / 格式化 | `buffer_overflow_check` | 越界、整数溢出、危险函数 |
| malloc / free / 句柄 / GDI 对象 | `memory_leak_check` | 泄漏、野指针、double free、GDI 对象没 DeleteObject |
| 串口 / TCP / UDP / 帧协议 | `communication_protocol_check` | 超时、粘包、切帧、字节序、重连 |
| 错误码 / 断言 / 日志 / 回调失败路径 | `error_handling_check` | 返回值不判、失败路径没释放资源 |
| 结构体 / 位域 / 函数指针 / 柔性数组 | `struct_best_practice_check` | 对齐、pack、只读表漏 `const` |
| 新增 / 改 `.c` `.h` | `firmware_code_style` | 命名、分层、Doxygen 注释、项目铁律 |
| 界面观感 / 配色 / 字体 / 间距 / 动效 / 文案 | `design-taste` | 设计三旋钮、AI 味黑名单、四态齐全、对比度 |
| 改 `designer/` | `designer-modify` | `.cgui` 契约、新增控件要动的 7~8 处 |

**三角色审查法**——执行协议与健康度判据的唯一权威是 **`adversarial-review`**（本技能不复制其正文）：
同一份 diff 过三遍，不要一遍包打天下——

| 角色 | 任务 | 硬约束 |
|---|---|---|
| 挑刺 | 列问题，**每条带 E1+ 证据**（文件:行 / grep 结果 / 调用链 / 开关值） | 只指控、不给方案；写不出触发路径就丢弃 |
| 辩护 | 对每条问题找**反证**（是不是漏看了上层？是不是编译不到？是不是环境假象？） | 默认"挑刺者是错的"，**重读原文**；驳回必须附反证出处 |
| 改进 | **只修没被驳倒的那些**，一次覆盖同类全部 | 配**能变红**的回归判据；驳倒的写进"驳回项"防后人误改 |

健康度：成立率 100%（⇒ 驳回率 0%）⇒ 辩护者橡皮图章，重做；**三段分三次输出**，
禁止一次给完"问题 + 裁定 + 修复"（那是自证自话）。

> 审查类技能正文里的 `esp_*` / `HAL_*` / FreeRTOS 示例是**上游模板残留**，本工程没有这些 API；
> 判据看**缺陷模式**（越界、未释放、返回值不判、切帧假设），不看示例里的 API 名。

**风险等级**：🔴 致命 / 🟠 高危 / 🟡 中危 / 🟢 低危。
`strictMode: true` 下 🔴🟠 必须修完才提交。每条问题必须带：文件 + 行号 + 等级 + 原理 + 修复示例。

---

## §5 收尾（做完不等于收好）

1. **变更摘要**：改了哪几个文件（用 `git status --short` 逐条过，手写清单必漏）、提交 hash。
2. **`.codebuddy/` 层改动单独说明**（计划 / 技能 / 审查报告属于 AI 侧配置，要让人看得见）。
3. **进度留痕**（学自 vibe-coding-cn 的 `progress.md`）：计划文件末尾追加"已完成 / 下一步 / 遗留"，
   **换会话续跑的人第一件事就是读它** —— 上下文是第一性要素，垃圾进垃圾出。
4. **沉淀**：踩到"非显然、下次还会踩"的坑 → 写进 `.codebuddy/memory/`（不入库，按类型分文件，
   并在 `MEMORY.md` 加一行索引）；**代码里能查到的一律不写**（路径 / 结构 / git 历史都不写）。
5. **并行交付**：成员回执逐条销账；`teams/` 运行态不清（gitignore）。
6. **临时产物**（探针、截图、提交信息文本）一律落 `build\`（gitignore），**收尾不清理**。

---

## §6 中断与失败恢复

| 症状 | 恢复动作 |
|---|---|
| 会话断了 / 换一轮继续 | **先读** `.codebuddy/plans/<feature>.md` 的进度段 → `git --no-pager status --short` → 从最后**未完成**的那段续；已完成的不要重做（别让 AI 凭印象开工） |
| 编译不过 | 先查**示例 exe 是否被残留进程锁住**（截图脚本常留），`taskkill` 后再编 —— 否则会报莫名其妙的 `undefined reference` |
| 门禁红了但改的是"仅注释/格式" | 给**骨架等价证明**（词法剥离后逐字节比，不能用朴素正则剥注释），否则不承认"无行为变化" |
| 批量编辑后不确定落盘没 | `git status --short` + 抽查字节数 / 行数；有 0 新增行的文件就是没落盘 |
| 验证方法本身有问题 | 回 P1 改计划的验证方法并说明原因，不要硬凑"通过" |
| 反复卡壳 | 回退到上一个 commit 换一条路重试（学自 vibe-coding-cn）；**不要在同一个错误假设上叠第四层补丁** |

---

## §7 禁区（本工程没有的东西，一律不得出现在输出里）

`idf.py` · `ai_code_review.py` · `CMakeLists.txt` · `sdkconfig` · `app_main` · FreeRTOS / `esp_*` / `HAL_*` ·
多板型 / 子模块 · `proj_*` 工程名 · 中断 / DMA / RTOS 任务 / 看门狗 / Flash 存储（只在"给板子写配套固件"时才需要）。

**本工程真正的对应关系**：

| 上游模板的词 | 本工程对应 |
|---|---|
| 中断 / 任务 | `cg_set_timer`、串口/网口线程（线程只搬字节，控件不是线程安全的） |
| 外设 | `cg_serial_*` / `cg_net_*` / GDI 句柄 |
| 复位 / 降级 | 关窗路径（回调里不要立刻销毁控件，用 `cg_set_timer` 延后） |
| 烧录实测 | `--selftest` / `--trace` / `tools\shot.ps1` / 真机键鼠探针 |

---

## §8 输出格式（走完流程后给）

```
## 完成
- 需求：<原文>
- 路径：C→P0→…→P7（标注跳过哪段 + 原因）
- 门禁：§3 表中每条 —— 通过 / 跳过+原因（贴关键输出，不写"已验证"这种没证据的话）
- 收敛：Converged / 未收敛（差哪几条）
- 产出：计划 .codebuddy/plans/<name>.md ｜ 报告 .codebuddy/code-reviews/<日期>-<name>.md
- 改动：<用 git status --short 过的清单>

## 对抗性闸门（不许省）
- 已否掉的项：<自己提过但被自己否掉的 + 否掉理由>
- 降级为观测的项：<根治方向 + 为何本轮不做>
- 未验证假设：<还需验证什么>
- 置信度：高 / 中 / 低 + 理由

## 遗留
- <已知风险 / 没做的边界>
```

> **贴实际输出，不要写"已验证通过"这种没有证据的结论；判不出来的就说判不出来。**
