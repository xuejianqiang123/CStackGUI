> **本文档是合并前独立工程 `examples\hextool\` 的原文**（2026-10-18 该工程已并入
> [`examples\suites\dev\`](../dev)：本工具的完整逻辑在 `modules\ht.c`，
> 编译 `build-app.bat suites\dev`，五个工具各占一个页签）。
>
> 文中「目录 / 编译 / 运行 / 自检」那几行命令是**合并前的口径，现已失效**（原工程目录已删）；
> 其余内容 —— 它演示什么、关键接口、容易踩的坑 —— 仍然逐条成立，对应 `modules\ht.c`。
# 进制与校验工具箱：把一段输入当文本或十六进制解析

> 目录 `examples\hextool\`　编译 `build-app.bat hextool`　运行 `examples\hextool\hextool.exe`　自检 `hextool.exe --selftest examples\hextool\hextool.cgui` → 控件 18 个 / 回调 11 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
面向工控 / 嵌入式调试的**纯计算**工具，不碰任何硬件 —— 同一段输入可按文本或 HEX 两种看法，产出字节视图、校验、编码与整帧报文预览：字节区（字节数 / HEX 串 / 可打印 ASCII / u16·u32 大小端）、校验区（ADD8 / XOR8 / CRC16-Modbus / CRC32 / MD5 / SHA1 / SHA256）、编码区（Base64 / URL / HEX 与文本互转）、报文区（帧头 + 地址 + 功能码 + 长度 + 载荷 + CRC16 + 帧尾）。
补的能力面是「hex/文本互转 + 字节面板 + 校验」。

## 二、怎么用
1. 在左侧多行框 `edt_input` 里贴输入；勾或不勾「按 HEX 解析」决定按十六进制还是文本取字节（HEX 支持空格 / 逗号 / 连字符分隔，如 `48 65 6C 6C 6F`）。
2. 点「解析字节」：右侧终端输出字节视图与全部校验；状态栏报字节数与解析模式。
3. 编码区按钮：`Base64 编码` / `Base64 解码` / `HEX → 文本` / `URL 编码` / `URL 解码` / `文本 → HEX`。
4. 点「组装协议帧并预览（HEX）」：走 `cg_proto_build` 把整帧拼出来预览（校验覆盖「校验值之前的全部字节」）。
5. 结果逐行写进右侧终端，可选中复制；拖动中间分隔条调左右宽度。
6. 命令行：`hextool.exe`（自动找同名 `.cgui`）、`hextool.exe xxx.cgui`、`--selftest`。编辑框在就绪钩子里预填样例，所以自检能把每条路径都跑到。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `edt_input` | edit | 输入（HEX 或文本） |
| `chk_hex` | checkbox | 按 HEX 解析（不勾按文本） |
| `btn_parse` | button | 字节视图 + 全部校验 |
| `btn_b64_enc` / `btn_b64_dec` | button | Base64 编 / 解码 |
| `btn_hex2txt` / `btn_txt2hex` | button | HEX ↔ 文本 |
| `btn_url_enc` / `btn_url_dec` | button | URL 编 / 解码 |
| `btn_frame` | button | 组装协议帧并预览 |
| `term_out` | terminal | 逐行结果输出 |
| `sb_main` | statusbar | 最近一次操作 / 字节数与模式 |

## 四、关键接口
`cg_hex_decode` / `cg_hex_encode` 十六进制与字节互转；`cg_bytes_get_u16` / `cg_bytes_get_u32` 大小端解释；`cg_crc16` / `cg_crc32` 校验；`cg_hash`（`"md5"` / `"sha1"` / `"sha256"`）摘要；`cg_base64_encode` / `cg_base64_decode`；`cg_url_encode` / `cg_url_decode`；`cg_proto_build` 拼整帧；`cg_term_write` 输出。

## 五、容易踩的坑
- 解析出的字节有上限（`HX_BYTES`），超了明确提示，不会悄悄截。
- `cg_proto_build` 的长度字节最大 255，载荷超过时只取前 255 个并注明。
- 换算是纯计算、不接硬件也不弹窗；编辑框在就绪钩子里预填样例，自检全程不阻塞。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · hextool」。
- `docs\CGUI-FORMAT.md` → `edit` / `checkbox` / `terminal` 的字段与事件。
- `docs\articles\09-校验与摘要.md` → CRC / 哈希 / 进制与补码的接口语义。
