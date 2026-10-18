> **本文档是合并前独立工程 `examples\framedec\` 的原文**（2026-10-18 该工程已并入
> [`examples\suites\comm\`](../comm)：本工具的完整逻辑在 `modules\fd.c`，
> 编译 `build-app.bat suites\comm`，五个工具各占一个页签）。
>
> 文中「目录 / 编译 / 运行 / 自检」那几行命令是**合并前的口径，现已失效**（原工程目录已删）；
> 其余内容 —— 它演示什么、关键接口、容易踩的坑 —— 仍然逐条成立，对应 `modules\fd.c`。
# 帧协议解析器：自己定一种帧、自己切帧、三种校验

> 目录 `examples\framedec\`　编译 `build-app.bat framedec`　运行 `examples\framedec\framedec.exe`　自检 `framedec.exe --selftest examples\framedec\framedec.cgui` → 控件 19 个 / 回调 8 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
`examples\modbus` 只讲 Modbus 这一种既有协议，「自己定一种帧、自己切帧与校验」这条路本示例来补。帧格式简单到一眼能懂：

```
[0xAA 0x55][长度 L][命令 cmd][负载 N 字节][校验 1 字节]     L = 校验前的字节数 = 4 + N，整帧 = L + 1
```

校验三选一（累加和低 8 位 / XOR / CRC16-Modbus），字节序两种可切（小端 / 大端）。数据有三种合成来源：粘贴十六进制、「模拟一帧」、「模拟连发」（每 300ms 造一批，**故意夹带噪声与断帧**）。

## 二、怎么用
1. 在十六进制输入框 `edt_hex` 里粘贴一帧（预填 `AA 55 06 01 0A 0B 1B`），或用「模拟一帧」按当前校验 / 命令 / 负载拼一帧填回输入框（只填不发）。
2. 点「喂入输入的十六进制」：解析成字节喂进切帧器（空格 / 换行 / 逗号都当分隔符）。
3. 合法帧进帧表 `tbl_frames`（含解码结果）；校验错的帧也进表，结论列写清算出多少、帧里多少。
4. 点「模拟连发」用定时器每 300ms 造一批（噪声 + 一帧合法帧 + 一段断帧），演示切帧健壮性。
5. 点「清空 / 重置」丢掉半帧、清空统计 / 帧表 / 日志；中间分隔条调左右宽度。
6. 命令行：`framedec.exe`（自动找同名 `.cgui`）、`framedec.exe xxx.cgui`、`--selftest`。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `spn_cmd` | edit（spin） | 命令字节（01 温度 / 02 量程 / 03 电压组） |
| `edt_pay` | edit | 负载 HEX（如 `0A 0B`） |
| `cmb_sum` | combo | 校验类型（累加和 / XOR / CRC16-Modbus） |
| `cmb_end` | combo | 字节序（小端 / 大端） |
| `btn_mock` / `btn_burst` | button | 模拟一帧 / 模拟连发 |
| `btn_feed` / `btn_reset` | button | 喂入输入的十六进制 / 清空重置 |
| `edt_hex` | edit | 十六进制输入 |
| `tbl_frames` | table | 切出的帧表（含解码结论） |
| `term_log` | terminal | 日志 |
| `sb_main` | statusbar | 收字节 / 成帧 / 校验错 / 残帧 / 丢字节 |

## 四、关键接口
`cg_crc16` 算 CRC16-Modbus；`cg_hex_decode` / `cg_hex_encode` 互转；`cg_bytes_get_u16` 取 16 位、`cg_to_signed` 转有符号；`cg_set_timer` / `cg_kill_timer` 管连发；`cg_table_add_column` / `cg_table_add_row` / `cg_table_row_count` / `cg_table_clear` 建表与限行；`cg_term_write` 写日志。

## 五、容易踩的坑
- 框架的通用切帧器与本帧布局不匹配（它的「长度字段」模式按「总长 = 长度值 + 附加量」解释，表达不了本帧「长度字节在命令之前、整帧 = 长度 + 1」），所以这里**手写切帧**，原因写在 `drain_stream` 的注释里。
- 校验错的帧也进表、结论列写清「算出多少 / 帧里多少」，不静默跳过 —— 这是通信调试最想要的那一眼。
- 连发定时器只在真窗口下能开（自检合成点击时若启动定时器，CI 会空转）。
- 帧表最多留 200 行，超了清空重来。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · framedec」。
- `docs\CGUI-FORMAT.md` → `edit`（`spin`）/ `combo` / `table` 的字段与事件。
