# 实现计划：串口组件（无界面控件 serial）

## 目标
- 新增一种**无界面控件** `serial`（串口组件，仿易语言）：画布上只是图标、运行时不建原生窗口；带属性（端口 / 波特率 / 数据位 / 停止位 / 校验 / 自动启动）与事件 `arrive`（数据到达）；数据到达时运行时在**主线程**派发事件，用户在回调里自己决定把数据写到哪个控件（terminal / listbox / chart / label…）。
- kit 层 `cg_serial` 扩展 `cg_serial_open_ex()` 支持数据位 / 停止位 / 校验（现有 8N1 的 `cg_serial_open` 保留为包装）。
- serialterm 示例改造为新用法的样板：串口组件发 `arrive` → 示例回调把数据**同时**路由到 terminal（收发日志）和 chart（速率曲线），体现"用户决定数据去哪"。

## 前置依赖
- 无 PRD/SDD；需求来自用户 2026-09-25 口述（仿易语言串口组件，数据落点由用户代码决定）。
- 关键先例（已探明，直接继承，不重新发明）：
  - 无矩形控件全链路样板 = `timer`：`cg_doc_kind.c:70`（0×0、事件位掩码）→ `cg_doc.c:427/:688`（字段解析/写回）→ `cg_win_create.c:44-48`（不建窗）→ `cg_win_main.c:254`（心跳派发）。
  - 事件载荷通道 = 「派发前写控件 ev_* 字段 + getter」：`cg_event.c:76-86`、`cg_win_event.c:226`。
  - 跨线程：不建新机制。读线程照旧只压环形缓冲（`cg_serial.c:142`），主线程在 50ms 心跳（`cg_win_main.c:254` WM_TIMER）里轮询 `cg_serial_available()` 派发。
  - 最近一次新增控件 = led/meter，触点清单已核（README「新增控件要动 7~8 处」）。

## 实现步骤（按序）

### 步骤 1：kit 层 —— cg_serial_open_ex
- 改动文件：`runtime/src/kit/cg_serial.c`、`runtime/include/cstackgui.h`
- 改动内容：
  - `cg_serial_open_ex(port, baud, bits, stop, parity)`：bits∈5..8（缺省 8）、stop∈1..2（缺省 1）、parity 0=无/1=奇/2=偶（缺省 0）；参数不合法时**干净失败**并给中文原因（沿用 g_open_err 机制）；`cg_serial_open(port, baud)` 变成 `open_ex(port, baud, 8, 1, 0)` 的包装。
  - parity 映射到 DCB：0→NOPARITY、1→ODDPARITY、2→EVENPARITY；奇偶校验开启时 `fParity = TRUE`。
- 验证方法：`tests/unit/test_serial.c` 增套件——参数校验（bits=0/9、stop=0/3、parity=3 都干净失败 + 有原因）、`cg_serial_open` 等价 `open_ex(…,8,1,0)`（用文件句柄路径测不了 DCB，重点测参数校验与不存在的设备名）。

### 步骤 2：core 层 —— kind 与文档字段
- 改动文件：`runtime/src/core/cg_doc.h`、`runtime/src/core/cg_doc_kind.c`、`runtime/src/core/cg_doc.c`
- 改动内容：
  - `CG_K_SERIAL` 枚举；`k_kinds` 加 `{ "serial", CG_K_SERIAL, 0, 0, 0, "串口", CG_EV_ARRIVE }`（0×0 = 无默认矩形，与 timer 同型；事件位取下一个空闲 bit，命名 `CG_EV_ARRIVE`，事件名 `"arrive"`）。
  - cg_node 加字段：`char sport[64]`（端口名）、`int s_baud`（缺省 9600）、`int s_bits`（缺省 8）、`int s_stop`（缺省 1）、`int s_parity`（缺省 0）、`int s_auto`（自动启动，缺省 0）。命名避开现有 `style`/`spin` 撞名。
  - `cg_doc.c`：解析（对 `serial` 类型读 `port/baud/bits/stop/parity/auto`，缺省值对齐上面）与写回（非缺省才写，风格照 `period`）。
- 验证方法：`tests/unit/test_doc.c` 增断言——解析缺省值、显式值、非法值回退缺省、写回恒等；`tests/unit/test_kind.c` 的「枚举↔表项双向一致」「类型名不重复」自动覆盖新 kind，必须绿。

### 步骤 3：win32 后端 —— 不建窗、心跳派发、生命周期
- 改动文件：`runtime/src/win32/cg_win_create.c`、`runtime/src/win32/cg_win_main.c`、`runtime/src/win32/cg_win_ctl.c`、`runtime/include/cstackgui.h`（API 声明）
- 改动内容：
  - `cgw_create_native()`：`serial` 直接跳过建窗（照 timer）；`auto=1` 时在此处 `cg_serial_open_ex(...)` 并把句柄存 `w->native2`（打开失败只 `cg_log` 警告，不拦启动）。
  - 心跳派发：`WM_TIMER` 处理完 `cg_timers_tick(app)` 后，扫 `app->widgets` 里 type==serial 且 `native2` 非空的控件，`cg_serial_available()>0` 且绑定了 `arrive` 回调 → `cg_dispatch(w, CG_EVENT_ARRIVE)`。回调里用 `cg_widget_serial(w)` 取句柄再 `cg_serial_read`（复用「事件不带载荷、回调自己取」的既有模式）。
  - 生命周期：win32 侧控件销毁路径上对 serial 关句柄（`cg_serial_close` 幂等 NULL 安全）。**层约束：core 不得 include kit**（check-layers 会红），所以关闭动作必须放在 win32 后端的销毁/清理点，不在 `cg_widget_free`。
  - 新公开 API：`cg_serial_t *cg_widget_serial(cg_widget_t *w)`（类型守卫：非 serial 返回 NULL）；`int cg_widget_serial_open(cg_widget_t *w, const char *port)`（按控件属性打开，动态改端口用；返回 0 成功）与 `void cg_widget_serial_close(cg_widget_t *w)`。
- 验证方法：`tests/unit/test_kind.c` / 自检路径不建窗 → serial 控件在 `--selftest` 下绝不打开真实串口（合成触发 `arrive` 回调时 `cg_widget_serial(w)` 返回 NULL，回调必须 NULL 安全——写进示例并验证）；`tools/check-dispatch.mjs` 与 `tools/check-layers.mjs` 必须绿（分层是这次最容易踩的红线）。

### 步骤 4：designer TS 侧（契约先行：字段名/缺省值与步骤 2 逐字一致）
- 改动文件：`designer/src/model/kinds.ts`、`designer/src/model/events.ts`、`designer/src/model/types.ts`、`designer/src/model/parse.ts`、`designer/src/model/factory.ts`、`designer/src/components/WidgetView.tsx`、`designer/src/components/Inspector/TypeSection.tsx`、`designer/src/components/Inspector/HintSection.tsx`、`designer/src/styles.css`
- 改动内容：
  - kinds.ts：`{ type: "serial", label: "串口", group: "其它", container: false, w: 48, h: 48, text: "" }`（画布给个格子放图标；顺序与 k_kinds 一致）。
  - events.ts：`serial: ["arrive"]`（标签「数据到达」；必须与 k_kinds 位掩码一致，否则 check-format/单测双向一致会红）。
  - types.ts + parse.ts：`port?: string; baud?: number; bits?: number; stop?: number; parity?: number; auto?: boolean`，读入带缺省容错（照 period 的写法）。
  - factory.ts：种子 `port:"COM1", baud:9600, bits:8, stop:1, parity:0`。
  - WidgetView.tsx：serial 走专门 case，渲染 `.ctl-serial` 图标盒子（照 timer 先例，串口插头 glyph + 端口名小字）。
  - TypeSection.tsx：`node.type === "serial"` 分支——端口名输入、波特率下拉（9600/19200/38400/57600/115200…）、数据位/停止位/校验下拉、自动启动勾选。
  - HintSection.tsx：serial 加入「没有原生窗口」特判（照 timer/menu）。
  - styles.css：`.ctl-serial`、`.glyph-serial`（照 `.ctl-timer` :1266 的样子）。
- 验证方法：`npx tsc` 编过；`tools/check-format.bat` 的 roundtrip（新增 serial 控件的 .cgui 走 parse→serialize→C 自检恒等）。
- 回执契约（若并行交付）：字段名 `port/baud/bits/stop/parity/auto`、缺省 `COM1/9600/8/1/0/假`、事件名 `arrive`——与 C 侧逐字一致，改任何一边必须同步另一边。

### 步骤 5：serialterm 示例改造（「用户决定数据去哪」的样板）
- 改动文件：`examples/serialterm/serialterm.cgui`、`examples/serialterm/main.c`
- 改动内容：
  - .cgui 加无界面控件 `{ id: "ser_main", type: "serial" }`（auto=假；端口/波特率仍由界面下拉控制）。
  - main.c：`on_connect_click` 改走 `cg_widget_serial_open(ser_main, 所选端口)`（打开前先把波特率写回控件，用新增的属性 setter 或 open 参数）；删掉自管 `cg_serial_t*` 与 `cg_set_timer` 轮询链。
  - 新回调 `on_serial_arrive`：`cg_serial_read(cg_widget_serial(w), buf, cap)` → `cg_term_write(term_log, CG_TERM_RX, …)`（收发日志）**并且**累计进 `rx_window`（速率曲线）——一个数据源路由两个控件，正是"用户决定去哪"的演示。句柄 NULL（自检模式）直接 return。
  - 发送/断开/退出路径改用 `cg_widget_serial_*`。
- 验证方法：`serialterm.exe --selftest` 通过（自检合成触发 arrive，句柄 NULL 不崩不卡）；真机打开 COM4 收发正常（交付时请用户实测）。

### 步骤 6：文档与门禁
- 改动文件：`docs/CGUI-FORMAT.md`（新 kind + 字段契约）、README 新增控件清单核对
- 验证方法（全量门禁）：`tools/run-tests.bat`（单测 + kit 冒烟）、`tools/check-format.bat`（roundtrip + 分层 + dispatch 契约）、`build-app.bat serialterm` + 自检。

## 验证清单（Validation-First 核心）
- [ ] 编译通过且无新增警告（runtime / designer tsc / serialterm）
- [ ] test_serial：open_ex 参数校验干净失败 + 中文原因
- [ ] test_doc / test_kind：serial 字段解析、缺省回退、写回恒等、枚举双向一致
- [ ] 行为验证：serialterm 用串口组件连接 COM4，数据到达事件把字节同时写进 terminal 与 chart（真机，交付时用户实测）
- [ ] 边界/异常：未绑 arrive 回调时不派发；未打开（native2=NULL）时心跳跳过；自检模式绝不碰真实串口；打开失败（端口被占/不存在）只记日志不拦启动；控件销毁后句柄不泄漏（--memcheck 过）
- [ ] 分层契约绿（core 不依赖 kit）；check-dispatch 14 条规则不红

## 风险与排查路径
- 风险：core 层新增字段/枚举动了 `cg_doc.h` 布局 → 全库重编，可能有遗漏的类型守卫分支。排查：全量 grep `CG_K_TIMER` 的所有 switch/strcmp 触点，逐一确认 serial 需不需要分支。
- 风险：k_kinds 位掩码与 designer events.ts 漂移。排查：test_kind 双向一致断言 + check-format；红了对着两边表逐位对。
- 风险：心跳扫 widgets 的性能（每 50ms 全扫）。排查：控件数通常 <100 且只对 type==serial 做 strcmp 前置过滤；若列表大再加 app 级 serial 控件缓存链（暂不做，ponytail）。
- 风险：`w->native2` 被别处复用（edit 的 spin 也用它）。排查：serial 类型的 native2 无冲突（不同类型不同语义是既有惯例），grep 确认没有按类型无关读 native2 的代码。
- 风险：roundtrip 门禁撞上陈旧 build\modeljs（2026-09-25 踩过）。排查：门禁红了先删 build\modeljs 重编 model 再判断。
