---
name: firmware_code_style
description: CStackGUI 的 C 代码规范（合并版，2026-09-20 吸收原 c-function-comment）。覆盖：函数 Doxygen 注释格式（用户指定）、命名、结构、安全、项目特有铁律。触发条件：新增或修改 runtime/ examples/ apps/ 下的 .c/.h，或审查 C 代码规范时自动执行。
auto_invoke: true
argument-hint: [文件路径或函数名]
---

# 技能：CStackGUI C 代码规范（合并版）

> 适用范围：`runtime/`、`examples/`、`apps/`、`tests/` 下的全部 `.c` / `.h`。
> 本项目是 Win32 GUI 运行时（不是单片机固件）：原通用嵌入式规范里的 HAL/寄存器/RTOS
> 条款不适用，已裁剪；保留命名/结构/安全/注释四类可执行规则，并补充本项目铁律。
> 原 `c-function-comment` 技能已合并进本技能（其 Doxygen 格式是用户指定的，全文收录在 §1）。

## 触发条件

- 新增或修改 `.c` / `.h` 文件
- 函数定义、变量声明、宏定义、结构体定义
- 代码注释、文件头
- 审查 C 代码规范

## §1 函数注释（用户指定的 Doxygen 格式，强制）

本项目 C 函数一律使用下面这种 Doxygen 块注释，放在函数定义（或声明）上方：

```c
/**
 * @brief 读一个文件到堆内存
 * @param path 文件路径
 * @return char* UTF-8 原文，失败返回 NULL（调用方负责 free）
 */
char *cg_read_file(const char *path);
```

规则：

- `@brief`：一句话说清函数**干什么**，不加句号、不写"这个函数用于"这种废话
- `@param`：**每个参数一行**，`@param 名字 说明`；说明写语义（单位、所有权、取值范围），不要复述类型
  - 无参数的函数不写 `@param`（也不要写"@param 无"）
  - 出参、入出参在说明里写清，如 `@param err 解析失败时写错误信息`
- `@return`：`@return 类型 说明`；写清**取值含义**（0 成功 / -1 失败 / NULL 表示什么），
  内存所有权要写明（谁 free）
- 回调函数（`cg_event_fn` 签名）同样适用，`@param user` 写清是谁挂进去的业务数据
- `static` 内部函数同样适用；头文件声明已有完整注释时，定义处可只留 `@brief` 一行
- **注释与实现不一致时以实现为准**，顺手把注释改对
- `.cgui` 相关函数的注释里写清对应的字段语义（画布侧 `model.ts` 要对得上）

文件头：每个 `.c`/`.h` 顶部一段 `@file/@brief`（一行式即可，说明该文件在整体里的职责）：

```c
/**
 * @file    cg_timer.c
 * @brief   一次性定时器：事件回调里推迟一件事做，后端 50ms 心跳驱动
 */
```

## §2 命名

- 变量：`snake_case`；模块内全局（static 文件作用域）加 `s_` 前缀；真全局加 `g_`；
  常量与宏全大写 + 下划线；禁拼音、禁无意义单字母（循环变量 `i/j/k` 除外）
- 函数：公共 API 统一 `cg_` 前缀 + `snake_case`（如 `cg_widget_set_text`）；
  文件内 static 函数省前缀、动词在后（`parse_frame`、`draw_ring`）
- 类型：`_t` 后缀（`cg_widget_t`、`cj_val_t`）；枚举值全大写、带模块前缀（`CG_K_*`、`CJ_STR`）
- 宏函数参数加括号；Win32 回调固定 `XxxProc` 命名（`MainProc`/`PanelProc`/`FocusProc`）

## §3 结构

- 头文件必须有 `#ifndef/#define/#endif` 保护；只放声明/类型/宏，实现进 `.c`
- 包含顺序：本项目头（`cg_internal.h`）→ 系统头（`<windows.h>`…）→ 标准库
- 函数不超过 100 行；超过先拆 static 助手，一个函数只做一件事
- 全局状态尽量收进 `cg_app_t`/`cg_win_state_t`；确需文件级状态用 `static s_xxx`
  并注释"谁在什么时机读写"

## §4 安全

- 指针解引用前判空；`free(p)` 后紧跟 `p = NULL`（本工程惯例，见 `cg_widget_free`）
- 数组访问先查边界（行号/列号/下标），越界要么静默兜底要么 stderr 报警，不许裸穿
- 可能失败的调用检查返回值；错误信息打到 stderr 且带 `[cstackgui]` 前缀
- 分配失败必须处理（返回 NULL/-1），不许假设内存无限

## §5 项目铁律（违反 = 高危）

1. 业务代码只 `#include "cstackgui.h"`，不碰 `cg_internal.h` 和 Windows 头
2. 控件类型表 `k_kinds`（cg_doc.c）与画布 `KINDS`（model.ts）必须同步；改格式跑 `tools\check-format.bat`，
   改类型表 / 事件掩码跑 `tools\run-tests.bat`（`tests\unit\test_kind.c` 会直接报哪一行不一致）
3. 事件按控件特性分配（`CG_EV_*` 位掩码），别给不需要交互的控件加事件
4. 原生控件按树序创建（父先于子），别遍历 `app->widgets` 链表建控件
5. 回调里改控件会触发原生通知再进回调——靠 `g_in_notify` 防重入，别删
6. `k_kinds` 按 kind 字段线性查，别用数组下标当枚举值
7. 控件状态字段带 `_dirty` 标记再交后端重建，否则用户选择会被反复重置
8. 静态字符串缓冲区尺寸保持一致，别用 `(void)` 糊弄 `-Wall` 告警

## 输出格式

【风险等级】规范/建议/低危
【位置】文件名:行号
【问题类型】缺注释/命名不规范/安全隐患/违反铁律
【错误代码片段】
【规范要求】（引用上面 § 编号）
【修复方案 + 正确示例】
