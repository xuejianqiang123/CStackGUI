# 实现计划（简版）：cg_kit 编码一组 —— 十六进制 / Base64 / URL

> 这是"从易语言精易模块学功能"那条线的第一片。精易模块（`精易模块v11.0.5[源码].e`）
> 4.5 MB、2715 条命令、322 个分类，其中与本工程最对得上的是 `编码_` / `校验_` / `对称算法_` /
> `进制_` / `字节集_` 这一族，而本工程的 `cg_kit` 里**一条都没有**（`cstackgui.h` 里
> `base64|md5|sha|aes|crypt` 零命中）。
>
> 本片只做**纯字符串/字节**的那几个：零新依赖、能拿公开测试向量钉死、当天能验完。
> 哈希（MD5/SHA1）与加密（AES）单独一片 —— 它们要么手写几百行、要么新增链接依赖
> （CryptoAPI/BCrypt），是独立决策，不混进来。

## 1. 要解决的**具体**问题（不是"别人有我们也得有"）

- `examples\serialterm\main.c` 与 `examples\netterm\main.c` 里各有一份**逐字相同**的手写
  `hex_parse()`（连注释都一样）—— "同一事实有两份"，正是本工程最忌讳的那种。
  两边还各自手写了一遍 `%02X` 拼显示串。
- 而业务想发一段 Base64 / 编一个 URL 参数，只能自己 include `<winsock.h>` 或手写表 ——
  前者违反"业务只 include cstackgui.h"，后者谁写谁知道容易错。
- 目标受众（写串口/网口/HTTP 调试小工具的人）里，这三个是**协议的公共分母**。

## 2. 契约（逐字）

```c
int cg_hex_encode(const void *data, int len, const char *sep, int upper, char *out, int out_size);
int cg_hex_decode(const char *text, void *out, int out_size);
int cg_base64_encode(const void *data, int len, char *out, int out_size);
int cg_base64_decode(const char *text, void *out, int out_size);
int cg_url_encode(const char *text, int space_plus, char *out, int out_size);
int cg_url_decode(const char *text, char *out, int out_size);
```

- **形态**：调用方给缓冲（与 `cg_open_file_dialog` / `cg_serial_read` 同一口径），
  不在这里 malloc —— 业务多是"把结果喂给控件或直接发出去"。
- **返回值**：写出的长度（不含结尾 `'\0'`）；参数/内容非法 **-1**；解码类缓冲放不下 **-2**
  （编码类需要的长度能事先算出来，放不下归 -1）。
- `cg_hex_decode` 的语义**逐字对齐**示例里那两段手写实现（空格/制表/换行/逗号/连字符当分隔符、
  `1A2B` 不写分隔也算对、只剩半个字节或非法字符 -1、放不下 -2）—— 所以那两个示例能直接换过来。
- `cg_url_encode` 的不编码集 = RFC 3986 unreserved（`A-Za-z0-9-_.~`），其余**按字节**编成 `%XX`
  （UTF-8 中文自然变成多个 `%XX`）。`cg_url_decode` 认 `%XX` 与 `+`（`+` → 空格）。
- `cg_base64_decode` **带不带 `=` 填充都认**（JWT 那类不填充的也能解），但会拒：
  长度 4n+1、不含规填充、`=` 之后还有内容、**尾部攒下的位不是 0**（最后一条最难查：
  只查长度的实现会安静地解出一个错字节）。

## 3. 边界（v1 不做，以及为什么）

- **不做 URL-safe Base64**（`-` / `_`）：仓库里零证据，需要的人自己换两个字符即可。
- **不做进制互转**（精易 `进制_` 14 条）：仓库里零证据，而十六进制展示已由 `cg_hex_*` 覆盖，
  要 `2`/`8` 进制的场景在桌面调试工具里罕见。真需要再加（当时评估过：两个函数、约 40 行）。
- **不做哈希/加密**：见开头，单独一片。
- 不提供"自动分配缓冲"的重载：那是另一套所有权规则，混在一起会让 `cg_free` 的约定变模糊。

## 4. 落地

| # | 文件 | 改什么 |
|---|---|---|
| 1 | `runtime/include/cstackgui.h` | 6 个声明 + 「编码」小节注释（含返回值与两条最难查的规则） |
| 2 | `runtime/src/kit/cg_kit_str.c` | 实现（约 200 行，含 `kit_hex_val` 与 Base64 表） |
| 3 | `tests/unit/test_kit_str.c`（新） | 4 个套件：Base64 用 **RFC 4648 §10 标准向量**；HEX 用示例那套语义；URL 含 UTF-8 中文 |
| 4 | `examples/serialterm/main.c`、`examples/netterm/main.c` | 删掉两份手写 `hex_parse()`，改用 `cg_hex_decode()`（并留指路注释） |
| 5 | `designer/src/supportlib.ts` | 画布「支持库」6 条（唯一中文索引） |
| 6 | `docs/CGUI-FORMAT.md`、`README.md` | API 速查 + 能力段 |

## 5. 验证

`build-all.bat`（示例改过，必须真编）→ `tools\run-tests.bat --memcheck`（78 套件 / 2295 断言 /
逐套件零新增存活堆块）→ `tools\check-format.bat`（支持库 221 条一致 / 分层 195 项）→
画布 `tsc --noEmit` + `vite build`。**全部通过。**

实施中两处红过，都是**我测试夹具写错**而不是实现错：`"1A 2B 2c"` 的期望值我误用了编码用例的
`{01,02,AB}`；`"1a,2b-3c"` 里 `3c` 是 `0x3C` 我却按 `0x2C` 比。记下来是因为这正是本工程
"验收命令必须能失败"那条的价值 —— 断言先红过，才说明它在真查。

## 6. 没验到的

- 示例跑起来之后的**界面**行为没重新截图（只验到"编译过 + 自检过"）：HEX 发送路径的语义
  由单测的往返用例保证，但"勾上 HEX、发一串、日志里显示对"这条**人在屏幕上看到的**链路
  这一轮没跑。
