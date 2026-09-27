/**
 * @file    cg_kit_check.c
 * @brief   kit 校验：CRC32 / CRC16-Modbus / MD5 / SHA-1 / SHA-256 / SHA-512
 * @details 来历：易语言精易模块的 `校验_` 一族（12 条：crc32 / md5 / md5_文本 / md4 / md2 /
 *          sha1 / sha256 / sha512 / hmac_md5 / rc4 / 哈希值计算）。CRC 与哈希不是一回事：
 *          哈希是"证明文件没被人改过"，CRC 是"让接收方认出这一帧有没有坏" ——
 *          工控设备协议帧几乎都是 CRC16，而"核对固件 MD5"是哈希的高频场景，两类都给。
 *
 *          CRC 刻意**逐位算、不建表**：没有静态状态 ⇒ 天然线程安全（串口/网口的读线程
 *          随时可能被调），也不用操心"表初始化了没"。代价是比查表慢约 8 倍，
 *          对协议帧与几 MB 的文件无感（要算大文件走 cg_crc32_file，那里是分块读的）。
 *
 *          哈希**手写**，不调 CryptoAPI。**最初写的理由是错的，已按实验更正**：
 *          我原先说"用 advapi32 就得让所有既有用户项目的链接命令补 -ladvapi32"。2026-09-27
 *          用工程自己的链接参数编了一个调用 CryptAcquireContextW 的探针
 *          （build\probe_link\crypt_probe.c），**它链接通过了** —— advapi32 本来就在可解析
 *          范围内（MinGW 的默认链接带着它，而且 cg_kit_sys.c 早就在用 RegOpenKeyExW）。
 *          保留手写实现靠的是这三条**真实**理由：
 *            1. **FIPS 策略下 CryptoAPI 会直接拒绝 CALG_MD5** —— 而"核对固件 MD5"正是本功能
 *               的高频用法；客户机器的安全策略不该让这个功能失效；
 *            2. 结果**不依赖客户机器上的 crypto 配置**（CSP 缺失 / 被策略裁剪 / 版本差异）：
 *               同一个文件在哪台机器上算出来都一样，这对"核对"工具是硬要求；
 *            3. 已经用两条互相独立的证据验过（公开向量 + Node/OpenSSL 派生），风险压住了。
 *          （同一个判据在 cg_local_ip 上是**反过来**的：iphlpapi 确实没链接 —— 探针里
 *          GetAdaptersInfo 报 undefined reference —— 所以"不枚举网卡拿 MAC 地址"仍然成立。
 *          结论：**"我以为需要新链接库"是个可验证的假设，先编译一次再拿它当理由。**）
 *
 *          手写的风险控制（这一段最怕"算得像个样子但其实错"）：
 *          1. 常量表由 build\hash-tables.mjs 用 BigInt **精确推导**（整数牛顿法求
 *             frac(cbrt(p))·2^bits），不靠手敲 200 多个魔数；
 *          2. 检验值由 build\hash-vectors.mjs 走 Node 的 crypto（OpenSSL 血统，与
 *             Windows 完全不同的实现）独立派生，并与 RFC 1321 / FIPS 180-1,2,4 的
 *             公开向量互校；常量进单测，跑测试不需要 Node。
 *
 *          不在本文件：MD2/MD4（没人用了）、HMAC-MD5 与 RC4（一个是协议鉴权、
 *          一个是加密，跟"校验"不是一回事，另开一片）。
 *
 *          长度字段：SHA-512 规范要 128 位，这里只填低 64 位（原文 ≤ 2^64 字节 ≈ 16 EB，
 *          现实里到不了）。
 */
#include "cg_kit_internal.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================ 位运算与字节序 */

/**
 * @brief 32 位循环左移
 * @param x 待移位的值
 * @param n 左移位数（0..31）
 * @return unsigned 移位后的结果
 */
static unsigned rol32(unsigned x, int n) { return (x << n) | (x >> (32 - n)); }

/**
 * @brief 32 位循环右移
 * @param x 待移位的值
 * @param n 右移位数（0..31）
 * @return unsigned 移位后的结果
 */
static unsigned rotr32(unsigned x, int n) { return (x >> n) | (x << (32 - n)); }

/**
 * @brief 64 位循环右移
 * @param x 待移位的值
 * @param n 右移位数（0..63）
 * @return unsigned long long 移位后的结果
 */
static unsigned long long rotr64(unsigned long long x, int n)
{
    return (x >> n) | (x << (64 - n));
}

/**
 * @brief 按**小端**读 32 位（MD5 用）
 * @param p 源数据首字节（至少 4 字节，不做边界检查）
 * @return unsigned 读出的 32 位数
 */
static unsigned le32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

/**
 * @brief 按**大端**读 32 位（SHA-1 / SHA-256 用）
 * @param p 源数据首字节（至少 4 字节，不做边界检查）
 * @return unsigned 读出的 32 位数
 */
static unsigned be32(const unsigned char *p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | (unsigned)p[3];
}

/**
 * @brief 按**大端**读 64 位（SHA-512 用）
 * @param p 源数据首字节（至少 8 字节，不做边界检查）
 * @return unsigned long long 读出的 64 位数
 */
static unsigned long long be64(const unsigned char *p)
{
    unsigned long long v = 0;
    int                i;

    for (i = 0; i < 8; i++) v = (v << 8) | (unsigned long long)p[i];
    return v;
}

/* ================================================================ CRC */

/**
 * @brief CRC32 裸循环（不含初值与末尾取反），分块累算用：把上一块的返回值喂回来即可
 * @param crc 上一块的累算值；首块传 0xFFFFFFFF
 * @param p 数据块首字节
 * @param n 数据块的字节数
 * @return unsigned 累算后的值（未取反）
 */
static unsigned crc32_raw(unsigned crc, const unsigned char *p, size_t n)
{
    size_t i;
    int    k;

    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++) crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
    }
    return crc;
}

/**
 * @brief CRC16 裸循环（多项式 0xA001 反射，不取反，分块累算用）
 * @param crc 上一块的累算值；首块传 0xFFFF
 * @param p 数据块首字节
 * @param n 数据块的字节数
 * @return unsigned 累算后的值（低 16 位有效，未取反）
 */
static unsigned crc16_raw(unsigned crc, const unsigned char *p, size_t n)
{
    size_t i;
    int    k;

    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++) crc = (crc & 1u) ? ((crc >> 1) ^ 0xA001u) : (crc >> 1);
    }
    return crc & 0xFFFFu;
}

/** @brief CRC32（IEEE 802.3 / zlib 同一套：多项式 0xEDB88320 反射，初值全 1，末尾取反）
 *  @param data 数据指针
 *  @param len 字节数（<= 0 或 data 为 NULL 时按空消息算）
 *  @return unsigned 32 位校验值；空输入返回 0（= ~初值，也正是"空消息的 CRC"） */
unsigned cg_crc32(const void *data, int len)
{
    if (len <= 0 || !data) return 0u;
    return ~crc32_raw(0xFFFFFFFFu, (const unsigned char *)data, (size_t)len);
}

/** @brief CRC16/Modbus（多项式 0xA001 反射，初值 0xFFFF，**不**取反）
 *  @param data 数据指针
 *  @param len 字节数（<= 0 或 data 为 NULL 时按空消息算）
 *  @return unsigned 16 位校验值（高 16 位是 0）；空输入返回 0xFFFF。
 *  @note 发了帧之后往帧尾塞的时候记得 **低字节在前**（Modbus 是 little-endian 传 CRC）：
 *        `buf[n] = (unsigned char)(crc & 0xFF); buf[n+1] = (unsigned char)(crc >> 8);` */
unsigned cg_crc16(const void *data, int len)
{
    if (len <= 0 || !data) return 0xFFFFu;
    return crc16_raw(0xFFFFu, (const unsigned char *)data, (size_t)len);
}

/* ================================================================ 分块读文件 */

/**
 * @brief 每读到一个数据块就回调一次
 * @param ctx 调用方挂进去的上下文，由分块读文件的原样透传
 * @param p 数据块首字节
 * @param n 数据块的字节数
 * @return void 无返回值
 * @details CRC 与哈希共用这个骨架，免得各写一遍开关文件与出错处理
 */
typedef void (*cg_chunk_fn)(void *ctx, const unsigned char *p, size_t n);

/**
 * @brief 分块读整个文件，逐块交给 sink
 * @param path 文件路径（UTF-8）
 * @param sink 每读到一个数据块回调一次
 * @param ctx 原样透传给 sink 的上下文
 * @return int 1 读完；0 打不开或中途读出错
 * @details 用 16 KB 栈缓冲循环读，**不把文件整读进内存** —— 固件镜像动辄几十上百 MB。
 *           读的过程中出错（磁盘错误/半截文件）返回 0：宁可报失败，也不给出一个
 *           "看着像但不完整"的校验值。
 */
static int file_chunks(const char *path, cg_chunk_fn sink, void *ctx)
{
    wchar_t       *wp;
    HANDLE         h;
    unsigned char  buf[16 * 1024];
    DWORD          got = 0;
    int            ok  = 0;

    if (!path) return 0;
    wp = kit_w(path);
    if (!wp) return 0;
    h = CreateFileW(wp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wp);
    if (h == INVALID_HANDLE_VALUE) return 0;

    for (;;) {
        if (!ReadFile(h, buf, (DWORD)sizeof(buf), &got, NULL)) break;
        if (got == 0) { ok = 1; break; }              /* 读到 0 字节 = 正常读完 */
        sink(ctx, buf, (size_t)got);
    }
    CloseHandle(h);
    return ok;
}

/**
 * @brief CRC32 的分块回调：把每一块喂进累算
 * @param ctx 累算值的地址（调用方持有的 unsigned 变量）
 * @param p 数据块首字节
 * @param n 数据块的字节数
 * @return void 无返回值
 */
static void crc32_sink(void *ctx, const unsigned char *p, size_t n)
{
    unsigned *acc = (unsigned *)ctx;
    *acc = crc32_raw(*acc, p, n);
}

/** @brief 算整个文件的 CRC32（分块读，不整读进内存）
 *  @param path 文件路径（UTF-8）
 *  @param out 校验值出口；成功才写入
 *  @return int 1 = 成功；0 = 打不开或中途读出错（此时 out 不动） */
int cg_crc32_file(const char *path, unsigned *out)
{
    unsigned acc = 0xFFFFFFFFu;

    if (!out) return 0;
    if (!file_chunks(path, crc32_sink, &acc)) return 0;
    *out = ~acc;
    return 1;
}

/* ================================================================ MD5 */

static const unsigned cg_md5_k[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u,
};

/** MD5 每轮的左移位数（4 轮 × 4 种） */
static const unsigned char cg_md5_s[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21,
};

typedef struct {
    unsigned           h[4];
    unsigned long long len;          /* 已吃进的原文总字节数（末尾长度字段要用） */
    unsigned char      buf[64];
    unsigned           used;
} md5_ctx;

/**
 * @brief 处理一个 64 字节块，更新 MD5 状态
 * @param c MD5 上下文（入出参：状态就地累加）
 * @param p 64 字节块的首字节
 * @return void 无返回值
 */
static void md5_block(md5_ctx *c, const unsigned char *p)
{
    unsigned w[16], a, b, cc, d, f, tmp;
    int      i, g;

    for (i = 0; i < 16; i++) w[i] = le32(p + i * 4);
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    for (i = 0; i < 64; i++) {
        if (i < 16)      { f = (b & cc) | (~b & d);   g = i; }
        else if (i < 32) { f = (d & b) | (~d & cc);   g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ cc ^ d;            g = (3 * i + 5) & 15; }
        else             { f = cc ^ (b | ~d);         g = (7 * i) & 15; }
        tmp = d;
        d   = cc;
        cc  = b;
        b   = b + rol32(a + f + cg_md5_k[i] + w[g], (int)cg_md5_s[i]);
        a   = tmp;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
}

/**
 * @brief 喂入任意字节数，攒够 64 字节就压块
 * @param c MD5 上下文（入出参：长度 / 已用字节 / 缓冲就地更新）
 * @param p 数据首字节
 * @param n 字节数
 * @return void 无返回值
 */
static void md5_update(md5_ctx *c, const unsigned char *p, size_t n)
{
    c->len += (unsigned long long)n;
    while (n > 0) {
        size_t take = 64u - (size_t)c->used;

        if (take > n) take = n;
        memcpy(c->buf + c->used, p, take);
        c->used += (unsigned)take;
        p += take;
        n -= take;
        if (c->used == 64u) {
            md5_block(c, c->buf);
            c->used = 0;
        }
    }
}

/**
 * @brief 补位并收尾，产出 16 字节 MD5 摘要
 * @param c MD5 上下文（入出参：收尾时就地补位）
 * @param out 摘要出口，调用方提供、至少 16 字节
 * @return void 无返回值
 */
static void md5_final(md5_ctx *c, unsigned char out[16])
{
    unsigned long long bits = c->len * 8ull;
    unsigned char      tail[72];
    unsigned           pad;
    int                i;

    /* 补 0x80 + 若干 0，使"原文 + 补位"正好剩 8 字节放长度（不够就再加一整块） */
    pad = (c->used < 56u) ? (56u - c->used) : (120u - c->used);
    tail[0] = 0x80;
    memset(tail + 1, 0, pad - 1u);
    for (i = 0; i < 8; i++) tail[pad + (unsigned)i] = (unsigned char)(bits >> (8 * i));   /* 小端 */
    md5_update(c, tail, (size_t)pad + 8u);

    for (i = 0; i < 4; i++) {
        out[i * 4 + 0] = (unsigned char)(c->h[i]);
        out[i * 4 + 1] = (unsigned char)(c->h[i] >> 8);
        out[i * 4 + 2] = (unsigned char)(c->h[i] >> 16);
        out[i * 4 + 3] = (unsigned char)(c->h[i] >> 24);
    }
}

/* ================================================================ SHA-1 */

/** SHA-1 的初始值：与 MD5 同源的固定序列（**不是** sqrt 派生 —— 生成器曾按 sqrt 规则给出
 *  SHA-256 的 IV，核对方才发现；这一组手写，由公开向量验） */
static const unsigned cg_sha1_iv[5] = {
    0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u,
};

typedef struct {
    unsigned           h[5];
    unsigned long long len;
    unsigned char      buf[64];
    unsigned           used;
} sha1_ctx;

/**
 * @brief 处理一个 64 字节块，更新 SHA-1 状态
 * @param c SHA-1 上下文（入出参：状态就地累加）
 * @param p 64 字节块的首字节
 * @return void 无返回值
 */
static void sha1_block(sha1_ctx *c, const unsigned char *p)
{
    unsigned w[80], a, b, cc, d, e, f, k, t;
    int      i;

    for (i = 0; i < 16; i++) w[i] = be32(p + i * 4);
    for (i = 16; i < 80; i++) w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3]; e = c->h[4];
    for (i = 0; i < 80; i++) {
        if (i < 20)      { f = (b & cc) | (~b & d);             k = 0x5a827999u; }
        else if (i < 40) { f = b ^ cc ^ d;                      k = 0x6ed9eba1u; }
        else if (i < 60) { f = (b & cc) | (b & d) | (cc & d);   k = 0x8f1bbcdcu; }
        else             { f = b ^ cc ^ d;                      k = 0xca62c1d6u; }
        t  = rol32(a, 5) + f + e + k + w[i];
        e  = d;
        d  = cc;
        cc = rol32(b, 30);
        b  = a;
        a  = t;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d; c->h[4] += e;
}

/**
 * @brief 喂入任意字节数，攒够 64 字节就压块
 * @param c SHA-1 上下文（入出参：长度 / 已用字节 / 缓冲就地更新）
 * @param p 数据首字节
 * @param n 字节数
 * @return void 无返回值
 */
static void sha1_update(sha1_ctx *c, const unsigned char *p, size_t n)
{
    c->len += (unsigned long long)n;
    while (n > 0) {
        size_t take = 64u - (size_t)c->used;

        if (take > n) take = n;
        memcpy(c->buf + c->used, p, take);
        c->used += (unsigned)take;
        p += take;
        n -= take;
        if (c->used == 64u) {
            sha1_block(c, c->buf);
            c->used = 0;
        }
    }
}

/**
 * @brief 补位并收尾，产出 20 字节 SHA-1 摘要
 * @param c SHA-1 上下文（入出参：收尾时就地补位）
 * @param out 摘要出口，调用方提供、至少 20 字节
 * @return void 无返回值
 */
static void sha1_final(sha1_ctx *c, unsigned char out[20])
{
    unsigned long long bits = c->len * 8ull;
    unsigned char      tail[72];
    unsigned           pad;
    int                i;

    pad = (c->used < 56u) ? (56u - c->used) : (120u - c->used);
    tail[0] = 0x80;
    memset(tail + 1, 0, pad - 1u);
    for (i = 0; i < 8; i++) tail[pad + (unsigned)i] = (unsigned char)(bits >> (8 * (7 - i)));   /* 大端 */
    sha1_update(c, tail, (size_t)pad + 8u);

    for (i = 0; i < 5; i++) {
        out[i * 4 + 0] = (unsigned char)(c->h[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(c->h[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(c->h[i] >> 8);
        out[i * 4 + 3] = (unsigned char)(c->h[i]);
    }
}

/* ================================================================ SHA-256 */

/* IV 与 K 都由 build\hash-tables.mjs 精确推导：frac(sqrt(p))·2^32 与 frac(cbrt(p))·2^32 */
static const unsigned cg_sha256_iv[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
};

static const unsigned cg_sha256_k[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

typedef struct {
    unsigned           h[8];
    unsigned long long len;
    unsigned char      buf[64];
    unsigned           used;
} sha256_ctx;

/**
 * @brief 处理一个 64 字节块，更新 SHA-256 状态
 * @param c SHA-256 上下文（入出参：状态就地累加）
 * @param p 64 字节块的首字节
 * @return void 无返回值
 */
static void sha256_block(sha256_ctx *c, const unsigned char *p)
{
    unsigned w[64], a, b, cc, d, e, f, g, h, t1, t2;
    int      i;

    for (i = 0; i < 16; i++) w[i] = be32(p + i * 4);
    for (i = 16; i < 64; i++) {
        unsigned s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        unsigned s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g = c->h[6];  h = c->h[7];
    for (i = 0; i < 64; i++) {
        unsigned s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        unsigned ch = (e & f) ^ (~e & g);
        unsigned s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        unsigned mj = (a & b) ^ (a & cc) ^ (b & cc);

        t1 = h + s1 + ch + cg_sha256_k[i] + w[i];
        t2 = s0 + mj;
        h  = g; g = f; f = e; e = d + t1;
        d  = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g;  c->h[7] += h;
}

/**
 * @brief 喂入任意字节数，攒够 64 字节就压块
 * @param c SHA-256 上下文（入出参：长度 / 已用字节 / 缓冲就地更新）
 * @param p 数据首字节
 * @param n 字节数
 * @return void 无返回值
 */
static void sha256_update(sha256_ctx *c, const unsigned char *p, size_t n)
{
    c->len += (unsigned long long)n;
    while (n > 0) {
        size_t take = 64u - (size_t)c->used;

        if (take > n) take = n;
        memcpy(c->buf + c->used, p, take);
        c->used += (unsigned)take;
        p += take;
        n -= take;
        if (c->used == 64u) {
            sha256_block(c, c->buf);
            c->used = 0;
        }
    }
}

/**
 * @brief 补位并收尾，产出 32 字节 SHA-256 摘要
 * @param c SHA-256 上下文（入出参：收尾时就地补位）
 * @param out 摘要出口，调用方提供、至少 32 字节
 * @return void 无返回值
 */
static void sha256_final(sha256_ctx *c, unsigned char out[32])
{
    unsigned long long bits = c->len * 8ull;
    unsigned char      tail[72];
    unsigned           pad;
    int                i;

    pad = (c->used < 56u) ? (56u - c->used) : (120u - c->used);
    tail[0] = 0x80;
    memset(tail + 1, 0, pad - 1u);
    for (i = 0; i < 8; i++) tail[pad + (unsigned)i] = (unsigned char)(bits >> (8 * (7 - i)));
    sha256_update(c, tail, (size_t)pad + 8u);

    for (i = 0; i < 8; i++) {
        out[i * 4 + 0] = (unsigned char)(c->h[i] >> 24);
        out[i * 4 + 1] = (unsigned char)(c->h[i] >> 16);
        out[i * 4 + 2] = (unsigned char)(c->h[i] >> 8);
        out[i * 4 + 3] = (unsigned char)(c->h[i]);
    }
}

/* ================================================================ SHA-512 */

/* IV 与 K 同理由 hash-tables.mjs 推导：frac(sqrt(p))·2^64 与 frac(cbrt(p))·2^64 */
static const unsigned long long cg_sha512_iv[8] = {
    0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull,
    0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
    0x510e527fade682d1ull, 0x9b05688c2b3e6c1full,
    0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull,
};

static const unsigned long long cg_sha512_k[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull,
    0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
    0x3956c25bf348b538ull, 0x59f111f1b605d019ull,
    0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
    0xd807aa98a3030242ull, 0x12835b0145706fbeull,
    0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull,
    0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
    0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull,
    0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull,
    0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull,
    0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
    0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull,
    0x06ca6351e003826full, 0x142929670a0e6e70ull,
    0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull,
    0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull,
    0x81c2c92e47edaee6ull, 0x92722c851482353bull,
    0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull,
    0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
    0xd192e819d6ef5218ull, 0xd69906245565a910ull,
    0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull,
    0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
    0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull,
    0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
    0x748f82ee5defb2fcull, 0x78a5636f43172f60ull,
    0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull,
    0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
    0xca273eceea26619cull, 0xd186b8c721c0c207ull,
    0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
    0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull,
    0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull,
    0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
    0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull,
    0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull,
};

typedef struct {
    unsigned long long h[8];
    unsigned long long len;          /* 只存低 64 位（规范要 128 位，见文件头注释） */
    unsigned char      buf[128];
    unsigned           used;
} sha512_ctx;

/**
 * @brief 处理一个 128 字节块，更新 SHA-512 状态
 * @param c SHA-512 上下文（入出参：状态就地累加）
 * @param p 128 字节块的首字节
 * @return void 无返回值
 */
static void sha512_block(sha512_ctx *c, const unsigned char *p)
{
    unsigned long long w[80], a, b, cc, d, e, f, g, h, t1, t2;
    int                i;

    for (i = 0; i < 16; i++) w[i] = be64(p + (size_t)i * 8);
    for (i = 16; i < 80; i++) {
        unsigned long long s0 = rotr64(w[i - 15], 1) ^ rotr64(w[i - 15], 8) ^ (w[i - 15] >> 7);
        unsigned long long s1 = rotr64(w[i - 2], 19) ^ rotr64(w[i - 2], 61) ^ (w[i - 2] >> 6);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    a = c->h[0]; b = c->h[1]; cc = c->h[2]; d = c->h[3];
    e = c->h[4]; f = c->h[5]; g  = c->h[6]; h = c->h[7];
    for (i = 0; i < 80; i++) {
        unsigned long long s1 = rotr64(e, 14) ^ rotr64(e, 18) ^ rotr64(e, 41);
        unsigned long long ch = (e & f) ^ (~e & g);
        unsigned long long s0 = rotr64(a, 28) ^ rotr64(a, 34) ^ rotr64(a, 39);
        unsigned long long mj = (a & b) ^ (a & cc) ^ (b & cc);

        t1 = h + s1 + ch + cg_sha512_k[i] + w[i];
        t2 = s0 + mj;
        h  = g; g = f; f = e; e = d + t1;
        d  = cc; cc = b; b = a; a = t1 + t2;
    }
    c->h[0] += a; c->h[1] += b; c->h[2] += cc; c->h[3] += d;
    c->h[4] += e; c->h[5] += f; c->h[6] += g;  c->h[7] += h;
}

/**
 * @brief 喂入任意字节数，攒够 128 字节就压块
 * @param c SHA-512 上下文（入出参：长度 / 已用字节 / 缓冲就地更新）
 * @param p 数据首字节
 * @param n 字节数
 * @return void 无返回值
 */
static void sha512_update(sha512_ctx *c, const unsigned char *p, size_t n)
{
    c->len += (unsigned long long)n;
    while (n > 0) {
        size_t take = 128u - (size_t)c->used;

        if (take > n) take = n;
        memcpy(c->buf + c->used, p, take);
        c->used += (unsigned)take;
        p += take;
        n -= take;
        if (c->used == 128u) {
            sha512_block(c, c->buf);
            c->used = 0;
        }
    }
}

/**
 * @brief 补位并收尾，产出 64 字节 SHA-512 摘要
 * @param c SHA-512 上下文（入出参：收尾时就地补位）
 * @param out 摘要出口，调用方提供、至少 64 字节
 * @return void 无返回值
 */
static void sha512_final(sha512_ctx *c, unsigned char out[64])
{
    unsigned long long bits = c->len * 8ull;
    unsigned char      tail[256];
    unsigned           pad;
    unsigned long long hi = 0;                    /* 128 位长度的高 64 位：原文 ≤ 2^64 字节时恒为 0 */
    int                i;

    /* 补位后剩 16 字节放长度（128 位），不够就再加一整块 */
    pad = (c->used < 112u) ? (112u - c->used) : (240u - c->used);
    tail[0] = 0x80;
    memset(tail + 1, 0, pad - 1u);
    for (i = 0; i < 8; i++) {
        tail[pad + (unsigned)i]      = (unsigned char)(hi >> (8 * (7 - i)));
        tail[pad + 8u + (unsigned)i] = (unsigned char)(bits >> (8 * (7 - i)));
    }
    sha512_update(c, tail, (size_t)pad + 16u);

    for (i = 0; i < 8; i++) {
        int j;

        for (j = 0; j < 8; j++) out[i * 8 + j] = (unsigned char)(c->h[i] >> (8 * (7 - j)));
    }
}

/* ================================================================ 哈希总入口 */

enum { CG_HASH_MD5 = 1, CG_HASH_SHA1, CG_HASH_SHA256, CG_HASH_SHA512 };

typedef struct {
    int id;
    union {
        md5_ctx    md5;
        sha1_ctx   sha1;
        sha256_ctx sha256;
        sha512_ctx sha512;
    } u;
} hash_ctx;

/**
 * @brief 算法名 -> 内部编号（大小写不敏感）
 * @param name 算法名，取值 md5 / sha1 / sha256 / sha512
 * @return int 内部编号；不认识返回 0
 */
static int hash_algo_id(const char *name)
{
    static const struct { const char *name; int id; } k_tab[] = {
        { "md5", CG_HASH_MD5 }, { "sha1", CG_HASH_SHA1 },
        { "sha256", CG_HASH_SHA256 }, { "sha512", CG_HASH_SHA512 },
    };
    size_t i;

    if (!name) return 0;
    for (i = 0; i < sizeof(k_tab) / sizeof(k_tab[0]); i++) {
        const char *a = name;
        const char *b = k_tab[i].name;

        while (*a && *b && kit_lower(*a) == *b) { a++; b++; }
        if (*a == '\0' && *b == '\0') return k_tab[i].id;
    }
    return 0;
}

/** MD5 的初始值（规范里就是这个固定序列，不是派生值） */
static const unsigned cg_md5_iv[4] = {
    0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u,
};

/**
 * @brief 按算法名初始化多态哈希上下文并装好初值
 * @param hc 上下文出口（入出参：就地清零并填初值）
 * @param algo 算法名，取值同 cg_hash
 * @return int 1 初始化成功；0 算法名不认识
 */
static int hash_begin(hash_ctx *hc, const char *algo)
{
    int id = hash_algo_id(algo);
    int i;

    if (!id) return 0;
    memset(hc, 0, sizeof(*hc));
    hc->id = id;
    switch (id) {
    case CG_HASH_MD5:
        for (i = 0; i < 4; i++) hc->u.md5.h[i] = cg_md5_iv[i];
        break;
    case CG_HASH_SHA1:
        for (i = 0; i < 5; i++) hc->u.sha1.h[i] = cg_sha1_iv[i];
        break;
    case CG_HASH_SHA256:
        for (i = 0; i < 8; i++) hc->u.sha256.h[i] = cg_sha256_iv[i];
        break;
    case CG_HASH_SHA512:
        for (i = 0; i < 8; i++) hc->u.sha512.h[i] = cg_sha512_iv[i];
        break;
    default:
        break;
    }
    return 1;
}

/**
 * @brief 按当前算法把一段数据喂进上下文
 * @param hc 哈希上下文（入出参：按算法分派更新）
 * @param p 数据首字节
 * @param n 字节数
 * @return void 无返回值
 */
static void hash_update(hash_ctx *hc, const unsigned char *p, size_t n)
{
    switch (hc->id) {
    case CG_HASH_MD5:    md5_update(&hc->u.md5, p, n);       break;
    case CG_HASH_SHA1:   sha1_update(&hc->u.sha1, p, n);     break;
    case CG_HASH_SHA256: sha256_update(&hc->u.sha256, p, n); break;
    case CG_HASH_SHA512: sha512_update(&hc->u.sha512, p, n); break;
    default: break;
    }
}

/**
 * @brief 收尾，返回摘要字节数
 * @param hc 哈希上下文（入出参：就地收尾）
 * @param out 摘要出口，调用方提供，按算法至少 16 / 20 / 32 / 64 字节
 * @return int 摘要字节数；0 = 出错
 */
static int hash_end(hash_ctx *hc, unsigned char *out)
{
    switch (hc->id) {
    case CG_HASH_MD5:    md5_final(&hc->u.md5, out);       return 16;
    case CG_HASH_SHA1:   sha1_final(&hc->u.sha1, out);     return 20;
    case CG_HASH_SHA256: sha256_final(&hc->u.sha256, out); return 32;
    case CG_HASH_SHA512: sha512_final(&hc->u.sha512, out); return 64;
    default: return 0;
    }
}

/**
 * @brief 哈希的分块回调：把每一块喂进哈希上下文
 * @param ctx 哈希上下文的地址（调用方持有的 hash_ctx）
 * @param p 数据块首字节
 * @param n 数据块的字节数
 * @return void 无返回值
 */
static void hash_sink(void *ctx, const unsigned char *p, size_t n)
{
    hash_update((hash_ctx *)ctx, p, n);
}

/**
 * @brief 摘要 -> 小写十六进制堆串
 * @param d 摘要字节
 * @param n 摘要字节数
 * @return char * 堆串（调用方负责 free）；内存不足返回 NULL
 */
static char *digest_hex(const unsigned char *d, int n)
{
    static const char k_hex[] = "0123456789abcdef";
    char             *s = (char *)malloc((size_t)n * 2 + 1);
    int               i;

    if (!s) return NULL;
    for (i = 0; i < n; i++) {
        s[i * 2]     = k_hex[d[i] >> 4];
        s[i * 2 + 1] = k_hex[d[i] & 15];
    }
    s[n * 2] = '\0';
    return s;
}

/** @brief 算一段内存的摘要，返回**小写十六进制**堆串（记得 cg_free）
 *  @param data 数据；len <= 0 时按空数据算
 *  @param len 字节数
 *  @param algo "md5" / "sha1" / "sha256" / "sha512"（大小写不敏感）
 *  @return char* 摘要串；algo 不认识或内存不足返回 NULL */
char *cg_hash(const void *data, int len, const char *algo)
{
    hash_ctx      hc;
    unsigned char d[64];
    int           n;

    if (!hash_begin(&hc, algo)) return NULL;
    if (len > 0 && data) hash_update(&hc, (const unsigned char *)data, (size_t)len);
    n = hash_end(&hc, d);
    return (n > 0) ? digest_hex(d, n) : NULL;
}

/** @brief 算整个文件的摘要（分块读，不整读进内存）
 *  @param path 文件路径（UTF-8）
 *  @param algo 同 cg_hash
 *  @return char* 小写十六进制摘要串（记得 cg_free）；打不开 / 中途读出错 / algo 不认识返回 NULL */
char *cg_hash_file(const char *path, const char *algo)
{
    hash_ctx      hc;
    unsigned char d[64];
    int           n;

    if (!hash_begin(&hc, algo)) return NULL;
    if (!file_chunks(path, hash_sink, &hc)) return NULL;
    n = hash_end(&hc, d);
    return (n > 0) ? digest_hex(d, n) : NULL;
}
