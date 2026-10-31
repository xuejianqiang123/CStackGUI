# T3 · 质量审判团（`t3-quality/`）

> 本目录**不是技能**，只是分组的壳：团队说明一律只写 `README.md`。
> 若在这里放 `SKILL.md`，技能扫描会停止下钻、把整团吞成一个技能，成员全部消失。

**使命**：用「指控→辩护→裁定」把缺陷挡在提交前
**何时点将**：代码写完、提交前、界面观感评审、挑刺/复核/对抗式
**团长**：`piv-review-changes`

## 成员（按上场顺序）

| # | 技能 | 一句话 |
|---|---|---|
| 1 | `piv-review-changes` | 提交前对全部改动做技术审查（质量门禁），产出审查报告到 .codebuddy/code-reviews/ |
| 2 | `adversarial-review` | 三角色对抗式代码评审（互相审判）——挑刺者带证据指控 → 辩护者默认"挑刺者是错的"独立复核裁定 → 改进者只修裁定成立项并配能变红的回归判据 |
| 3 | `buffer_overflow_check` | 检测缓冲区溢出、数组越界、字符串操作越界、memcpy/strcpy/sprintf 等危险函数的边界安全 |
| 4 | `memory_leak_check` | 检测内存泄漏（动态分配未释放）、堆碎片、内存越界、野指针，以及句柄/GDI 对象未配对释放等问题 |
| 5 | `communication_protocol_check` | 审查串口、TCP/UDP、帧协议（cg_frame_*）的稳定性、超时处理、校验机制、粘包切帧与重连 |
| 6 | `error_handling_check` | 审查错误码处理、异常捕获、断言使用、日志记录、容错机制、故障恢复策略 |
| 7 | `struct_best_practice_check` | 审查结构体设计规范、内存优化、对齐问题、函数指针架构、柔性数组、位域使用等 18 种高级用法的正确性与最佳实践 |
| 8 | `firmware_code_style` | CStackGUI 的 C 代码规范（合并版，2026-09-20 吸收原 c-function-comment） |
| 9 | `design-taste` | 视觉/交互/文案的「设计品味」门禁 —— 动手前先做设计读盘、定三个旋钮，动手后按反模式黑名单与质量地板自检，消除"一眼 AI 做出来的"界面 |

## 最小加载集

只加载**命中的这一个团**；进团后按上表顺序逐个加载，**不要整团全加载、不要预加载下游团**。
编制真值在 `../_registry/teams.toml`，本文件由 `python tools\check_roster.py` 校验。
