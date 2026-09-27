/**
 * @file    cg_win_diag.c
 * @brief   诊断：日志文件落点、致命错误弹窗、未处理异常兜底
 * @details 发布版是 -mwindows（没有控制台），原来那些 "[cstackgui] ..." 提示全走 stderr，
 *          用户一个字都看不到：界面文件没放对、回调名写错、程序崩了 —— 表现都是
 *          "双击以后什么都没发生"。这里补三件事：
 *            1) 日志落到 %LOCALAPPDATA%\<exe名>\app.log（用户目录一定可写，exe 目录常常不可写）
 *            2) 致命错误弹一个中文 MessageBox，并告诉用户日志在哪
 *            3) SetUnhandledExceptionFilter 兜住崩溃，至少留一行日志 + 一次提示
 */
#include "cg_win_internal.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>      /* wcsrchr / _snwprintf */

/** @brief 取 %LOCALAPPDATA%（UTF-8）；拿不到写空串 */
/**
 * @brief 取本机用户数据目录（UTF-8）；拿不到写空串
 * @param out 输出缓冲；写 UTF-8 路径，保证以 NUL 结尾
 * @param out_size out 的容量（字节）
 * @return void 无返回值（环境变量缺失或缓冲不足时 out 为空串）
 */
static void diag_localappdata(char *out, int out_size)
{
    wchar_t wbuf[1024];
    DWORD   n = GetEnvironmentVariableW(L"LOCALAPPDATA", wbuf,
                                       (DWORD)(sizeof(wbuf) / sizeof(wbuf[0])));
    out[0] = '\0';
    if (n == 0 || n >= (DWORD)(sizeof(wbuf) / sizeof(wbuf[0]))) return;
    cgw_utf8_into(wbuf, out, out_size);   /* 宽->窄转换统一在 cg_win_util.c */
}

/** @brief 取路径的文件名部分（兼容 \\ 与 /） */
/**
 * @brief 取路径的文件名部分（兼容反斜杠与斜杠）
 * @param path 待切分的路径（窄字符）；调用方保证非空
 * @return const char * 指向文件名起始处；指向 path 内部，调用方不得释放
 */
static const char *diag_base(const char *path)
{
    const char *s1 = strrchr(path, '\\');
    const char *s2 = strrchr(path, '/');
    if (s2 > s1) s1 = s2;
    return s1 ? s1 + 1 : path;
}

/** @brief 未处理异常：写日志 + 弹一次，然后让进程照常结束 */
/**
 * @brief 未处理异常：写日志 + 弹一次，然后让进程照常结束
 * @param ep 异常上下文；提供异常码、出错地址与访问冲突参数
 * @return LONG 固定返回"已处理"，交回系统继续终结进程
 */
static LONG WINAPI diag_on_crash(EXCEPTION_POINTERS *ep)
{
    DWORD code = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionCode : 0;
    void *addr = (ep && ep->ExceptionRecord) ? ep->ExceptionRecord->ExceptionAddress : NULL;
    char  msg[640];
    char  mod[512] = "(unknown)";
    HMODULE hmod;

    if (addr && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                   (LPCSTR)addr, &hmod)) {
        DWORD len = GetModuleFileNameA(hmod, mod, sizeof(mod) - 1);
        if (len > 0) mod[len] = '\0';
        FreeLibrary(hmod);
    }

    if (code == 0xC0000005 && ep && ep->ExceptionRecord &&
        ep->ExceptionRecord->NumberParameters >= 2) {
        /* AV：参数0=操作(0读/1写/8执行)，参数1=被访问的非法地址 */
        ULONG_PTR op = ep->ExceptionRecord->ExceptionInformation[0];
        void *bad = (void *)ep->ExceptionRecord->ExceptionInformation[1];
        const char *opname = (op == 1) ? "写" : (op == 8) ? "执行" : "读";
        cg_log(CG_LOG_ERROR, "访问冲突：尝试%s 地址 %p（出错地址 %p，模块 %s）",
               opname, bad, addr, mod);
    }

    /* 栈回溯：定位崩溃发生在哪个调用链里（尤其第三方模块内部） */
    {
        void     *frames[40];
        char      mbuf[512];
        USHORT    n = CaptureStackBackTrace(0, 40, frames, NULL);
        cg_log(CG_LOG_ERROR, "调用栈(%u 帧)：", (unsigned)n);
        for (USHORT i = 0; i < n; i++) {
            HMODULE hm = NULL;
            const char *name = "(?)";
            DWORD64 off = 0;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                   (LPCSTR)frames[i], &hm) && hm) {
                DWORD l = GetModuleFileNameA(hm, mbuf, sizeof(mbuf) - 1);
                if (l > 0) { mbuf[l] = '\0'; name = diag_base(mbuf); }
                off = (DWORD64)((char *)frames[i] - (char *)hm);
                FreeLibrary(hm);
            }
            cg_log(CG_LOG_ERROR, "  #%02u %s + 0x%llX", i, name, (unsigned long long)off);
        }
    }

    cg_log(CG_LOG_ERROR, "程序异常退出：code=0x%08lX addr=%p module=%s",
           (unsigned long)code, addr, mod);
    snprintf(msg, sizeof(msg),
             "程序遇到异常，即将退出。\n\n"
             "异常码：0x%08lX\n出错地址：%p\n出错模块：%s\n\n"
             "日志文件：\n%s",
             (unsigned long)code, addr, mod, cg_log_file() ? cg_log_file() : "(未启用)");
    cg_fatal_dialog("CStackGUI 程序异常", msg);
    return EXCEPTION_EXECUTE_HANDLER;
}

/** @brief 弹一个阻塞式错误框（UTF-8 入参）。给 core 用的致命错误出口 */
/**
 * @brief 弹一个阻塞式错误框（UTF-8 入参）；给运行时内核用的致命错误出口
 * @param title 标题栏文字（UTF-8）；NULL 时用 "CStackGUI"
 * @param text 正文文字（UTF-8）；NULL 时用空串
 * @return void 无返回值（阻塞到用户点确定）
 */
void cg_fatal_dialog(const char *title, const char *text)
{
    wchar_t *wt = cgw_wc_from_utf8(title ? title : "CStackGUI");
    wchar_t *wb = cgw_wc_from_utf8(text ? text : "");
    if (wb) {
        MessageBoxW(NULL, wb, wt ? wt : L"CStackGUI",
                    MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TOPMOST);
    }
    free(wt);
    free(wb);
}

/** @brief 启动诊断：定位日志文件、必要时轮转、装异常兜底、记一行启动信息 */
/**
 * @brief 启动诊断：定位日志文件、必要时轮转、装异常兜底、记一行启动信息
 * @return void 无返回值
 */
void cg_diag_init(void)
{
    char  exe[512] = { 0 }, path[1500] = { 0 }, base[1024] = { 0 }, stem[128] = { 0 };
    char *dot;
    int   have_exe = (cg_exe_path(exe, sizeof(exe)) == 0 && exe[0]);

    if (have_exe) {
        snprintf(stem, sizeof(stem), "%s", diag_base(exe));
        dot = strrchr(stem, '.');
        if (dot) *dot = '\0';
    }

    diag_localappdata(base, (int)sizeof(base));
    if (base[0] && stem[0]) {
        wchar_t *wdir;
        wchar_t  wbuf[1400];
        snprintf(path, sizeof(path), "%s\\%s\\app.log", base, stem);
        /* 目录可能还没有：建一次（父目录 %LOCALAPPDATA% 一定在） */
        wdir = cgw_wc_from_utf8(path);
        if (wdir) {
            wchar_t *sl;
            _snwprintf(wbuf, sizeof(wbuf) / sizeof(wbuf[0]), L"%ls", wdir);
            sl = wcsrchr(wbuf, L'\\');
            if (sl) { *sl = L'\0'; CreateDirectoryW(wbuf, NULL); }
            free(wdir);
        }
    } else if (have_exe) {
        /* 拿不到 LOCALAPPDATA 就退到 exe 旁边 */
        snprintf(path, sizeof(path), "%s.app.log", exe);
    }

    if (path[0]) {
        /* 超过 256KB 轮转一次，日志不会无限长大 */
        FILE *probe = fopen(path, "rb");
        if (probe) {
            long sz;
            fseek(probe, 0, SEEK_END);
            sz = ftell(probe);
            fclose(probe);
            if (sz > 256 * 1024) {
                char     old_path[1520];
                wchar_t *wn, *wo;
                snprintf(old_path, sizeof(old_path), "%s.old", path);
                wn = cgw_wc_from_utf8(path);
                wo = cgw_wc_from_utf8(old_path);
                if (wn && wo) MoveFileExW(wn, wo, MOVEFILE_REPLACE_EXISTING);
                free(wn);
                free(wo);
            }
        }
        cg_log_open(path);
    }

    SetUnhandledExceptionFilter(diag_on_crash);
    cg_log(CG_LOG_INFO, "启动：%s", have_exe ? exe : "(未知路径)");
    if (path[0]) cg_log(CG_LOG_INFO, "日志文件：%s", path);
}
