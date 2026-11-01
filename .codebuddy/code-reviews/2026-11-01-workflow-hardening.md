# 审查报告：工作流硬化（2026-11-01）

> 范围：`.codebuddy/` 元层与 `tools/` 判据。未触碰 `runtime/` `designer/` `examples/`。
> 计划：`.codebuddy/plans/workflow-hardening.md`

## 1. 改动清单（`git status --short` 逐条）

| 文件 | 类型 | 说明 |
|---|---|---|
| `tools/hooks/inject-context.cjs` | M | 简报不再把 3 个元门禁报成「上次收工门禁：全绿」 |
| `tools/hooks/verify-on-stop.cjs` | M | 门禁结果改三态；判不出来不覆盖基线；判据抽成可测纯函数 |
| `tools/hooks/test-verify.cjs` | A | 10 条判据回归用例 |
| `tools/gate.bat` | A | 门禁单一入口（full / fast / skills / canvas / doctor） |
| `tools/check-format.bat` | M | 接入 `test-verify.cjs` |
| `tools/check_roster.py` | M | 新增判据 F：`expert-teams` §1 总表 ↔ `teams.toml` |
| `.codebuddy/README.md` | M | 版本历史改单指 `global-rule.md` §10；删「41→28 / 28→29」旧演进数字 |
| `.codebuddy/skills/t0-orchestration/sop/SKILL.md` | M | §3 顶部加「日常执行入口 = `gate.bat`」 |
| `.codebuddy/plans/workflow-hardening.md` | A | 本方案 |
| `.codebuddy/code-reviews/2026-11-01-workflow-hardening.md` | A | 本报告 |

**未提交但已生效**：`git config core.hooksPath = .githooks`（本地仓库配置，不进版本库）。

## 2. 验证证据（命令 + 实际输出）

| 验证 | 命令 | 结果 |
|---|---|---|
| 钩子接线 | `git config --get core.hooksPath` | `.githooks`（改前为空） |
| 钩子接线（自检模式） | `tools\gate.bat --doctor` | `[ok] pre-push 已接线` |
| 简报措辞 | `node tools/hooks/inject-context.cjs` | `门禁 元门禁: 全绿 ｜ 业务门禁(编译/单测/契约): 未自动跑` |
| 判据正例 | `node tools/hooks/test-verify.cjs` | `[ok] verify-on-stop 判据全过：10 条` |
| 判据反例（测试有效性） | `node build/_probe_old_verify.cjs` | `[ok] 反例真的红：旧实现会用超时抹掉绿基线` |
| 编制正例 | `python tools\\check_roster.py` | `四处一致` |
| 编制反例（能红） | 临时从 §1 的 T3 行删 `design-taste` | `[fail] §1 的 T3 成员与 teams.toml 不一致：少 ['design-taste'] 多 []`；已恢复，`git diff` 对该文件无输出 |
| 技能子集门禁 | `tools\gate.bat --skills` | 5 步全绿 |
| 全量门禁 | `tools\gate.bat --full` | `[ok] gate passed (full)`，含新接入的 `test-verify` 与判据 F |
| bat 编码 | `bat-utf8.ps1 -Mode check` | `16 file(s) checked - UTF-8 BOM-less + CRLF + ASCII prelude` |
| 空白卫生 | `git diff --check` | 仅一条 CRLF 提示（`.codebuddy/README.md`），非空白错误 |

## 3. 三方裁定

| # | 指控（挑刺） | 等级 | 辩护（找反证） | 裁定 |
|---|---|---|---|---|
| C1 | `gate.bat --canvas` 用 `npm run build --prefix designer`，与 `sop` §3 的 `cd designer && npx vite build` 形态不同，可能是假门禁 | 🟠 | 读 `designer/package.json`：`"build": "vite build"`、`"typecheck": "tsc --noEmit"`，两条脚本存在且语义等价；`--prefix` 作用于 npm 而非 vite，行为正确 | **驳回**（附出处：`designer/package.json` scripts 段） |
| C2 | `.last-gate.json` 新增的 `scope` 字段没有任何消费者，是死字段 | 🟡 | 确认无消费者。但保留是有意的：将来业务门禁若要写这个文件，靠它区分「这次写的是哪类门禁」，避免又出现一次伪绿灯 | **成立，降级为观测**（理由见上；不修，防止后人误当遗漏删掉） |
| C3 | `test-verify.cjs` 用 `require` 引入 `verify-on-stop.cjs`，可能会把 hook 主流程一起跑起来（读 stdin、写状态、返回 feedback） | 🟠 | `verify-on-stop.cjs` 末尾是 `module.exports = {...}` + `if (require.main === module) main()`；`require` 时 `require.main` 不是它，主流程不执行。实测跑 10 条用例，无任何 hook 输出、未写 `.last-gate.json` | **驳回** |
| C4 | 判据 F 用 `cells[3]` 位置索引取成员列，表格加一列就会误判 | 🟡 | 会误判为「§1 总表里找不到 T3 那一行」而不是静默放过 —— 属于**响亮失败**，可接受；表结构由 `teams.toml` 的渲染约定固定 | **驳回**（失败方式是响亮的，不是静默的） |

**健康度**：4 条指控，1 条成立（且降级）→ 成立率 25%，驳回率 75%。辩护方给出了反证出处，不是橡皮图章。

## 4. 遗留

| 项 | 状态 | 理由 |
|---|---|---|
| `.last-gate.json` 的 `scope` 无消费者 | 观测 | 预留位，见 C2 |
| plans / code-reviews 缺「该落未落」判据 | 未做 | 下轮：先统计再定阈值，否则会批量生成空壳报告 |
| schemas 无 validator | 未做 | 下轮：需先定报告结构的权威字段集 |
| `sop` §3 命令形态一致性判据 | 未做 | 下轮：历史计划里全是旧形态，误报面大 |
| 产出物目录归一（`docs/PRD_*` vs `plans/*-prd.md`） | 待用户定夺 | 文档组织取向，非技术对错 |
| 新 clone 仍需手工跑 `install-hooks.bat` | 已知 | 已由 `gate.bat --doctor` 兜住提示；`core.hooksPath` 是本地配置，无法进库 |
