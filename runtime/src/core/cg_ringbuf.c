/**
 * @file    cg_ringbuf.c
 * @brief   环形字节缓冲（cg_ring_t）：平台服务层收数据用的定容 FIFO
 * @details 为什么单独一个文件：串口收数据需要"读线程写、UI 线程读"的缓冲，网口以后用同一套。
 *          它**故意不带锁** —— 谁用谁负责同步（串口那边是一把 CRITICAL_SECTION），于是测试
 *          可以在单线程里把环绕、溢出、清空这些边界挨个锤一遍，不用去赌并发时序。
 *          满了的行为是**丢最老的**：与终端控件的行数上限同一个策略 —— 宁可缺旧数据，
 *          也要保住最新那一帧（盯串口日志时，最新的一帧往往才是你正在找的那一帧）。
 */
#include "cg_internal.h"

#include <stdlib.h>
#include <string.h>

/** @brief 建环；容量非法或没内存返回 0。
 *  @note  **先无条件清零，绝不读旧内容**。这样即使调用方递进来一个没初始化的结构体，
 *         也不会去 free 一个野指针 —— free 野指针是一次堆损坏，而它报错的位置往往
 *         离现场很远（这里踩过：症状是整套测试跑到一半进程 0xC0000374 退出）。
 *         代价是"要用新容量重建，得先自己 cg_ring_free"，这条别忘了。 */
int cg_ring_init(cg_ring_t *r, int cap)
{
    if (!r) return 0;
    r->buf = NULL;
    r->cap = 0;
    r->head = 0;
    r->count = 0;
    r->dropped = 0;
    if (cap <= 0) return 0;
    r->buf = (unsigned char *)malloc((size_t)cap);
    if (!r->buf) return 0;
    r->cap = cap;
    return 1;
}

/** @brief 释放（幂等；NULL 安全） */
void cg_ring_free(cg_ring_t *r)
{
    if (!r) return;
    free(r->buf);
    r->buf = NULL;
    r->cap = 0;
    r->head = 0;
    r->count = 0;
    r->dropped = 0;
}

/** @brief 写入 len 字节，返回实际写入。满了丢最老的；一次就超过容量时只留最后 cap 个字节 */
int cg_ring_push(cg_ring_t *r, const void *data, int len)
{
    const unsigned char *p = (const unsigned char *)data;
    int tail, i;

    if (!r || !r->buf || r->cap <= 0 || !data || len <= 0) return 0;

    /* 一次写进来的就超过容量：前面的直接算丢掉，只保留尾部 cap 个字节 */
    if (len >= r->cap) {
        r->dropped += (long long)r->count + (long long)(len - r->cap);
        memcpy(r->buf, p + (len - r->cap), (size_t)r->cap);
        r->head = 0;
        r->count = r->cap;
        return len;
    }

    /* 先腾地方：该丢几个最老的 */
    if (r->count + len > r->cap) {
        int drop = r->count + len - r->cap;
        r->head = (r->head + drop) % r->cap;
        r->count -= drop;
        r->dropped += drop;
    }

    tail = (r->head + r->count) % r->cap;
    for (i = 0; i < len; i++) {
        r->buf[tail] = p[i];
        tail = (tail + 1) % r->cap;
    }
    r->count += len;
    return len;
}

/** @brief 取出最多 cap 字节，返回实际取出（先进先出） */
int cg_ring_pop(cg_ring_t *r, void *out, int cap)
{
    unsigned char *p = (unsigned char *)out;
    int n, i, head;

    if (!r || !r->buf || !r->count || !out || cap <= 0) return 0;
    n = (cap < r->count) ? cap : r->count;
    head = r->head;
    for (i = 0; i < n; i++) {
        p[i] = r->buf[head];
        head = (head + 1) % r->cap;
    }
    r->head = head;
    r->count -= n;
    return n;
}

/** @brief 可读字节数 */
int cg_ring_count(const cg_ring_t *r)
{
    return (r && r->buf) ? r->count : 0;
}

/** @brief 清空（dropped 是"历史上一共丢了多少"的累计，清空不清它） */
void cg_ring_clear(cg_ring_t *r)
{
    if (!r) return;
    r->head = 0;
    r->count = 0;
}
