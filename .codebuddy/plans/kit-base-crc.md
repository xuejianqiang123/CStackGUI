# 实现计划（简版）：cg_kit 进制/补码 与 校验（CRC）

> 上一片（`kit-encoding.md`）我把"进制互转"砍掉了，理由是"仓库里零证据"。
> **这个判断是错的** —— 用户是**工控/嵌入式向**，二进制的位段（寄存器值）、十进制与十六进制
> 互看，本来就是这类工具每天的活。领域常识不该等我看到仓库里的用例才算数。
> 本片把它补上，并顺带做同一领域的校验。

## 1. 证据（从精易模块源码里挖出来的，不是我猜的）

- **`进制_` 14 条**：`二八十六到十 / 二到十 / 八到十 / 十六到十 / 十到二 / 十到八 / 十到十六 /
  十到三十六 / 三十六到十 / 十六进制转有符号的十进制 / 有符号的十进制转十六进制 / 十六到十_汇编`。
  → 关键不是"进制换算"，而是最后两条：**补码解释**（报文给 `0xFFF6`、协议说 12 位有符号 ⇒ -10）。
- **`校验_` 12 条**：`取crc32 / 取md5 / 取md5_文本 / 取md5_进度 / 取md4 / 取md2 / 取sha1 /
  取sha256 / 取sha512 / 取hmac_md5 / 取rc4 / 哈希值计算`。
  → 有 **CRC32**；**没有 CRC16**，而工控设备协议帧（Modbus RTU）几乎都是 CRC16 —— 这条按用户领域补。
- `运算_` 里有 `大小端转换`；`字节集_` 有 `反转 / 取左边 / 取右边 / 找子` —— 本轮不做
  （字节序在 Win32/`winsock` 里已有 htons/ntohs 那一套，且仓库里零用例）。

## 2. 契约

```c
int       cg_int_to_base(long long v, int base, char *out, int out_size);   /* 2..36，大写 */
int       cg_base_to_int(const char *s, int base, long long *out);          /* 1/0 */
int       cg_base_convert(const char *s, int from_base, int to_base, char *out, int out_size);
long long         cg_to_signed(unsigned long long v, int bits);   /* 低 bits 位按补码读 */
unsigned long long cg_from_signed(long long v, int bits);         /* 写成 bits 位补码 */
unsigned cg_crc32(const void *data, int len);   /* IEEE 802.3 / zlib（初值全 1，末尾取反） */
unsigned cg_crc16(const void *data, int len);   /* CRC16/Modbus（初值 0xFFFF，不取反） */
```

几条刻意的决定：

- **输出大写**：数据手册、寄存器表都大写；要小写十六进制用 `cg_hex_encode()`（它本来就有 `upper`）。
- **`cg_base_to_int` 是"整串必须合法"**：认 `-/+` 与 `0x`/`0b`/`0o` 前缀（调试工具里到处是这种写法），
  允许首尾空白，但**中间有空白即失败** —— `"1A 2B"` 是两段，拼成一段是静默错读。
  溢出也算失败。返回 1/0（与 `cg_accel_parse` 同一风格），而不是靠特殊返回值。
- **补码按位宽**：`bits` 是必须的参数，因为"0xFFF6 是多少"离开位宽没有答案
  （12 位是 -10，16 位是 65526）。测试里两种位宽都钉了。
- **CRC 逐位算、不建表**：没有静态状态 ⇒ **天然线程安全**（串口/网口读线程随时可能调它），
  也不用操心"表初始化了没"。慢约 8 倍，对帧与几 MB 文件无感；上百 MB 的大文件不划算，已写进头文件。
- **哈希不在本片**：MD5/SHA1/SHA256/HMAC 要么手写几百行、要么新增链接依赖（CryptoAPI/BCrypt），
  是独立决策，单独一片。

## 3. 落地

| # | 文件 | 改什么 |
|---|---|---|
| 1 | `runtime/include/cstackgui.h` | 7 个声明 + 「进制转换与补码」「校验（CRC）」两节注释 |
| 2 | `runtime/src/kit/cg_kit_str.c` | 进制 + 补码（与编码同一族） |
| 3 | `runtime/src/kit/cg_kit_check.c`（新） | CRC32 / CRC16 —— 校验的独立落点，将来哈希也进这里 |
| 4 | `tests/unit/test_kit_str.c` | 新套件「进制转换与补码」 |
| 5 | `tests/unit/test_kit_check.c`（新） | 新套件 CRC32 / CRC16 —— 只用公开检验值 + 一条性质测 |
| 6 | `designer/src/supportlib.ts` | 画布「支持库」词条 |
| 7 | `docs/CGUI-FORMAT.md`、`README.md`、`docs/ARCHITECTURE.md` | API 速查 / 能力段 / kit 文件清单（新文件必须列，`check-layers.mjs` 第 6 条会红） |

## 4. 验证

- 公开检验值：CRC-32/ISO-HDLC("123456789") = `0xCBF43926`、CRC-16/MODBUS("123456789") = `0x4B37`；
  CRC32("a") = `0xE8B7BE43`。
- **性质测**（比背下来的帧 CRC 可靠）：把算出的 CRC 按**低字节在前**接回帧尾，
  整帧再算一遍必须 = 0 —— 这是现场验帧的习惯做法，同时证明"低字节在前"没写反。
- 进制侧钉住失败路径：空串 / 只有符号 / 只有前缀 / 非法字符 / 数字超出该进制 / 中间空白 / 溢出。
- 门禁：`build-all.bat`（新 `.c` 会被自动编进去）、`run-tests.bat --memcheck`
  （逐套件零新增存活堆块）、`check-format.bat`（分层契约要认出新文件）、画布 `tsc` + `vite`。

## 5. 完成记录（2026-09-27）

- [x] 落地清单 1~7
- [x] 门禁全绿（**实际输出**）：`build-all.bat` 10 个示例逐个 `--selftest` 通过；
      `tools\run-tests.bat --memcheck` **81 套件 / 2358 断言 / 0 失败**、逐套件"没有新增存活堆块"；
      `tools\check-format.bat` → `[ok] format contract check passed`（支持库 **228 条**、
      基础能力从 8 类增到 **9 类**；分层契约 **196 项 / 47 个 `.c` 基名唯一** ——
      新文件 `cg_kit_check.c` 已被 ARCHITECTURE 清单列出并通过 check-layers 第 6 条）；
      画布 `tsc --noEmit` + `vite build` 通过
- [x] **公开检验值实测通过**：CRC32("123456789") = `0xCBF43926`、CRC16/MODBUS = `0x4B37`、
      CRC32("a") = `0xE8B7BE43`；Modbus 的"含 CRC 的整帧再算一遍 = 0"性质也成立
- [ ] 提交：按 `git-redlines` 口径**未提交**（本片与上一片 `kit-encoding` 都还在工作区）

### 实施中被断言当场抓出的两处错（都是**我的测试**写错，不是实现）

1. `0xFFF6` 按 **16 位**读同样是 **-10**（bit15 是 1）—— 我原本拿无符号的 65526 当期望值，
   那句"位宽不同结论不同"的**例子本身是错的**。换成干净的对照：`0xF` 按 4 位 = -1、按 8 位 = 15。
   这恰好说明 `bits` 参数不能省：`0xFFF6` 在 12 位和 16 位下都是 -10，但 `0xF` 换了位宽结论就变 ——
   "看起来显然"的断言最该用公开向量钉死。
2. 想断言"两个 CRC 的低 16 位**不同**"，却写成用 `cg_test_int` 比**相等**。
   教训是 `cg_test_int(a, b)` 是**相等**断言，比"不等"必须显式写 `!=`，
   否则读代码的人会以为在验证相等。

> 本节原先按预期填了"80 套件"，实际是 81 —— 已按实际结果改正。
> 写计划时**先**把"验证结果"填上，是这套流程里最该避免的动作（结果得先跑出来）。

## 6. 没验到的

- 进制/CRC 都只到"单测 + 公开检验值"这一层，**没有在真机示例的界面上用过**（比如 serialterm
  里算一帧 Modbus CRC 显示出来）—— 要截图级证据得改示例，本轮没做。
- `cg_base_to_int` 对 `-0x8000000000000000`（LLONG_MIN 那种）只做了边界拒绝，没有专门构造用例。
