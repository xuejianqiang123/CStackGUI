# 实现计划（简版）：cg_kit 哈希（MD5 / SHA-1 / SHA-256 / SHA-512）

> 单片完成 `校验_` 那一族（精易 12 条）：上一片做了 CRC 两个，这一片做哈希。
> 做完一族比另开一族故事更完整；`系统_`（255 条）与 `字节集_`（27 条）排后面。

## 1. 决定：**手写**，不调 CryptoAPI

先说清楚这不是"省依赖"的选择，而是**链接命令是用户可见的公开契约**：

- 用 CryptoAPI 就得在 `build-app.bat`、`packaging\build-app.bat`、`tools\run-tests.bat`、
  `tools\check-kit.bat` **以及 `docs/ARCHITECTURE.md` 里教用户的那行链接命令**
  （`-lcomctl32 -luser32 -lgdi32 -lshell32 …`）都补 `-ladvapi32`。
  于是**所有既有用户项目的构建会当场 `undefined reference to CryptAcquireContextW`**。
  给一个校验功能配上"请改你的链接参数"，代价明显不对等。
- 附带两个坑：**FIPS 策略下 `CALG_MD5` 会被系统拒绝**（而"核对固件 MD5"正是本功能的高频用法）；
  以及失去平台无关性（手写这段是纯计算，将来别的后端也能用）。

代价是自己维护 4 个算法。风险用两条独立证据压住（见 §4）。

## 2. 契约

```c
char *cg_hash(const void *data, int len, const char *algo);   /* md5/sha1/sha256/sha512 */
char *cg_hash_file(const char *path, const char *algo);       /* 分块读，核固件用 */
int   cg_crc32_file(const char *path, unsigned *out);         /* 补上一片承认的"文件"缺口 */
```

- 算法名**大小写不敏感**，不认识返回 `NULL`（不猜、不缩略）。
- 返回**小写十六进制**堆串（32/40/64/128 字符），用完 `cg_free`。
- 文件版本用 **16 KB 分块读**（`CreateFileW` + `ReadFile`，kit 层本来就是 Win32 平台服务层），
  **不把文件整读进内存** —— 固件镜像几十上百 MB。
- 读中途出错返回失败，**不给"看着像但不完整"的校验值**：算错一个字节的 MD5 比不给更糟 ——
  用户会拿它当"固件被人改过"的证据。
- 长度字段：SHA-512 规范要 128 位，只填低 64 位（原文 ≤ 2^64 字节，现实到不了），已写进文件头。

## 3. 落地

| # | 文件 | 改什么 |
|---|---|---|
| 1 | `runtime/src/kit/cg_kit_check.c` | 4 个算法 + 分块读骨架 + 三个公共函数；CRC 抽出 `*_raw` 以便分块累算 |
| 2 | `runtime/include/cstackgui.h` | 3 个声明 + 一节注释（含"为什么手写"） |
| 3 | `tests/unit/test_kit_check.c` | 3 个新套件：公开向量 / 补位边界 / 算法名与文件路径 |
| 4 | `designer/src/supportlib.ts` | 3 条词条，分组改名「校验（CRC / 哈希）」 |
| 5 | `docs/CGUI-FORMAT.md`、`README.md`、`docs/ARCHITECTURE.md` | API 速查 / 能力段 / kit 清单 |

## 4. 验证：两条**互相独立**的证据

手写的密码学代码最怕"算得像个样子但其实错"。所以：

1. **常量表不靠手敲**：`build\hash-tables.mjs` 用 BigInt 精确推导
   （整数牛顿法求 `frac(cbrt(p))·2^bits` 与 `frac(sqrt(p))·2^bits`）。MD5 的 `K[64]+S[64]`、
   SHA-256 的 `K[64]`、SHA-512 的 `K[80]`、各算法 IV 全是推导出来的。
2. **检验值来自另一个实现**：`build\hash-vectors.mjs` 走 Node 的 `crypto`（OpenSSL 血统，
   与 Windows 毫无关系）派生，并与 **RFC 1321 / FIPS 180-1,2,4 的公开向量**互校。

覆盖：10 条公开向量（含 `md5("")`、`sha1/sha256(56 字节)`、`sha512(112 字节)`）+
16 条分块边界常量（长度 64/65/128/129 × 4 算法，其中 128/129 是 128 字节块的分水岭）+
文件路径（分块读 == 一次算，含 40 KB 跨块文件）+ 错误路径（不认识的算法名、打不开的文件、
`out` 不被脏写）。

### 实施中的两个发现

1. **推导出来的不等于对的**：生成器对 SHA-1 的 IV 用了 `sqrt(p)` 规则，而 SHA-1 的 IV 是
   MD5 式的固定序列（`67452301…C3D2E1F0`）—— 靠"与我记忆中的公开实现逐条核对"才发现。
   所以 SHA-1 的 5 个初始值改为手写（由公开向量验）。这条值得记住：
   **自动化推导只保证"按我写的规则算对了"，不保证"我的规则是对的"。**
2. 分块读的骨架（`file_chunks` + 回调）被 CRC 与哈希共用 —— 否则"打开/循环/出错/关闭"
   这段要写两遍，出错处理迟早会分叉。

## 5. 完成记录（2026-09-27）

- [x] 落地清单 1~5
- [x] `tools\run-tests.bat --memcheck` **84 套件 / 2402 断言 / 0 失败**，逐套件零新增存活堆块
      （哈希单测一次通过：10 条公开向量 + 16 条边界 + 跨块文件全对）
- [x] `build-all.bat` 全示例自检、`check-format.bat`、画布 `tsc --noEmit` + `vite build` 全绿
- [x] runtime 编译零警告（`-Wall -Wextra`）
- [ ] 提交：按 `git-redlines` 口径

## 6. 边界（明确不做）

- **MD2 / MD4**：没人用了（MD4 是 NTLM 那类老协议用的，工具里没有场景）。
- **HMAC-MD5**：是"协议鉴权"不是"校验"，跟 `cg_hash` 的语义不同（要 key），另开一片。
- **RC4**：加密，不是摘要，另开一片。
- 没有流出式（begin/update/end）公共 API：`cg_hash_file` 内部就是分块的，用户要的场景已覆盖；
  等真有人要"边收网络流水边算哈希"再说（YAGNI）。
