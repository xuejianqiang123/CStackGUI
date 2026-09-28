# 串口助手：无界面串口组件 + terminal 接收视图的样板

> 目录 `examples\serialterm\`　编译 `build-app.bat serialterm`　运行 `examples\serialterm\serialterm.exe`　自检 `serialterm.exe --selftest` → 控件 63 个 / 回调 31 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
一个"跟板子说话"的串口助手。串口在 `.cgui` 里是一个**无界面控件**（`type: "serial"`，画布上只是个图标，运行时不建窗口），数据到达派发 `arrive` 事件，**数据落到哪个控件全由回调代码决定**。功能上对标现场常用的 XCOM：16 进制显示、时间戳、定时发送、多条发送、发送文件、自动保存、收发计数都有。
反超点是**切帧**：用 `cg_frame_*` 的"帧间静默"把字节流切成整帧，每帧单独一行并报长度与 CRC16 结论 —— 通信调试里最想要的那一眼。
有意留的边界：不抄 XCOM 的厂商私有 IAP 协议；要什么协议就用 `cg_proto_build` / `cg_modbus_*` 自己组帧。

## 二、怎么用
1. 点「刷新」枚举本机串口 → 在 `cmb_port` 选端口、`cmb_baud` 选波特率，再按板子配数据位 / 停止位 / 校验（`cmb_bits` / `cmb_stop` / `cmb_parity`）→ 点「打开串口」。
2. 打开失败会在日志里给出中文原因（端口被占用 / 找不到 / 波特率不支持），不是干巴巴一句"失败了"。
3. 顶部开关：`chk_hexview` 16 进制显示、`chk_stamp` 时间戳、`chk_frame` 按帧显示、`chk_autosave` 自动保存。
4. 勾了「按帧显示」后，用 `cmb_fmode` 选定界方式：静默 / 换行符 / 定长 / 长度字段；`spn_arg` 的含意随之变（静默=毫秒、换行符=分隔字节、定长=每帧字节数、长度字段=长度字段在帧内的偏移）。
5. 单条发送：`edt_send` 输入内容，`chk_hex` 勾"HEX 发送"就写 `1A 2B 3C` 这种；`chk_newline` 发完追 `\r\n`；点「发送」或在输入框直接回车。
6. 多条发送：`edt_multi` 一行一条（`#` 开头是注释），点「依次发送」，勾 `chk_loop` 就一直轮；每条间隔看 `spn_multi`。
7. 定时发送：勾 `chk_timed`，按 `spn_cycle` 周期反复发上面那一行；点「停止发送」停掉它或停止多条轮发。
8. 「打开文件」把一个文本文件读进「多条发送」框；「发送文件」把文件内容原样发出。
9. 协议组帧区：填帧头 / 地址 / 功能码 / 数据 / 校验 / 帧尾，`lab_prev` 实时预览组出的帧；点「发送一帧」，或勾「周期发送」按周期发。
10. `cht_rate` 显示每秒收到的字节数；「保存日志」「清空」在顶部按钮排。
11. 命令行：`serialterm.exe --demo` 不开串口、把几批合成字节灌进接收管线并把帧 / CRC 结论打到 stdout；`serialterm.exe --demo=COM4` 真开这个口走一遍"选参数 → 打开 → 发 → 收"。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `ser_main` | serial | 无界面串口组件，数据到达发 `arrive` |
| `term_log` | terminal | 收发 / 程序消息日志，支持 hex、时间戳、按帧显示 |
| `cmb_port` / `cmb_baud` | combo | 端口与波特率 |
| `btn_connect` / `btn_refresh` | button | 打开串口 / 重新枚举 |
| `chk_frame` + `cmb_fmode` + `spn_arg` | checkbox/combo/edit | 按帧显示与其定界方式、参数 |
| `edt_send` / `chk_hex` / `btn_send` | edit/checkbox/button | 单条发送 |
| `edt_multi` / `btn_sendall` / `chk_loop` | edit/button/checkbox | 多条发送 |
| `grp_proto` 内各控件 | — | 协议组帧预览与发送 |
| `cht_rate` | chart | 每秒收到字节数 |

## 四、关键接口
- `cg_widget_serial` / `cg_widget_serial_open`（配 `set_baud` / `set_format`）：把下拉里选的参数写进串口组件再开。
- `cg_serial_read` / `cg_serial_write` / `cg_serial_available` / `cg_serial_list` / `cg_serial_last_error`：收发字节、枚举端口、取中文错误原因。
- `cg_frame_init` / `cg_frame_feed` / `cg_frame_take` / `cg_frame_tick`：把字节流切成帧（四种定界）。
- `cg_crc16`：对切出的帧算 CRC16 并给出通过 / 不符结论。
- `cg_term_write`：往 terminal 写 RX / TX / 系统消息，着色由控件管。
- `cg_chart_push` / `cg_chart_set_range`：速率曲线。
- `cg_proto_build`：按帧头 / 地址 / 功能码 / 数据 / 校验 / 帧尾组出整帧。
- `cg_app_has_window`：回调先问有没有窗口，自检才跑得干净。

## 五、容易踩的坑
- `cg_app_selftest` 会**合成触发每个回调**（包括 `arrive`），所以回调第一件事就要判串口组件是不是 NULL —— 自检模式下链路不存在，直接返回；开串口、弹对话框前也要先问 `cg_app_has_window`。
- 读线程只搬字节、**绝不碰控件**：控件只在建它的线程里碰，这是 Win32 硬约束。
- 发送时"HEX 还是原文""要不要加新行"的转换**只在 `prepare_send` 一处做**，单条 / 多条 / 文件 / 演示共用它，免得某条路径忘了加新行。

## 六、相关文档
- 专文：`docs\articles\08-案例-serialterm-无界面串口与编辑框.md`。
- `examples\教学使用说明.md` → 「七、实用工具详解 · serialterm」。
- `docs\CGUI-FORMAT.md` → 第 2.9 节（`terminal`）、第 4 节（控件类型表）。
