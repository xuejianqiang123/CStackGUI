# T6 · 事实与文档团（`t6-documentation/`）

> 本目录**不是技能**，只是分组的壳：团队说明一律只写 `README.md`。
> 若在这里放 `SKILL.md`，技能扫描会停止下钻、把整团吞成一个技能，成员全部消失。

**使命**：文档与代码同源，结论有证据，文字不像机器写的
**何时点将**：下「已实现/未实现」结论、文档冲突、写正式文档、画时序图
**团长**：`evidence-discipline`

## 成员（按上场顺序）

| # | 技能 | 一句话 |
|---|---|---|
| 1 | `evidence-discipline` | 取证纪律——在给出"某功能已实现 / 某能力被使用 / 旧仓做过 / 两个说法哪个对"这类结论前，如何采集可证伪的证据，避免用伪证据下结论 |
| 2 | `doc-truth-sync` | 文档与代码事实的双向同步规程——防止"文档腐化滞后于代码"让 AI/新人读到过时需求状态后跳过已实现功能、重复实现、或上报不存在的问题 |
| 3 | `humanizer` | 去掉**文字**里的 AI 味（正式产出物专用：README / docs / 教学说明 / PR 描述 / 提交说明） |
| 4 | `wavedrom-gen` | Generate, revise, validate, and render official WaveDrom diagrams from n |

## 最小加载集

只加载**命中的这一个团**；进团后按上表顺序逐个加载，**不要整团全加载、不要预加载下游团**。
编制真值在 `../_registry/teams.toml`，本文件由 `python tools\check_roster.py` 校验。
