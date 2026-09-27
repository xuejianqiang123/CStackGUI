/**
 * @file    cg_kit_check.c
 * @brief   kit 校验：CRC32 / CRC16-Modbus
 * @details 从"从易语言精易模块学功能"那条线来的 —— 它的 `校验_` 一族有 crc32 / md5 / sha1 /
 *          sha256 / sha512 / hmac_md5 / rc4。这里先给 CRC 两个，因为**它们是工控的日常**：
 *          CRC 与哈希不是一回事 —— 哈希是"证明文件没被人改过"，CRC 是"让接收方认出这一帧
 *          有没有坏"，Modbus RTU 这类设备协议几乎都是 CRC16。
 *
 *          实现刻意**逐位算、不建表**：没有静态状态 ⇒ 天然线程安全（串口/网口的读线程随时
 *          可能被调），也不用操心"表初始化了没"。代价是比查表慢约 8 倍 ——
 *          对协议帧与几 MB 的文件无感；上百 MB 的大文件不划算，那种场合另说。
 *
 *          哈希（MD5/SHA1/SHA256/HMAC）**不在这里**：要么手写几百行、要么新增链接依赖
 *          （CryptoAPI / BCrypt），是独立决策，单独一片。
 */
#include "cg_kit_internal.h"

/** @brief CRC32（IEEE 802.3 / zlib：多项式 0xEDB88320 反射，初值全 1，末尾取反）
 *  @param data 数据指针
 *  @param len 字节数（<= 0 或 data 为 NULL 时按空消息算）
 *  @return unsigned 32 位校验值；空输入返回 0（= ~初值，也正是"空消息的 CRC"） */
unsigned cg_crc32(const void *data, int len)
{
    const unsigned char *p = (const unsigned char *)data;
    unsigned             crc = 0xFFFFFFFFu;
    int                  i, k;

    for (i = 0; i < len && p; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return ~crc;
}

/** @brief CRC16/Modbus（多项式 0xA001 反射，初值 0xFFFF，**不**取反）
 *  @param data 数据指针
 *  @param len 字节数（<= 0 或 data 为 NULL 时按空消息算）
 *  @return unsigned 16 位校验值（高 16 位是 0）；空输入返回 0xFFFF。
 *  @note 发了帧之后往帧尾塞的时候记得 **低字节在前**（Modbus 是 little-endian 传 CRC）：
 *        `buf[n] = (unsigned char)(crc & 0xFF); buf[n+1] = (unsigned char)(crc >> 8);` */
unsigned cg_crc16(const void *data, int len)
{
    const unsigned char *p = (const unsigned char *)data;
    unsigned             crc = 0xFFFFu;
    int                  i, k;

    for (i = 0; i < len && p; i++) {
        crc ^= p[i];
        for (k = 0; k < 8; k++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xA001u) : (crc >> 1);
        }
    }
    return crc & 0xFFFFu;
}
