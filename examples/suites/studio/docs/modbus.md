> **本文档是合并前独立工程 `examples\modbus\` 的原文**（2026-10-18 该工程已并入
> [`examples\suites\studio\`](../)：本工具的完整逻辑在 `modules\mb.c`，
> 编译 `build-app.bat suites\studio`，16 个工具各占一个页签）。
>
> 文中「目录 / 编译 / 运行 / 自检」那几行命令是**合并前的口径，现已失效**（原工程目录已删）；
> 其余内容 —— 它演示什么、关键接口、容易踩的坑 —— 仍然逐条成立，对应 `modules\mb.c`。
# Modbus 主站调试：拼帧 → 发送 → 切帧 → 解帧 → 落界面

> 目录 `examples\modbus\`　编译 `build-app.bat modbus`　运行 `examples\modbus\modbus.exe`　自检 `modbus.exe --selftest` → 控件 22 个 / 回调 4 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
工控上位机的主循环：**拼一帧 → 发出去 → 把回来的字节按协议切成一帧帧 → 解帧 → 落到界面**。示例连的是 **Modbus/TCP** 从站：先用 `cg_net` 连上，再用 `cg_modbus_build_read` 拼读请求、按 MBAP 头里的长度字段切帧后交给 `cg_modbus_parse` 解回应，结果填进寄存器表，完整报文留在终端里。
配套的 `mock-slave.mjs` 是一个够用的模拟从站，本机就能把整条链真跑一遍，不需要硬件。有意留的边界：**框架不给"会持状态的主站"** —— 轮询节奏 / 超时 / 重试都归 `main.c` 自己管。

## 二、怎么用
1. 先另开一个控制台跑模拟从站：`node examples/modbus/mock-slave.mjs 1502`（默认 1502；标准端口 502 需要管理员权限）。
2. 在界面上填地址（本机自测 `127.0.0.1`）、端口（默认 1502）、从站号（1..247）→ 点「连接」，`led_link` 亮起。
3. 选功能码 `cmb_func`：03 读保持寄存器 / 04 读输入寄存器 / 01 读线圈；填起始地址（按协议从 0 算）与数量（一次最多 125 个寄存器）→ 点「读一次」。
4. 读回来的寄存器填进 `tbl_regs`；完整收发报文以 HEX 写进 `term_log`；状态栏显示成功 / 失败 / 等待毫秒。
5. 勾 `chk_auto`「自动轮询」按 `spn_iv` 间隔反复读（间隔别太短，工控现场一般 200ms~2s）。
6. 命令行加 `--demo`（或设环境变量 `CG_MODBUS_DEMO=1`）时，启动就自动连本机模拟从站并开始轮询。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `edt_host` / `spn_port` | edit | 从站地址 / 端口 |
| `spn_slave` | edit | 从站号（单元标识） |
| `btn_conn` / `btn_read` | button | 连接 / 读一次 |
| `cmb_func` | combo | 03 / 04 / 01 三种读 |
| `spn_addr` / `spn_count` | edit | 起始地址 / 寄存器个数 |
| `chk_auto` / `spn_iv` | checkbox / edit | 自动轮询与其间隔 |
| `tbl_regs` | table | 最近一次读回的寄存器表 |
| `term_log` | terminal | 完整报文（按方向着色） |
| `led_link` / `sb_main` | led / statusbar | 连接灯 / 成功·失败·等待计数 |

## 四、关键接口
- `cg_modbus_build_read`：按功能码 / 从站号 / 起始 / 个数拼出一帧读请求（事务号自己加一，用来对回应）。
- `cg_modbus_parse`：解回应，已替你剥掉异常响应的 `功能码 | 0x80`，给出原功能码 + 异常码。
- `cg_frame_init` / `cg_frame_feed` / `cg_frame_take` / `cg_frame_reset`：按长度字段（MBAP）把字节流切成一帧帧。
- `cg_net_connect` / `cg_net_read` / `cg_net_write` / `cg_net_connected` / `cg_net_close`：连接与收发。
- `cg_hex_encode`：把字节编成 HEX 文本再写进终端，日志存盘 / 复制出来能直接对协议手册看。
- `cg_set_timer`：主循环时钟（收数据与超时判断都挂在这一拍上）。

## 五、容易踩的坑
- 切帧不能假设"读一次 = 一帧"：TCP 是字节流，一次 `cg_net_read` 可能拿到半帧或一帧半，靠 `cg_frame_*` 攒。（串口上的 Modbus/RTU 没有长度字段，同一套 API 换成帧间静默模式即可。）
- 等回应期间不再发下一次，超过 `REQ_TIMEOUT_MS` 没收到就报超时（这是 Modbus 主站一问一答的事务概念）。
- 从站的异常响应（`功能码 | 0x80` + 异常码）是协议的一部分，**要当正常结果处理**，不是"通信坏了"。
- 定时器回调**必须自己重挂**（`cg_set_timer` 是一次性的）。
- 自检会合成触发每个回调，凡真连网的地方先问 `cg_app_has_window`，否则 CI 会卡在真连接上。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · modbus」。
- `docs\CGUI-FORMAT.md` → 第 2.9 节（`terminal`）、第 2.6 节（表格 `columns`）、第 4 节（控件类型表）。
