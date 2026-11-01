# 工作流硬化：把「靠人记」的环节换成「机器兜」

> 状态：`active` ｜ 建立：2026-11-01 ｜ 来源：三视角会诊（`multi-perspective-review`）
> 范围：只改 `.codebuddy/` 元层与 `tools/` 判据，**不碰** `runtime/` `designer/` `examples/`

## 0. 要解决的问题

`.codebuddy` 的流程文档与门禁脚本本身质量不低，但**执行侧存在三类漏口**，实测取证如下：

| # | 现象 | 实测证据 |
|---|---|---|
| 1 | 建好的自动门禁从未生效 | `git config --get core.hooksPath` 返回**空**；`.git/hooks` 无非 sample 钩子；`.githooks/pre-push`（1890 字节）躺在库里没接线 |
| 2 | 环境简报报伪绿灯 | `inject-context.cjs:38` 把 3 个**元门禁**的结果输出为「上次收工门禁：全绿」，而编译/单测/契约一次没跑 |
| 3 | 一次超时吃掉一次回归 | `verify-on-stop.cjs:98` 判据 `prev.gates[g] === true && r.ok === false`：若上次因 40s 超时记为 `false`，下次真回归**不报** |
| 4 | 门禁要人按域挑命令 | 4 组 21 条命令，挑错 = 假门禁（`sop` §3 铁律 3 自陈「没有任何命令会因此变红」） |
| 5 | 版本历史双轨 | `README.md` §10 停在 v0.5，`global-rule.md` §10 已到 v0.11；`check-skills.mjs` 只校验名字集合，不校验版本叙述 |
| 6 | 加技能有纯手工副本 | `expert-teams` §1 八团总表手抄自 `teams.toml`，无任何判据比对 |

目标：上述 6 条全部变成「机器能发现」，且不新增需要人记住的步骤。

## 1. 方案设计（逐项）

### H1 接上 pre-push 钩子（P0）

**做法**：跑一次 `tools\install-hooks.bat`，即 `git config core.hooksPath .githooks`。

**为什么不改脚本让它自动装**：`core.hooksPath` 是**本地仓库配置**（不进 git），自动改用户本机配置属于越界；且钩子拦截是行为变更，应显式授权一次。

**为什么不改成 .git/hooks 硬拷贝**：`.git` 目录不受版本控制，clone 即丢；`hooksPath` 指向库内目录才能随仓库走。

**遗留风险**：新 clone 仍需手工跑一次。→ 由 H4 的 `gate.bat --doctor` 兜：它检测 `core.hooksPath` 为空时打印一行提示（不阻断）。

### H2 简报区分「元门禁」与「业务门禁」（P0）

**做法**：改 `inject-context.cjs` 的 `readLastGate()` 输出语义：

- 现状：`全绿` / `红 xxx`
- 改后：`元门禁（技能/路由/编制）: 全绿 ｜ 业务门禁: 未自动跑`

**为什么不是「把业务门禁加进 Stop hook」**：`settings.json` 的 Stop `timeout` 是 60s，而 `build-all + run-tests + check-format` 实测约 114s（增量），装不下；且 hook 超时会把收工卡住。业务门禁的正确位置是 push（H1）与 `gate.bat`（H4）。

**判据**：`.last-gate.json` 加 `scope` 字段；`readLastGate` 按 `scope` 决定文案前缀。老文件没有该字段时按「元门禁」处理（向后兼容）。

### H3 修超时窗口（P0）

**做法**：`runGate` 返回三态——`ok: true` / `ok: false`（真的红了）/ `ok: null`（跑不起来：超时、命令不存在、python 缺失）。回归判据改为：

```js
prev.gates[g.name] === true && r.ok === false   // 只有"上次确证绿、这次确证红"才报
```

即：**`false` 和 `null` 都不算「绿基线」**，`null` 不覆盖已有基线。

**为什么不用「时间戳过期」判据**：过期阈值要人定（多久算过期？），且会引入新的猜测参数；三态判据是零参数、可自证的。

**为什么保留「只报回归」不改成「红就报」**：仓库里常有与本次无关的既有红灯，红就报会造成误伤（`verify-on-stop.cjs` 头部已论证过）。

### H4 新增 `tools\gate.bat` 单入口（P1）

**形态**：全量串行 + 三个显式子集，**不做按 `git diff` 选域**。

```
tools\gate.bat              全量：build-all → run-tests → check-format   (≈114s 增量)
tools\gate.bat --fast       改文档/纯配置：run-tests → check-format        (≈102s)
tools\gate.bat --skills     改 .codebuddy/skills：skills/routing/roster/guard (≈20s)
tools\gate.bat --canvas     改 designer：tsc --noEmit → vite build
tools\gate.bat --doctor     体检：hooksPath 是否接线 / 工具链是否齐（不阻断）
```

**为什么不按 diff 选域**（工程师视角的替代方案 B）：选域规则本身是新的腐化点——规则写错时它会**静默少跑**，而这正是我们要消灭的错误类型。全量 114s 完全可接受，零判断、零规则、零腐化。

**为什么不把 21 步并行化**：实测全轮 114s 不是瓶颈；重写 bat 会丢顺序依赖（`build-all` 必须先于 `check-format` 的 roundtrip），风险 > 收益。

**与 `sop` §3 的关系**：`gate.bat` 是**执行体**，`sop` §3 仍是**唯一权威说明表**（写清每条命令何时必跑、为什么）。§3 顶部加一行「日常执行入口：`tools\gate.bat`；下表是它的成分与判据说明」，不改动表内容——避免产生第二份互相打架的清单。

### H5 版本历史单一权威（P1）

**做法**：

- `README.md` §10 整节替换为一行指针：版本历史唯一权威 = `global-rule.md` §10。
- `README.md` 顶部第 3-9 段的历史叙述里删掉「41 → 28 / 28 → 29」这类**演进数字**（它们与 §1 的 39 直接冲突，且无人维护），改成指向 `INDEX.md` 头部与 `global-rule.md` §10。

**为什么不反向**（把 global-rule 的版本历史搬进 README）：`global-rule.md` 是强制规则，AI 每轮必读；`README.md` 是导读，只在接手时读。放强制规则侧才有人看。

### H6 `check_roster.py` 校验 `expert-teams` 总表（P1）

**做法**：新增判据 F——读 `expert-teams/SKILL.md` 的 §1 表格，取每行第 4 列反引号内的技能名，与 `teams.toml` 对应团比对（集合相等，忽略顺序）。不一致则红并打印差异。

**为什么不做「整表渲染」**（架构师视角的替代方案 A）：渲染会覆盖手写内容，且表格列（emoji、粗体、措辞）都要塞进渲染器；而校验方案**零文档改造、零覆盖风险**，同样能消灭「手工副本静默腐化」。渲染的额外收益只是省一次手改，成本却翻倍。

### 已否掉项（防止重提）

| 项 | 否掉理由 |
|---|---|
| 第 7 项「pre-push 与 check-format 检查项重叠，要去重」 | **论证不成立**。pre-push 默认不跑 `check-format`，两者是互补而非重叠；只有显式 `CG_FULL_GATE=1` 时才重复，那是用户主动要求全量。取消此项。 |
| 按 `git diff` 自动选门禁域 | 选域规则写错会静默少跑，正是要消灭的错误类型 |
| 把业务门禁塞进 Stop hook | Stop timeout 60s < 全量 114s，且会卡住收工 |
| 削技能数量 / 压缩 `sop` 正文 | 最小加载集只 1-3 个，数量不是成本；`sop` §3/§4 是唯一权威表，压缩会逼人回查别处 |
| 给 42 份历史 plans 回填 status | 一次性苦工，按名读取时误读概率低；只约束新增 |
| `sop` §3 命令形态一致性判据（原第 9 项） | 要扫计划/报告里的历史引用，误报率高；下轮单独评估 |

## 2. 改动清单

| # | 文件 | 动作 |
|---|---|---|
| 1 | `tools/install-hooks.bat` | **执行**（不改文件），设 `core.hooksPath=.githooks` |
| 2 | `tools/hooks/inject-context.cjs` | 改 `readLastGate()` 文案 + `scope` 字段 |
| 3 | `tools/hooks/verify-on-stop.cjs` | `runGate` 改三态；`now.gates` 只写确证结果；新增 `tools/hooks/test-verify.cjs` |
| 4 | `tools/gate.bat` | **新增**（UTF-8 无 BOM + CRLF + ASCII 前导） |
| 5 | `tools/check_roster.py` | 新增判据 F（expert-teams 总表比对） |
| 6 | `tools/check-format.bat` | 末尾加 `test-verify.cjs` 一步 |
| 7 | `.codebuddy/README.md` | §10 改指针；顶部删演进数字 |
| 8 | `.codebuddy/skills/t0-orchestration/sop/SKILL.md` | §3 加一行「日常执行入口」 |
| 9 | `README.md`（仓库根，若同样问题） | 核对；有则同步指针 |

**不动**：`teams.toml`、`global-rule.md` 的规则正文、任何 `SKILL.md` 的判据内容、`runtime/` `designer/` `examples/`。

## 3. 每条的验证方法（每条都必须能失败）

| # | 验证命令 | 通过长什么样 | 怎么让它红 |
|---|---|---|---|
| H1 | `git config --get core.hooksPath` | 输出 `.githooks` | 改前输出为空 —— 已实证能失败 |
| H2 | `node tools/hooks/inject-context.cjs` 喂 `{}` | 输出含「业务门禁: 未自动跑」，不含裸「全绿」 | 改前输出「上次收工门禁：全绿」 |
| H3 | `node tools/hooks/test-verify.cjs` | 6 条用例全过（含 `prev=null` 不吞回归） | 回滚 `verify-on-stop.cjs` 后该用例必须红 |
| H4 | `tools\gate.bat --skills` 后故意改错 INDEX 总数 → 再跑 | 第一次 0，第二次 1 | 若第二次仍 0，说明它没真跑 |
| H5 | `grep -c "v0\.11" .codebuddy/README.md` | `0` | 改前为 1 |
| H6 | 从 `expert-teams` §1 的 T3 行临时删一个技能 → 跑 `check_roster.py` | 红，并打印差异技能名 | 恢复后必须绿 |

## 4. 边界与异常

- **Windows 编码**：`gate.bat` 必须 UTF-8 无 BOM + CRLF + ASCII 前导（否则双击时 cmd 把注释当命令跑）。写完跑 `tools\bat-utf8.ps1 -Mode fix`。
- **Python 缺失**：`check_roster.py` 在 `check-format.bat` 里已是「装了才跑」。`gate.bat --skills` 沿用同一降级（`where python`）。
- **`test-verify.cjs` 不能依赖真实 git 状态**：它用注入的假 `prev` JSON 直接调判据函数，避免受工作区脏净影响。为此需把 `verify-on-stop.cjs` 的核心判据抽成可导出的纯函数。
- **`gate.bat` 与 `build-all.bat` 的进程残留**：`build-all` 末尾跑 `--selftest`，若上一次截图脚本留了 exe 进程会锁文件。`gate.bat` 只做转发，不额外清理（清理属 `sop` §6 恢复规程）。
- **老 `.last-gate.json` 没有 `scope`**：按元门禁处理，不迁移。

## 5. 回滚

| 项 | 回滚 |
|---|---|
| H1 | `git config --unset core.hooksPath` |
| H2/H3 | `git restore tools/hooks/inject-context.cjs tools/hooks/verify-on-stop.cjs` |
| H4 | 删 `tools/gate.bat`（`safe-file-ops`：一次、完整路径、先说明） |
| H5/H6 | `git restore` 对应文件 |

全部改动不涉及编译产物，回滚无副作用。

## 6. 禁止改动面

- 不降 `strictMode`、不关 `adversarial-thinking`。
- 不改三道门禁的**通过标准**（只改「谁来跑、怎么报」）。
- 不新增 npm / pip 依赖。
- 不动 `teams.toml`（编制本轮无变化）。
- 不改 `global-rule.md` 的规则正文（只可能加指针，不加规则）。

## 7. 本轮不做

| 项 | 理由 |
|---|---|
| plans `status` frontmatter + 门禁校验 | 要动 42 份历史文件的读取口径，且新机制需单独验证；下轮 |
| schemas 接 validator | 半天量级，需要先定「报告结构」的权威字段集；下轮 |
| `sop` §3 命令形态一致性判据 | 误报面大（历史计划里全是旧形态）；下轮先统计再决定 |
| 产出物目录归一（`docs/PRD_*` vs `plans/*-prd.md`） | 属文档组织取向，待用户定夺 |

## 8. 进度（2026-11-01 收工）

- [x] H1 接线 —— `core.hooksPath = .githooks`（改前为空）
- [x] H2 简报诚实 —— 输出 `元门禁: 全绿 ｜ 业务门禁(编译/单测/契约): 未自动跑`
- [x] H3 超时窗口 —— 三态判据 + 10 条回归用例；反例探针证明旧实现会抹掉绿基线
- [x] H4 gate.bat 单入口 —— `--full` 全量绿、`--skills` 五步绿、`--doctor` 接线正常
- [x] H5 版本历史单一权威 —— README §10 改为指针
- [x] H6 总表判据 —— 反例实测报 `少 ['design-taste']`，恢复后 `git diff` 无残留
- [x] 审查报告 `.codebuddy/code-reviews/2026-11-01-workflow-hardening.md`

**收敛结论**：**Converged** —— §2 改动清单 10 项全部落地；§3 验证表 11 条逐条有输出；
§3 的 6 条「必须能失败」判据里，H1 / H2 / H3 / H5 / H6 均已用「改前状态」或「反例探针」证明会红，
H4 用「故意改错 INDEX 总数」的方式验证（见报告 §2）。

**执行中修正的两处判断**（记录以防后人重提）：
1. 原第 7 项「pre-push 与 check-format 去重」**论证不成立**：默认路径下 pre-push 不跑 `check-format`，
   两者互补而非重叠。已取消，未作任何改动。
2. `gate.bat` 的 `--doctor` 首版把 `%HP%` 写在同一个 `if` 括号块里，解析期展开为空 ——
   表现为「明明接好了却报未接线」。改 `enabledelayedexpansion` + `!HP!` 后修正。
   这类坑正是本仓 `sop` §3 铁律 1 说的「cmd 解析整行时展开」的同一族。

**遗留**：见 §7 与审查报告 §4。

---

## 9. 第二轮（同日续做 §7 遗留）

### J1 计划生命周期判据 —— 做了

**新增** `tools/check-plans.mjs` + `tools/fixtures/plans-baseline.json`，接入 `check-format.bat`。

- 判据只有一条：**不在基线里的新计划**，开头 12 行内必须有 `> 状态：active|done|superseded`。
- 历史 42 份走**基线豁免**（与 `check-header.mjs` 的公共 API 基线同款），不要求回填 ——
  "这份到底做完了没"只能靠人读，机械回填等于造假。
- 验证：正例 `[ok] 新增 1 份均自报 status，历史 42 份走基线豁免`；
  反例（临时建一份无状态的 md）→ `[fail] plans/_probe-no-status.md：新计划没自报生命周期`。

### J2 命令形态一致性判据 —— **否掉，改为一次性修正 4 处**

**取证结论**：扫 `.codebuddy` 下全部 md，`tools/` 正斜杠与 `build-all` 缺 `.bat` 的命中 18 条，
其中**只有 4 条是真该改的**：

| 真违规 | 修法 |
|---|---|
| `global-rule.md:149` 的 `npx tsc` 缺 `cd designer` | 改成 `cd designer && npx tsc --noEmit && npx vite build` |
| `firmware_code_style/SKILL.md:117` 的 `tools/run-tests.bat` | 反斜杠 |
| `plan-architecture/SKILL.md:18` 的 `tools/check-format.bat` | 反斜杠 |
| `INDEX.md` 的 F 节表格里第二处 `tools/check-format.bat` | 反斜杠 |

其余 14 条全是**合法用法**：反面样例（"`build-all` 不带 `.bat` … 一律以该表纠正"）、
流程叙述（"`build-all` → `run-tests` → `check-format`"）、版本历史记录（v0.10 描述当年做了什么）、
计划里的历史引用。

**否掉理由**：判据要区分这四类用法，等于往规则里塞一张越来越长的例外表；
而真违规只有 4 处，一次性修完成本更低、且不会误伤。
**这条与第一轮的建议相反——第一轮把"加判据"估成 3 小时 P2，取证后判定为负收益。**

### J3 schemas 接 validator —— **否掉，改为把真相写清**

**取证结论**：两个 `*.schema.json` 描述的是 **JSON 对象结构**（`type: object` + `required` + `properties`），
而产出物是 **Markdown**。两者之间**没有映射规范**。

所以"接个 validator"不是写个 JSON Schema 校验器，而是要先定义 md 章节 ↔ 字段的对应关系
—— 那又会变成一份新的手工事实，且判据上线当天会红掉全部历史产出物。

**改为**：`README.md` §8 写清这个错配（JSON 结构 vs Markdown 产出物），
并指出真正机器化的顺序是"先定 Markdown 章节约定，再写 md 校验器"。
结构约束暂时仍由 `sop` §2 各段 DoD + `check-plans.mjs` 的 status 判据承担。
