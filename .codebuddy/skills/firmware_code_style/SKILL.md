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

## §6 皮肤编辑框（cg_skin_edit.c）渲染铁律

`cg_skin_edit.c` 用子类在 EDIT 上自绘圆角边框 + 单行文字垂直居中，踩过的像素级规矩，改这块必须遵守：

1. **每次重画先把整条边框带（含让出的上边距）填成底色，再画线** —— 否则细线样式只覆盖带内一个像素，上一次聚焦画的 2px 紫环会残留（"点亮一次后边框痕迹不消失"）。
2. **GDI 笔宽与路径坐标配套**：2px 笔路径给 `bw/2=1`（正好压住 `[0,2)`）；1px 笔必须配 1px 的边框带且路径给 `0`（给 `1` 或 `0.5` 会画成半像素线→断线/残缺）；镜框式用 `bw=3`、两圈线分别给 `0` 和 `2`（中间留 1px 底色缝）。
3. **量文字行高必须先 `WM_GETFONT` 把控件自己的字体选进 `GetDC` 的 DC 再 `GetTextMetrics`** —— DC 默认选的是 16px System 位图字体，量小了 pad 算大、文字整条下沉（雅黑 10pt 实际行高约 19px），且 `WM_SETFONT` 里"字高变了才重排"的比较会恒为不变、换字号不重排。
4. **调节器（spin）不能用 `UDS_ALIGNRIGHT`**：它按 buddy 的窗口矩形贴右，会压在自绘边框和圆角上；改为创建时不带该样式，由 `cg_skin_edit_fit_spin()` 按客户区内侧摆位（右让 bw、上下各让 bw），并在 `WM_SIZE`/`WM_SETFONT` 里重摆 + 设右内边距。聚焦环优先级高于边框样式（无边框也要有焦点反馈）。
5. **边框/焦点环用 `GetWindowDC` 画在客户区之外**（客户区四边由 `WM_NCCALCSIZE` 四边内缩 `bw`），控件自身重绘（选中/滚动/光标闪烁）盖不掉 —— 这是设计意图，**别把边框画进内容区**。
6. **边框只在非客户区消息里重画，别挂进 `WM_PAINT`**：`sk_edit_box`（画边框）曾挂在 `WM_PAINT` 分支里，客户区重画时用 `GetWindowDC` 画非客户区边框——流式灌入滚动时 DC 带滚动偏移，边框被画进内容区，留下一条皮肤灰（`B8B2BD`）横贯实心线（2026-09-25 修，回归在 test_skin_edit.c）。正确做法：边框只响应 `WM_NCPAINT`/`WM_SETFOCUS`/`WM_KILLFOCUS`；`WM_PAINT` 只走 `DefSubclassProc` 让 EDIT 画客户区。

## §7 GDI 渲染 bug 的像素级复现方法（auto_invoke：改 cg_skin_edit.c 或怀疑"渲染 bug"时）

别靠肉眼猜，也别空谈机制——在 `tests/unit/test_skin_edit.c` 里用真窗口 + `GetPixel` 把渲染结果量化：

- **量化思路**：建一个真 EDIT 挂上 `cg_skin_apply_edit`，`ShowWindow` + `UpdateWindow` 后用 `GetDC`/`GetPixel` 逐行扫客户区，统计"非白像素横贯整行"的行。正常文本行字间有空隙，覆盖率到不了 ~80%；实心规则线能到 ~100%，一眼可分。
- **复现三要素（缺一个可能不重现）**：① 挂皮肤 vs 不挂（对照组）② 单层弹窗 vs 子窗口（贴近主窗口上下文）③ **单次灌入 vs 反复灌入 + 滚动**（`cg_edit_scroll_end`），真实串口助手是第三种。本次的线条 bug 只在 ③ + 挂皮肤时出现。
- **扫描很贵，且对皮肤控件逐像素 `GetPixel` 是灾难**：一轮扫满屏约 30 万次 `GetPixel`；更要命的是对挂了皮肤的窗口逐像素读会**触发皮肤反复重画**（30 万次 × 重画），CPU 跑满、窗口"无响应"，看起来像死循环，其实只是巨慢。**正确做法：先 `CreateCompatibleDC`+`CreateCompatibleBitmap`，`BitBlt` 把客户区抓到离屏位图一次，再对位图逐像素扫**——飞快且不惊动控件。别在每一轮都扫，每 N 轮扫一次（N=5 就够抓到）。
- **定性来源看颜色**：扫到实心线后把该行平均颜色打出来（如 `RGB=B8B2BD`），和皮肤调色板比对——`SK_EDGE_GREY=B8B2BD` 一对上就知道是皮肤画的，能省掉大量瞎猜。
- **测试框架 stdout 设无缓冲**（`cg_test_main.c` 里 `setvbuf(stdout, NULL, _IONBF, 0)`）：否则重定向到文件是 4KB 块缓冲，卡死/被杀时缓冲丢失，永远看不到真实进度。
- **排查坑**：进程被命令超时杀掉会给出伪退出码 `0xCFFFFFFF`（看着像崩溃，其实不是 AV，是被杀了）；`run-tests.bat` 会 `cd` 到 `build/tests` 再跑 exe，测试里写相对路径会偏移，要落盘诊断文件就用绝对路径。
- **跑全量单测**用 `tools/run-tests.bat --no-kit`；中文 `--grep` 参数会因控制台编码被搅，改在测试里打 ASCII tag（如 `"round 25"`）再筛。

## 输出格式

【风险等级】规范/建议/低危
【位置】文件名:行号
【问题类型】缺注释/命名不规范/安全隐患/违反铁律
【错误代码片段】
【规范要求】（引用上面 § 编号）
【修复方案 + 正确示例】
