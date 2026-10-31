# T2 · 架构与实现团（`t2-architecture/`）

> 本目录**不是技能**，只是分组的壳：团队说明一律只写 `README.md`。
> 若在这里放 `SKILL.md`，技能扫描会停止下钻、把整团吞成一个技能，成员全部消失。

**使命**：代码落在正确的层，按计划实现，有测试兜底
**何时点将**：写改代码、加控件/字段、改画布、接手陌生模块、重构
**团长**：`piv-implement`

## 成员（按上场顺序）

| # | 技能 | 一句话 |
|---|---|---|
| 1 | `piv-implement` | 严格按照实现计划执行编码，不做计划外改动 |
| 2 | `prime-codebase` | 加载代码库上下文（Context is King） |
| 3 | `analyzing-projects` | 系统性分析陌生代码库——梳理结构、技术栈、模块划分、约定与设计模式 |
| 4 | `piv-plan-implementation` | 为单个功能/切片制定详细实现计划，写入 .codebuddy/plans/ |
| 5 | `test-driven-development` | C 层测试先行（RED→GREEN→REFACTOR）——没有先失败的断言，不写生产代码 |
| 6 | `designer-modify` | 修改 CStackGUI 的 UI 设计器（designer/ —— Electron + React 19 + Vite + TS 的 .cg |

## 最小加载集

只加载**命中的这一个团**；进团后按上表顺序逐个加载，**不要整团全加载、不要预加载下游团**。
编制真值在 `../_registry/teams.toml`，本文件由 `python tools\check_roster.py` 校验。
