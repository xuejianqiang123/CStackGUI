/**
 * @file    cg_serial.c
 * @brief   串口（cg_serial_*）：打开一条串口链路 + 读线程 + 接收缓冲
 * @details 为什么要有它：这套东西最常被用来的场景就是"跟一块板子说话"。没有它，业务就得自己
 *          include windows.h 去 CreateFile("\\.\COMx")，"只 include cstackgui.h"的约定会破。
 *
 *          **线程模型（唯一被支持的用法，也是唯一容易出事的地方）**：
 *          cg_serial_open() 起一个读线程，它只做一件事 —— 把驱动里的字节搬进一把临界区保护的
 *          环形缓冲（满了丢最老的，见 core/cg_ringbuf.c）。**业务绝不能在读线程里碰控件**：
 *          正确写法是在 cg_set_timer 的时钟回调里 cg_serial_read() 取数据，再 cg_term_write()
 *          灌进终端控件。示例见 examples/serialterm。
 *
 *          读用**同步 ReadFile + 超时**而不是 overlapped：少一层 CancelIo / GetOverlappedResult
 *          的时序坑，代价是关端口时最多等一个读超时周期（这里配成约 50ms）。
 *
 *          v1 的边界（有意不做）：一个句柄一条线；不自动重连；写是同步的（带超时，
 *          不是写队列）。要动这些改的是本文件，不是界面。
 */
#define UNICODE
#define _UNICODE

#include "cg_internal.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** 接收缓冲上限：满了丢最老的（与终端控件的行数上限同一个策略） */
#define SER_RX_CAP      (64 * 1024)
/** 一次最多从驱动读多少 */
#define SER_CHUNK       4096
/** 读间隔超时（毫秒）：没数据时每约 50ms 返回一次，于是读线程能及时看到"停"标志 */
#define SER_RD_INTERVAL 5
#define SER_RD_TOTAL    50

struct cg_serial_t {
    HANDLE           h;            /* 串口句柄（单测里也可能是文件句柄） */
    HANDLE           ev_stop;      /* 通知读线程退出 */
    HANDLE           th;
    CRITICAL_SECTION lock;         /* 保护 rx 与两个计数器、err */
    cg_ring_t        rx;
    long long        rx_total;
    long long        tx_total;
    char             port[64];     /* 只用于日志与错误信息 */
    char             err[192];
};

/** 最近一次"没有句柄可挂"的失败原因（参数不合法、cg_serial_open 打不开端口）。
 *  只有主线程会读它，不做同步 —— 界面上顺手一问就是这个。 */
static char g_open_err[192];

/* ------------------------------------------------------------ 内部助手 */

/**
 * @brief UTF-8 -> UTF-16 堆串
 * @param s 待转换的 UTF-8 字符串，NULL 直接失败
 * @return wchar_t * 新分配的宽串，由**调用方 free**；s 为 NULL、转换失败或内存不足返回 NULL
 */
static wchar_t *ser_wide(const char *s)
{
    int      n;
    wchar_t *out;

    if (!s) return NULL;
    n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    out = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!out) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, out, n);
    return out;
}

/**
 * @brief UTF-16 -> UTF-8（含结尾 \0）写进调用方缓冲
 * @param ws 源宽串，NULL 直接失败
 * @param out 目标缓冲，由调用方提供并保证至少 cap 字节可写
 * @param cap 目标缓冲容量（字节），必须 > 0
 * @return int 1 = 转换成功；0 = 参数不合法或转换失败
 */
static int ser_u8(const wchar_t *ws, char *out, int cap)
{
    if (!ws || !out || cap <= 0) return 0;
    return WideCharToMultiByte(CP_UTF8, 0, ws, -1, out, cap, NULL, NULL) > 0;
}

/**
 * @brief 把几个最常见的 Win32 错误码翻成人话
 * @param e GetLastError() 取到的 Win32 错误码
 * @return const char * 静态字符串，**调用方不要 free**；认不出来的码返回"系统错误"
 * @note 不引 FormatMessage：那要再转一次编码，而这几个码覆盖了实际会遇到的大多数
 */
static const char *ser_why(DWORD e)
{
    switch (e) {
    case ERROR_FILE_NOT_FOUND:    return "找不到这个端口";
    case ERROR_PATH_NOT_FOUND:    return "找不到这个端口";
    case ERROR_ACCESS_DENIED:     return "端口被占用（可能别的程序已经打开了它）";
    case ERROR_INVALID_NAME:      return "端口名不合法";
    case ERROR_BAD_COMMAND:       return "设备不响应（线被拔了？）";
    case ERROR_OPERATION_ABORTED: return "操作被中止";
    case ERROR_SEM_TIMEOUT:       return "设备超时（波特率对不对？线接好了吗？）";
    case ERROR_INVALID_HANDLE:    return "句柄已失效（端口被关掉或设备被拔）";
    default:                      return "系统错误";
    }
}

/**
 * @brief 记一条错误：写进链路（没有链路就写全局）+ 写日志
 * @param s 目标链路，NULL 表示只写全局 g_open_err（没有链路可挂时用）
 * @param what 动作的简短中文描述，拼在系统错误码前面
 * @return void 无返回值
 * @note 先在栈上拼好整条消息，再一次性拷进目标缓冲 —— 这样任何时刻那个缓冲都是"以 \0 结尾的
 *       完整旧串或完整新串"，主线程随手读也不会读到半个字符串
 */
static void ser_fail(cg_serial_t *s, const char *what)
{
    DWORD e = GetLastError();
    char  msg[192];

    snprintf(msg, sizeof(msg), "%s：%s（系统错误 %lu）", what, ser_why(e), (unsigned long)e);
    if (s) {
        EnterCriticalSection(&s->lock);
        memcpy(s->err, msg, strlen(msg) + 1);
        LeaveCriticalSection(&s->lock);
        cg_log(CG_LOG_ERROR, "串口 %s %s", s->port, msg);
    } else {
        memcpy(g_open_err, msg, strlen(msg) + 1);
        cg_log(CG_LOG_ERROR, "串口 %s", msg);
    }
}

/* ------------------------------------------------------------ 读线程 */

/**
 * @brief 读线程：把驱动里的字节搬进接收缓冲，**只做这一件事** —— 不碰控件、不碰界面
 * @param p cg_serial_from_handle() 传进来的 cg_serial_t *，借用，不负责释放
 * @return DWORD 恒为 0（CreateThread 的退出码只是形式要求）
 */
static DWORD WINAPI ser_thread(LPVOID p)
{
    cg_serial_t   *s = (cg_serial_t *)p;
    unsigned char  chunk[SER_CHUNK];
    DWORD          backoff = 200;        /* 连续失败的重试间隔：200ms 起指数退避，30s 封顶 */
    ULONGLONG      drop_logged_at = 0;   /* 上次打"缓冲溢出"WARN 的时刻（5s 节流） */
    long long      drop_reported = 0;    /* 上次已经写进日志的丢弃计数快照 */

    for (;;) {
        DWORD      got = 0;
        long long  drop_now;
        ULONGLONG  tnow;

        if (WaitForSingleObject(s->ev_stop, 0) == WAIT_OBJECT_0) break;

        if (!ReadFile(s->h, chunk, SER_CHUNK, &got, NULL)) {
            /* 设备没了（线被拔掉）或端口已经被关：记一笔，按指数退避再看要不要停。
             * 拔线是现场常态故障，固定 200ms 重试 = 每秒 5 条错误把 app.log 刷满
             * （2026-10-17 架构评审"拔线自愈缺失"整改）：200ms 起每失败一次翻倍，
             * 30s 封顶；一旦读到数据立即归零（下面成功路径）。 */
            ser_fail(s, "读取失败");
            if (WaitForSingleObject(s->ev_stop, backoff) == WAIT_OBJECT_0) break;
            backoff = (backoff < 15000) ? backoff * 2 : 30000;
            continue;
        }
        if (backoff > 200) {
            cg_log(CG_LOG_INFO, "串口 %s：读取已恢复", s->port);
            backoff = 200;
        }

        if (!got) {
            /* 超时没数据（或文件句柄读到尾）。串口那边 COMMTIMEOUTS 已经天然限速，
             * 但文件句柄不会等 —— 少了这句 Sleep，单测里那个文件句柄会 100% 占满一个核。 */
            Sleep(5);
            continue;
        }

        EnterCriticalSection(&s->lock);
        cg_ring_push(&s->rx, chunk, (int)got);
        s->rx_total += (long long)got;
        drop_now = s->rx.dropped;
        LeaveCriticalSection(&s->lock);

        /* 缓冲溢出（业务取数跟不上）必须在日志里留痕：精确计数由 cg_serial_dropped()
         * 随时可查，这里按 5s 节流打一条 WARN —— 不节流会反过来把日志淹掉。 */
        tnow = GetTickCount64();
        if (drop_now > drop_reported && tnow - drop_logged_at >= 5000) {
            cg_log(CG_LOG_WARN, "串口 %s：接收缓冲溢出，已累计丢弃 %lld 字节（业务取数跟不上；cg_serial_dropped() 可查精确值）",
                   s->port, drop_now);
            drop_reported = drop_now;
            drop_logged_at = tnow;
        }
    }
    return 0;
}

/* ------------------------------------------------------------ 建立与释放 */

/**
 * @brief 内部：拿一个已经打开好的句柄建链路并起读线程
 * @param handle 已打开的串口 / 文件句柄；成功即所有权移交给链路，之后由 cg_serial_close() 关闭
 * @param name 端口名，只用于日志与错误信息，NULL 记成 "?"
 * @param rx_cap 接收缓冲容量（字节），<= 0 时用默认值；满了丢最老的
 * @return cg_serial_t * 新链路，堆内存由 cg_serial_close() 回收；句柄无效 / 内存不足 / 起线程失败返回 NULL，此时句柄仍归调用方
 */
cg_serial_t *cg_serial_from_handle(void *handle, const char *name, int rx_cap)
{
    cg_serial_t *s;

    if (!handle || handle == INVALID_HANDLE_VALUE) return NULL;

    s = (cg_serial_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->h = (HANDLE)handle;
    snprintf(s->port, sizeof(s->port), "%s", name ? name : "?");
    if (rx_cap <= 0) rx_cap = SER_RX_CAP;

    if (!cg_ring_init(&s->rx, rx_cap)) {
        free(s);
        return NULL;
    }
    InitializeCriticalSection(&s->lock);

    /* 手动重置事件：一旦置位就一直置位（"请停"这个信号不该被读走） */
    s->ev_stop = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!s->ev_stop) {
        DeleteCriticalSection(&s->lock);
        cg_ring_free(&s->rx);
        free(s);
        return NULL;
    }

    s->th = CreateThread(NULL, 0, ser_thread, s, 0, NULL);
    if (!s->th) {
        CloseHandle(s->ev_stop);
        DeleteCriticalSection(&s->lock);
        cg_ring_free(&s->rx);
        free(s);
        return NULL;
    }
    return s;
}

/**
 * @brief 打开串口（固定 8N1，等价 open_ex(port, baud, 8, 1, 0)）
 * @param port 端口名如 "COM3"
 * @param baud 波特率，必须 > 0
 * @return cg_serial_t * 新链路，由 cg_serial_close() 回收；参数不合法或打不开返回 NULL，原因用 cg_serial_last_error(NULL) 取
 */
cg_serial_t *cg_serial_open(const char *port, int baud)
{
    return cg_serial_open_ex(port, baud, 8, 1, 0);
}

/**
 * @brief 打开串口（全参数版）：配好 DCB 与超时、建链路并起读线程
 * @param port 端口名如 "COM3"
 * @param baud 波特率，必须 > 0
 * @param bits 数据位，只能是 5..8
 * @param stop 停止位，只能是 1..2
 * @param parity 校验位，0=无 1=奇 2=偶
 * @return cg_serial_t * 新链路，由 cg_serial_close() 回收；参数不合法、打不开端口或起线程失败返回 NULL，原因用 cg_serial_last_error(NULL) 取
 * @note 参数不合法时**干净失败** —— 不合法的值不往驱动送（半开着的端口比打不开更坑人）
 */
cg_serial_t *cg_serial_open_ex(const char *port, int baud, int bits, int stop, int parity)
{
    HANDLE      h;
    wchar_t    *wp;
    DCB         dcb;
    COMMTIMEOUTS tmo;
    cg_serial_t *s;
    char        path[80];
    const char *parity_txt[3] = { "N", "O", "E" };

    if (!port || !*port) {
        snprintf(g_open_err, sizeof(g_open_err), "端口名是空的");
        cg_log(CG_LOG_ERROR, "串口打开失败：端口名是空的");
        return NULL;
    }
    if (baud <= 0) {
        snprintf(g_open_err, sizeof(g_open_err), "波特率不合法（%d）", baud);
        cg_log(CG_LOG_ERROR, "串口 %s 打开失败：波特率不合法（%d）", port, baud);
        return NULL;
    }
    if (bits < 5 || bits > 8) {
        snprintf(g_open_err, sizeof(g_open_err), "数据位不合法（%d，只能是 5..8）", bits);
        cg_log(CG_LOG_ERROR, "串口 %s 打开失败：%s", port, g_open_err);
        return NULL;
    }
    if (stop < 1 || stop > 2) {
        snprintf(g_open_err, sizeof(g_open_err), "停止位不合法（%d，只能是 1..2）", stop);
        cg_log(CG_LOG_ERROR, "串口 %s 打开失败：%s", port, g_open_err);
        return NULL;
    }
    if (parity < 0 || parity > 2) {
        snprintf(g_open_err, sizeof(g_open_err), "校验位不合法（%d，0=无 1=奇 2=偶）", parity);
        cg_log(CG_LOG_ERROR, "串口 %s 打开失败：%s", port, g_open_err);
        return NULL;
    }

    /* 设备名一律走 "\\.\" 前缀：不然 "COM10" 以上会被当成文件名 */
    if (port[0] == '\\') snprintf(path, sizeof(path), "%s", port);
    else                 snprintf(path, sizeof(path), "\\\\.\\%s", port);

    wp = ser_wide(path);
    if (!wp) {
        snprintf(g_open_err, sizeof(g_open_err), "端口名编码转换失败");
        return NULL;
    }
    h = CreateFileW(wp, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        ser_fail(NULL, "打开失败");
        free(wp);
        return NULL;
    }
    free(wp);

    SetupComm(h, SER_RX_CAP, 4096);

    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb)) {
        ser_fail(NULL, "读取端口参数失败");
        CloseHandle(h);
        return NULL;
    }
    dcb.BaudRate = (DWORD)baud;
    dcb.fBinary = TRUE;
    dcb.fParity = (parity != 0) ? TRUE : FALSE;   /* 开校验才让驱动去验 */
    dcb.ByteSize = (BYTE)bits;
    dcb.Parity = (BYTE)(parity == 1 ? ODDPARITY : parity == 2 ? EVENPARITY : NOPARITY);
    dcb.StopBits = (BYTE)(stop == 2 ? TWOSTOPBITS : ONESTOPBIT);
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fAbortOnError = FALSE;
    if (!SetCommState(h, &dcb)) {
        ser_fail(NULL, "设置端口参数失败（波特率支持吗）");
        CloseHandle(h);
        return NULL;
    }

    /* 读超时：没数据时约 50ms 返回一次（读线程靠这个周期看"停"标志，关端口也就最多等这么久） */
    memset(&tmo, 0, sizeof(tmo));
    tmo.ReadIntervalTimeout = SER_RD_INTERVAL;
    tmo.ReadTotalTimeoutMultiplier = 0;
    tmo.ReadTotalTimeoutConstant = SER_RD_TOTAL;
    tmo.WriteTotalTimeoutMultiplier = 0;
    tmo.WriteTotalTimeoutConstant = 1000;
    SetCommTimeouts(h, &tmo);

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    s = cg_serial_from_handle(h, port, SER_RX_CAP);
    if (!s) {
        snprintf(g_open_err, sizeof(g_open_err), "建立读线程失败");
        CloseHandle(h);
        return NULL;
    }
    cg_log(CG_LOG_INFO, "串口 %s 已打开（%d %d%s%d）", port, baud, bits,
           parity_txt[parity], stop);
    return s;
}

/* ------------------------------------------------------------ 串口组件（serial 控件）
 * 无界面控件的控件侧 API：参数在控件上（cg_internal.h 的 sport/s_baud...），
 * 打开后的 cg_serial_t* 放 native2。放在 kit 是因为 cg_serial_* 在这层；
 * 到达轮询的起停走 cg_win_restart_serial（与 cg_widget_set_period 调
 * cg_win_restart_timer 同一条先例路线）。 */

/**
 * @brief 取串口组件的链路句柄
 * @param w 串口组件
 * @return cg_serial_t * 借用的链路，**归控件所有、调用方不要关**；未打开或类型不对返回 NULL
 */
cg_serial_t *cg_widget_serial(cg_widget_t *w)
{
    if (!w || !w->type || strcmp(w->type, "serial") != 0) return NULL;
    return (cg_serial_t *)w->native2;
}

/**
 * @brief 按控件属性打开链路（端口名可省略 = 用控件里配的）；已打开则先关旧的再开
 * @param w 串口组件
 * @param port 端口名，NULL 或空串表示用控件里配的端口；非空时会回写控件与节点
 * @return int 0 = 成功（链路存进控件，归控件所有，由 cg_widget_serial_close() 关闭）；-1 = 类型不对、没配端口或打开失败
 */
int cg_widget_serial_open(cg_widget_t *w, const char *port)
{
    cg_serial_t *s;

    if (!w || !w->type || strcmp(w->type, "serial") != 0) return -1;

    /* 重连 = 先关旧的（换端口 / 换波特率都靠"再开一次"表达） */
    if (w->native2) {
        cg_serial_close((cg_serial_t *)w->native2);
        w->native2 = NULL;
    }

    if (port && *port) {
        snprintf(w->sport, sizeof(w->sport), "%s", port);
        if (w->node) {                      /* 回写节点：dump/自检看到的是最新值 */
            free(w->node->sport);
            w->node->sport = cg_str_dup(port);
        }
    }
    if (!w->sport[0]) {
        cg_log(CG_LOG_WARN, "串口组件 #%s 没配端口，打开不了", w->id ? w->id : "-");
        return -1;
    }

    s = cg_serial_open_ex(w->sport, w->s_baud, w->s_bits, w->s_stop, w->s_parity);
    if (!s) return -1;                      /* 原因 cg_serial_open_ex 已记日志 */

    w->native2 = s;
    cg_win_restart_serial(w);               /* 起到达轮询 */
    return 0;
}

/**
 * @brief 关掉串口组件的链路（幂等、NULL 安全）
 * @param w 串口组件
 * @return void 无返回值
 */
void cg_widget_serial_close(cg_widget_t *w)
{
    if (!w) return;
    if (w->native2) {
        cg_serial_close((cg_serial_t *)w->native2);
        w->native2 = NULL;
    }
    cg_win_restart_serial(w);               /* 停轮询（native2 空时它只 KillTimer） */
}

/** @brief 参数要变了：先把开着的链路摘下来
 *  @param w 串口组件
 *  @return int 1 = 刚才开着（调用方改完属性要用 cg_widget_serial_open(w, NULL) 补回来）
 *  @note 先摘再改：cg_widget_serial_open() 自己也会关旧的，但那时属性已是新值，
 *        中途失败会留下"旧链路按新参数"的错觉。这里干脆先停干净。
 */
static int serial_take_off(cg_widget_t *w)
{
    int was_open = (w->native2 != NULL);

    if (w->native2) {
        cg_serial_close((cg_serial_t *)w->native2);
        w->native2 = NULL;
    }
    return was_open;
}

/**
 * @brief 改波特率：链路开着就按新参数原样重开，没开就只记住
 * @param w 串口组件
 * @param baud 新波特率，必须 > 0；与当前值相同时什么都不做
 * @return void 无返回值
 */
void cg_widget_serial_set_baud(cg_widget_t *w, int baud)
{
    int was_open;

    if (!w || baud <= 0 || baud == w->s_baud) return;

    was_open = serial_take_off(w);
    w->s_baud = baud;
    if (w->node) w->node->s_baud = baud;    /* 节点跟着改，dump/自检看到的是新值 */

    if (was_open) cg_widget_serial_open(w, NULL);
}

/**
 * @brief 改数据位 / 停止位 / 校验：同上（三样一起给，省得改一次重开一次）
 * @param w 串口组件
 * @param bits 数据位，只能是 5..8
 * @param stop 停止位，只能是 1..2
 * @param parity 校验位，0=无 1=奇 2=偶
 * @return void 无返回值
 */
void cg_widget_serial_set_format(cg_widget_t *w, int bits, int stop, int parity)
{
    int was_open;

    if (!w || !w->type || strcmp(w->type, "serial") != 0) return;
    if (bits < 5 || bits > 8 || stop < 1 || stop > 2 || parity < 0 || parity > 2) {
        /* 越界就整个忽略：只改一半会造出"7 数据位 + 无校验"这种谁也没要的组合 */
        cg_log(CG_LOG_WARN, "串口组件 #%s：数据位/停止位/校验不合法（%d/%d/%d），已忽略",
               w->id ? w->id : "-", bits, stop, parity);
        return;
    }
    if (bits == w->s_bits && stop == w->s_stop && parity == w->s_parity) return;

    was_open = serial_take_off(w);
    w->s_bits   = bits;
    w->s_stop   = stop;
    w->s_parity = parity;
    if (w->node) {                          /* 节点跟着改，dump/自检看到的是新值 */
        w->node->s_bits   = bits;
        w->node->s_stop   = stop;
        w->node->s_parity = parity;
    }

    if (was_open) cg_widget_serial_open(w, NULL);
}

/**
 * @brief 关掉串口并回收读线程（**NULL 安全，但不是幂等**）
 * @param s 要关的链路，必须来自 cg_serial_open() / cg_serial_from_handle()；除了 NULL，别的句柄不能重复关
 * @return void 无返回值
 * @note 同一个句柄关两次是 use-after-free（实测堆损坏退出），关完请立刻把变量置空
 */
void cg_serial_close(cg_serial_t *s)
{
    if (!s) return;

    /* 顺序很重要：**先请读线程停、等它真的退出，再关句柄、再拆缓冲**。
     * 反过来就是"读线程还在往一个已经释放的缓冲里写"—— 表现是偶发崩溃或数据错乱。 */
    if (s->th) {
        DWORD wr;
        SetEvent(s->ev_stop);
        wr = WaitForSingleObject(s->th, 3000);   /* 最坏一个读超时周期；超时了也不能强杀线程 */
        if (wr != WAIT_OBJECT_0) {
            /* 读线程没出来（典型：坏 USB 驱动让 ReadFile 挂死超过 3 秒）。此刻继续拆
             * 缓冲 / 锁 / 句柄 = 给一个还活着的线程准备 use-after-free（2026-10-17 架构
             * 评审"close 的 3 秒幻觉"整改）：宁可把整份链路泄漏到进程结束（由系统回收），
             * 也绝不释放一个还可能被写的内存。 */
            cg_log(CG_LOG_ERROR, "串口 %s：关闭时读线程未退出（>3s），放弃回收以避免崩溃；本链路泄漏至进程退出", s->port);
            return;
        }
        CloseHandle(s->th);
    }
    if (s->ev_stop) CloseHandle(s->ev_stop);
    if (s->h) CloseHandle(s->h);
    DeleteCriticalSection(&s->lock);
    cg_ring_free(&s->rx);
    free(s);
}

/* ------------------------------------------------------------ 收发 */

/**
 * @brief 发送数据（同步写、带写超时，不是写队列）
 * @param s 链路
 * @param data 要发的数据，调用方保留所有权，函数只读不释放
 * @param len 要发的字节数，必须 > 0
 * @return int 实际写出的字节数（可能小于 len）；-1 = 参数不合法、链路没有句柄或写失败
 */
int cg_serial_write(cg_serial_t *s, const void *data, int len)
{
    DWORD wrote = 0;

    if (!s || !data || len <= 0) return -1;
    if (!s->h) return -1;

    if (!WriteFile(s->h, data, (DWORD)len, &wrote, NULL)) {
        ser_fail(s, "写入失败");
        return -1;
    }
    EnterCriticalSection(&s->lock);
    s->tx_total += (long long)wrote;
    LeaveCriticalSection(&s->lock);
    return (int)wrote;
}

/**
 * @brief 从接收缓冲取最多 cap 字节（非阻塞，没数据立刻返回）
 * @param s 链路
 * @param buf 目标缓冲，由调用方提供并保证至少 cap 字节可写
 * @param cap 本次最多取多少字节，必须 > 0
 * @return int 实际取到的字节数，可能是 0；参数不合法时返回 0
 */
int cg_serial_read(cg_serial_t *s, void *buf, int cap)
{
    int n;

    if (!s || !buf || cap <= 0) return 0;
    EnterCriticalSection(&s->lock);
    n = cg_ring_pop(&s->rx, buf, cap);
    LeaveCriticalSection(&s->lock);
    return n;
}

/**
 * @brief 取接收缓冲里还有多少字节没被取走
 * @param s 链路
 * @return int 未取走的字节数；s 为 NULL 时返回 0
 */
int cg_serial_available(const cg_serial_t *s)
{
    int n;

    if (!s) return 0;
    EnterCriticalSection((CRITICAL_SECTION *)&s->lock);
    n = cg_ring_count(&s->rx);
    LeaveCriticalSection((CRITICAL_SECTION *)&s->lock);
    return n;
}

/**
 * @brief 丢掉接收缓冲里还没取走的数据
 * @param s 链路
 * @return void 无返回值
 */
void cg_serial_flush_rx(cg_serial_t *s)
{
    if (!s) return;
    EnterCriticalSection(&s->lock);
    cg_ring_clear(&s->rx);
    LeaveCriticalSection(&s->lock);
}

/**
 * @brief 取链路累计收到的字节数
 * @param s 链路
 * @return long long 自打开以来收到的总字节数；s 为 NULL 时返回 0
 */
long long cg_serial_rx_bytes(const cg_serial_t *s)
{
    return s ? s->rx_total : 0;
}

/**
 * @brief 取链路累计发出的字节数
 * @param s 链路
 * @return long long 自打开以来发出的总字节数；s 为 NULL 时返回 0
 */
long long cg_serial_tx_bytes(const cg_serial_t *s)
{
    return s ? s->tx_total : 0;
}

/**
 * @brief 取接收缓冲因溢出累计丢弃的字节数
 * @param s 链路
 * @return long long 自打开以来丢弃的总字节数（业务取数跟不上时才 > 0）；s 为 NULL 时返回 0
 * @note 缓冲满时丢最老的字节，计数只增不减；读线程同时会按 5s 节流往日志写 WARN
 */
long long cg_serial_dropped(const cg_serial_t *s)
{
    long long n;

    if (!s) return 0;
    EnterCriticalSection((CRITICAL_SECTION *)&s->lock);
    n = s->rx.dropped;
    LeaveCriticalSection((CRITICAL_SECTION *)&s->lock);
    return n;
}

/**
 * @brief 取最后一次错误的中文描述
 * @param s 链路，NULL 表示问"最近一次打开失败"的原因
 * @return const char * 常驻缓冲里的字符串，**调用方不要 free**；还没出过错时是空串
 */
const char *cg_serial_last_error(const cg_serial_t *s)
{
    if (!s) return g_open_err;
    return s->err;
}

/* ------------------------------------------------------------ 端口枚举 */

/**
 * @brief 枚举本机串口，每行一个端口名（形如 "COM3\nCOM7\n"）
 * @param buf 输出缓冲，由调用方提供并保证至少 cap 字节可写
 * @param cap 输出缓冲容量（字节），必须 > 1
 * @return int 写入的字节数（不含结尾 \0）；参数不合法、本机一个串口都没有，或一行都装不下时返回 0
 */
int cg_serial_list(char *buf, int cap)
{
    HKEY  key;
    DWORD i = 0;
    int   used = 0;

    if (!buf || cap <= 1) return 0;
    buf[0] = '\0';

    /* 注册表这个键是"设备路径 -> 端口名"的映射：值名像 \Device\Serial0，值就是 COM3。
     * 一台机器上一个串口都没有时这个键可能根本不存在 —— 那是正常情况，不是错误。 */
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DEVICEMAP\\SERIALCOMM", 0,
                      KEY_READ, &key) != ERROR_SUCCESS) {
        return 0;
    }

    for (;;) {
        wchar_t name[256];
        wchar_t val[128];
        DWORD   nlen = 256, vlen = sizeof(val), type = 0;
        char    port[128];

        if (RegEnumValueW(key, i++, name, &nlen, NULL, &type,
                          (BYTE *)val, &vlen) != ERROR_SUCCESS) break;
        if (type != REG_SZ || vlen < 2) continue;
        if (!ser_u8(val, port, (int)sizeof(port))) continue;
        if (port[0] == '\0') continue;
        if (used + (int)strlen(port) + 2 > cap) break;      /* 装不下了：宁可少列也不截断半行 */
        used += snprintf(buf + used, (size_t)(cap - used), "%s\n", port);
    }
    RegCloseKey(key);
    return used;
}
