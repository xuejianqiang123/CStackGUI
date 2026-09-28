/**
 * @file    cg_kit_modbus.c
 * @brief   Modbus 帧编解码：请求帧拼装 + 响应帧解析（RTU / TCP 两种封装）
 * @details **为什么进框架**：工控上位机十有八九要读写寄存器，而"拼请求帧 / 认响应帧 /
 *          认出异常帧"这段与界面、与传输方式都无关，**每个项目都要重写一遍** ——
 *          docs/articles/09-校验与摘要.md 里**原来**有一段教用户手写 `build_read_holding()` 的示例，
 *          本文件把那 10 行变成一行调用（那篇文档随后也改成引用这里的 API）。
 *
 *          **纯函数：不持状态、不碰 I/O**。连接、超时、重试、轮询节奏仍归应用
 *          （与 cg_crc16 / cg_bytes_* 同一个立场：框架给原料，事务归业务）。
 *          所以这里没有"主站会话"那种东西 —— 一个函数进来，一个帧出去。
 *
 *          两处最容易传错、这里替你钉住的约定：
 *          1. **RTU 的 CRC 低字节在前** —— 手写时最常见的"明明算对了、板子却不认"；
 *          2. **异常响应的功能码是 `原功能码 | 0x80`**，解析后 `out->func` 给的是**原功能码**、
 *             `out->exception` 给异常码，应用不用自己 `& 0x7F` 再猜。
 *          另外 0x05（写单个线圈）在线路上是 `FF00`/`0000` 而不是 1/0：**拼装时两种都收**
 *          （给 1 替你换成 FF00），**解析时原样给出**（调试工具该显示线上真实的东西）。
 *
 *          正确性靠**规范自带的实例帧**（见 tests/unit/test_kit_modbus.c）：
 *          读保持寄存器 `01 03 00 6B 00 03` 后面必须正好是 `76 87`；仓库里那篇文档手写的
 *          `01 03 00 00 00 0A C5 CD` 当作**第二个独立来源**的向量 —— 新 API 与人工手写
 *          必须逐字节一致。两处都不是"看起来对"，是能对上具体字节的。
 *
 *          不在本文件：主站事务（轮询 / 超时 / 重试 / 多从站调度）。那需要一份"会话状态"，
 *          而框架这一层不持有运行状态；真要做，也是在上层组件里做，不是塞进这个 kit。
 */
#include "cstackgui.h"

#include <string.h>

/* ------------------------------------------------------------ 内部小工具 */

/**
 * @brief 在帧尾追加 RTU 的 CRC16（**低字节在前**，Modbus 规定的顺序）
 * @param f 帧缓冲（已有 n 个字节，末尾得留得下 2 个 CRC 字节）
 * @param n 当前帧长（字节）
 * @return int 追加后的新长度（即 n + 2）
 */
static int mb_append_crc(unsigned char *f, int n)
{
    unsigned crc = cg_crc16(f, n);

    f[n++] = (unsigned char)(crc & 0xFF);
    f[n++] = (unsigned char)((crc >> 8) & 0xFF);
    return n;
}

/**
 * @brief 写 7 字节 MBAP 头；pdu_len 是不含 unit id 的 PDU 长度（头里的长度字段要 +1）
 * @param f 帧缓冲（至少 7 字节，就地写）
 * @param tid 事务标识（16 位，大端写进头）
 * @param unit 从站 / 单元号（8 位）
 * @param pdu_len PDU 长度，不含 unit id；头里的长度字段写的是它 + 1
 * @return void 无返回值
 */
static void mb_put_mbap(unsigned char *f, int tid, int unit, int pdu_len)
{
    f[0] = (unsigned char)((tid >> 8) & 0xFF);
    f[1] = (unsigned char)(tid & 0xFF);
    f[2] = 0;                                       /* 协议标识：Modbus 恒为 0 */
    f[3] = 0;
    f[4] = (unsigned char)(((pdu_len + 1) >> 8) & 0xFF);
    f[5] = (unsigned char)((pdu_len + 1) & 0xFF);
    f[6] = (unsigned char)unit;
}

/**
 * @brief 这个功能码是不是"读类"（01/02 位、03/04 寄存器）
 * @param func 功能码
 * @return int 1 = 读类；0 = 不是
 */
static int mb_is_read_func(int func)
{
    return func == 1 || func == 2 || func == 3 || func == 4;
}

/**
 * @brief 这个功能码是不是"写类"（05/06 单个、0F/10 多个）
 * @param func 功能码
 * @return int 1 = 写类；0 = 不是
 */
static int mb_is_write_func(int func)
{
    return func == 5 || func == 6 || func == 15 || func == 16;
}

/* ------------------------------------------------------------ 拼请求帧 */

/**
 * @brief 拼一个"读类"请求帧（01/02 位、03/04 寄存器），RTU / TCP 两种封装都行
 * @param tcp 非 0 = 加 7 字节 MBAP 头（TCP）；0 = 以从站地址开头、帧尾附 CRC（RTU）
 * @param tid 事务标识（仅 TCP 写进 MBAP）
 * @param slave 从站地址（0..255）
 * @param func 功能码（1/2/3/4）
 * @param start 起始地址（0..65535）
 * @param count 数量（位类 1..2000，寄存器类 1..125）
 * @param out 输出缓冲（调用方提供，本函数不接管所有权）
 * @param out_size out 的字节数
 * @return int 写入 out 的帧长（字节）；参数非法或放不下返回 -1
 */
int cg_modbus_build_read(int tcp, int tid, int slave, int func, int start, int count,
                         void *out, int out_size)
{
    unsigned char *f = (unsigned char *)out;
    int            n, pdu_len = 5;

    if (!f || out_size <= 0) return -1;
    if (slave < 0 || slave > 255 || start < 0 || start > 0xFFFF) return -1;
    if (!mb_is_read_func(func)) return -1;
    /* 规范给的边界：位类一次最多 2000 个，寄存器类最多 125 个 */
    if (count < 1 || count > (func <= 2 ? 2000 : 125)) return -1;

    if (tcp) {
        if (out_size < 7 + pdu_len) return -1;
        mb_put_mbap(f, tid, slave, pdu_len);
        n = 7;
    } else {
        if (out_size < 1 + pdu_len + 2) return -1;
        f[0] = (unsigned char)slave;
        n = 1;
    }
    f[n++] = (unsigned char)func;
    f[n++] = (unsigned char)((start >> 8) & 0xFF);
    f[n++] = (unsigned char)(start & 0xFF);
    f[n++] = (unsigned char)((count >> 8) & 0xFF);
    f[n++] = (unsigned char)(count & 0xFF);

    return tcp ? n : mb_append_crc(f, n);
}

/**
 * @brief 拼一个"写类"请求帧（05/06 单个、0F/10 多个），RTU / TCP 两种封装都行
 * @param tcp 非 0 = 加 7 字节 MBAP 头（TCP）；0 = 以从站地址开头、帧尾附 CRC（RTU）
 * @param tid 事务标识（仅 TCP 写进 MBAP）
 * @param slave 从站地址（0..255）
 * @param func 功能码（5/6/15/16）
 * @param start 起始地址（0..65535）
 * @param vals 要写的值（只读）：05/06 只取第一个；15 按位取非 0 为"合"；16 每个值 2 字节
 * @param nvalues 值的个数（05/06 必须为 1；15/16 最多 123）
 * @param out 输出缓冲（调用方提供，本函数不接管所有权）
 * @param out_size out 的字节数
 * @return int 写入 out 的帧长（字节）；参数非法或放不下返回 -1
 */
int cg_modbus_build_write(int tcp, int tid, int slave, int func, int start,
                          const unsigned short *vals, int nvalues, void *out, int out_size)
{
    unsigned char *f = (unsigned char *)out;
    int            n, i, pdu_len;

    if (!f || out_size <= 0 || !vals) return -1;
    if (slave < 0 || slave > 255 || start < 0 || start > 0xFFFF) return -1;
    if (!mb_is_write_func(func)) return -1;
    if (nvalues < 1) return -1;
    if ((func == 5 || func == 6) && nvalues != 1) return -1;      /* 单个：正好一个值 */
    if ((func == 15 || func == 16) && (nvalues > 123)) return -1; /* 多个：规范上限 123 个寄存器 */

    if (func == 5 || func == 6) {
        pdu_len = 5;
    } else if (func == 15) {
        pdu_len = 6 + (nvalues + 7) / 8;                          /* 位类：按位打包 */
    } else {
        pdu_len = 6 + nvalues * 2;                                /* 寄存器类：每值 2 字节 */
    }

    if (tcp) {
        if (out_size < 7 + pdu_len) return -1;
        mb_put_mbap(f, tid, slave, pdu_len);
        n = 7;
    } else {
        if (out_size < 1 + pdu_len + 2) return -1;
        f[0] = (unsigned char)slave;
        n = 1;
    }
    f[n++] = (unsigned char)func;
    f[n++] = (unsigned char)((start >> 8) & 0xFF);
    f[n++] = (unsigned char)(start & 0xFF);

    if (func == 5) {
        /* 线上是 FF00 = 合、0000 = 断。收 1/0 也认（省得每个项目自己写那次换算），
         * 其余值原样发（有些设备用别的非零值表示"合"，调试工具不该拦）。 */
        unsigned v = vals[0];

        if (v == 1) v = 0xFF00;
        f[n++] = (unsigned char)((v >> 8) & 0xFF);
        f[n++] = (unsigned char)(v & 0xFF);
    } else if (func == 6) {
        f[n++] = (unsigned char)((vals[0] >> 8) & 0xFF);
        f[n++] = (unsigned char)(vals[0] & 0xFF);
    } else if (func == 15) {
        int bytes = (nvalues + 7) / 8;

        f[n++] = (unsigned char)((nvalues >> 8) & 0xFF);
        f[n++] = (unsigned char)(nvalues & 0xFF);
        f[n++] = (unsigned char)bytes;
        for (i = 0; i < bytes; i++) {
            int b = 0, k;

            for (k = 0; k < 8; k++) {
                int idx = i * 8 + k;

                if (idx < nvalues && vals[idx]) b |= (1 << k);   /* 低位在前 */
            }
            f[n++] = (unsigned char)b;
        }
    } else {
        f[n++] = (unsigned char)((nvalues >> 8) & 0xFF);
        f[n++] = (unsigned char)(nvalues & 0xFF);
        f[n++] = (unsigned char)(nvalues * 2);
        for (i = 0; i < nvalues; i++) {
            f[n++] = (unsigned char)((vals[i] >> 8) & 0xFF);
            f[n++] = (unsigned char)(vals[i] & 0xFF);
        }
    }

    return tcp ? n : mb_append_crc(f, n);
}

/* ------------------------------------------------------------ 解析响应帧 */

/**
 * @brief 解析一个响应帧（读 / 写 / 异常响应都认），RTU / TCP 两种封装都行
 * @param tcp 非 0 = 按 TCP（先跳 7 字节 MBAP）；0 = 按 RTU（先验 CRC）
 * @param frame 收到的整帧字节（只读）
 * @param len 整帧长度（字节）
 * @param out 解析结果（调用方提供，进来先清零）
 * @return int 0 成功；-1 参数非法或帧太短；-2 CRC 不对（仅 RTU）；-3 帧格式 / 功能码不认
 */
int cg_modbus_parse(int tcp, const void *frame, int len, cg_modbus_rsp_t *out)
{
    const unsigned char *f = (const unsigned char *)frame;
    int pdu, pdu_len, slave, func, i;

    if (!f || !out || len < 0) return -1;
    memset(out, 0, sizeof *out);
    out->addr = -1;

    if (tcp) {
        if (len < 9) return -1;
        if (f[2] != 0 || f[3] != 0) return -3;               /* 协议标识必须为 0 */
        pdu_len = (f[4] << 8) | f[5];
        if (pdu_len < 2 || 6 + pdu_len > len) return -3;     /* 头里写的长度与实际字节数不符 */
        slave   = f[6];
        pdu     = 7;
        pdu_len -= 1;                                        /* 长度含 unit id，PDU 要减掉 */
    } else {
        if (len < 5) return -1;
        /* "含 CRC 的整帧再算一遍必须是 0" —— 这条性质比逐个字节比对更能说明帧没坏，
         * 也是现场验证帧对不对的习惯做法（见 cg_crc16 的注释）。 */
        if (cg_crc16(f, len) != 0) return -2;
        slave   = f[0];
        pdu     = 1;
        pdu_len = len - 3;                                   /* 去掉从站 + 两个 CRC 字节 */
    }

    out->slave = slave;
    func       = f[pdu];

    if (func & 0x80) {                                       /* 异常响应：只有异常码 */
        if (pdu_len != 2) return -3;
        out->func      = func & 0x7F;                        /* 给**原**功能码，应用不用自己剥 */
        out->exception = f[pdu + 1];
        out->nvalues   = 0;
        return 0;
    }

    out->func = func;

    if (mb_is_read_func(func)) {
        int bc = f[pdu + 1];

        if (pdu_len != 2 + bc) return -3;
        if (func >= 3) {                                     /* 寄存器类：每值 2 字节、大端 */
            if (bc <= 0 || bc % 2) return -3;
            if (bc / 2 > CG_MODBUS_MAX_VALUES) return -3;
            out->nvalues = bc / 2;
            for (i = 0; i < out->nvalues; i++)
                out->values[i] = (unsigned short)((f[pdu + 2 + i * 2] << 8) |
                                                   f[pdu + 3 + i * 2]);
        } else {                                             /* 位类：按位解成 0/1 */
            if (bc <= 0) return -3;
            for (i = 0; i < bc * 8 && out->nvalues < CG_MODBUS_MAX_VALUES; i++)
                out->values[out->nvalues++] =
                    (unsigned short)((f[pdu + 2 + i / 8] >> (i % 8)) & 1);
        }
        return 0;
    }

    if (mb_is_write_func(func)) {                            /* 写响应：地址 + 值/数量回显 */
        if (pdu_len != 5) return -3;
        out->addr     = (f[pdu + 1] << 8) | f[pdu + 2];
        out->nvalues  = 1;
        out->values[0] = (unsigned short)((f[pdu + 3] << 8) | f[pdu + 4]);
        return 0;
    }

    return -3;                                               /* 不是我们认的功能码 */
}
