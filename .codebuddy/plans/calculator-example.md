# calculator 例程实现计划

- 日期：2026-10-12
- 功能名：calculator-example
- 触发：用户给出界面截图（"计算器"），要求按图实现 examples/ 下的例程

## 目标

新增 `examples/calculator/`（`calculator.cgui` + `main.c`），实现截图中的六个功能区，
全部为真实可用的纯计算逻辑，遵守工程示例惯例（单 main.c + cgui、CG_BIND 绑定、
on_ready 预填样例、--selftest 全路径不弹窗不写盘）。

## 界面分区（按截图）

| 分区 | 控件 | 行为 |
| --- | --- | --- |
| 计算 | edt_expr + btn_calc → edt_result；btn_formula | 自研递归下降表达式求值；公式按钮把语法说明写进结果框（不弹窗） |
| 时间戳 | btn_gen_ts / btn_ts2utc / btn_utc2ts + edt_ts / edt_utc | 生成当前 unix 秒并 `cg_clip_set_text` 复制（有窗才复制）；时间戳↔"YYYY-MM-DD HH:MM:SS" 互转 |
| 进制转换 | 两组 radio（原/被，2/8/10/16）+ edt_conv + btn_neg + edt_conv_res + btn_neg2 | rad 变化/edit 变化即转换；"取反"对当前值 64 位按位取反后再转 |
| 长度计算 | edt_len_body（多行）+ edt_len_out + btn_len | 字节数（UTF-8）/ 字符数 / 行数写进输出框 |
| 字符串转换 | edt_str + btn_str2hex；edt_hex + btn_hex2str；chk_sp + edt_sp + btn_sp | 原地转换（输入框内容被结果替换，便于连锁操作）；第三组按复选框给 HEX 加/去空格 |
| 位运算 | edt_bits + btn_bits → edt_bits_res | 解析 `N'VALUE` 逗号列表，首段为最高位拼接，输出 HEX + 按段分组的二进制 |

状态栏 `sb_main`：就绪 + 最近一次操作描述。

## 设计决策

1. **表达式计算器自研**（不引第三方库）：递归下降，double 求值，位运算（`& | ^ ~ << >>`）
   操作数转 `long long`；递归深度上限 16、表达式长上限 512、除零/未知符号/括号不配平
   全部走友好错误串。函数：sqrt/ln/log/log2/exp/sin/cos/tan/asin/acos/atan/abs/floor/ceil/
   round/min/max/pow；常量 pi/e；支持 0x/0b 前缀。**边界全部静态定长缓冲 + snprintf**。
2. **UTC→时间戳**手写 civil_from_days 逆运算（标准算法，无 timegm 依赖）；
   时间戳→UTC 用 gmtime/strftime（标准 C 头即可）。
3. **进制转换**：`strtoull`（base 2/8/10/16）解析；输出按目标进制 `snprintf`；
   取反 = `~v`（64 位无符号）。radio 互斥由 runtime 保证（用户点击时），
   程序侧只在 ready 时给默认勾选写死。
4. **字符串转换原地替换**：与截图中"一个输入框配一个按钮"的布局一致；
   STR→HEX 走 `cg_hex_encode`，HEX→STR 走 `cg_hex_decode`；加/去空格自写（固定缓冲）。
5. **自检红线**（照 hextool 惯例）：
   - on_ready 预填全部输入框（表达式样例、时间戳样例、进制样例、位运算样例）；
   - 剪贴板写、消息框类副作用先判 `cg_app_has_window()`；
   - 每个回调零弹窗零写盘，--selftest 合成事件可全跑。

## 文件清单

- `examples/calculator/calculator.cgui`（新建，界面）
- `examples/calculator/main.c`（新建，行为）
- `.codebuddy/plans/calculator-example.md`（本文件）

## 验证方法

1. `build-app.bat calculator` 编译通过、无新增警告；
2. `calculator.exe --selftest` 退出码 0（合成事件全路径）；
3. 行为抽查（真机）：表达式 `(1+2)*3-4/2`、`0xff&0x0f`、`sqrt(2)`、错误串；
   时间戳往返互转一致；进制 255=0xFF；取反对偶；STR↔HEX 往返；位运算
   `29'h0, 1'h1, 1'h0, 1'h0` → 二进制分组正确；
4. 真机探针脚本：如需自动化则新写 `build/calc-verify.ps1`（P/Invoke + 真点击 +
   断言 + 截图）；本轮先以 --selftest + 手工抽查为准，脚本按验证结果再决定。

## 边界与不做

- 不做科学计数法显示美化（`%.12g` 足够）；
- 表达式不支持变量、赋值、比较运算；
- 时间戳只支持秒级 unix（不支持毫秒）；
- 位运算段宽上限 64（超出报错），段数上限 16。
