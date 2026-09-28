/**
 * @file    cg_kit_sys.c
 * @brief   kit 系统能力：剪贴板、外部程序（打开/启动）、当前时间、HKCU 注册表、系统信息
 * @details 从 cg_kit.c 按子系统纯搬移拆出（公共 API 未变）；跨文件复用的字符助手
 *          声明见 cg_kit_internal.h。
 *
 *          末尾"系统信息"那一段（2026-09-27 加）是"从精易模块 系统_ 一族学功能"的产物：
 *          那一族 254 条里，头 34 条就是"每个环境变量一个命令"
 *          （系统_COMPUTERNAME / 系统_TEMP / 系统_WINDIR …）—— 易语言没有 getenv 的产物，
 *          在 C 侧首先是**一个** cg_env()。哪些不抄、以及"不引入新链接库"这条约束的来由
 *          （含一次被实验推翻的判断），见 .codebuddy/plans/kit-sysinfo.md。
 */
#include "cg_kit_internal.h"

#include <shellapi.h>       /* ShellExecuteW */
#include <tlhelp32.h>       /* CreateToolhelp32Snapshot / PROCESSENTRY32W（查进程在不在） */
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------ 剪贴板 / 外部程序 / 时间 */

/**
 * @brief 取剪贴板文本
 * @return char * 堆串，用完 cg_free；剪贴板里没有文本（或打不开剪贴板）返回 NULL
 */
char *cg_clip_get_text(void)
{
    char *out = NULL;

    if (!OpenClipboard(NULL)) return NULL;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (h) {
            wchar_t *w = (wchar_t *)GlobalLock(h);
            if (w) {
                out = kit_u8(w);
                GlobalUnlock(h);
            }
        }
    }
    CloseClipboard();
    return out;
}

/**
 * @brief 写剪贴板文本
 * @param text 要写进去的 UTF-8 文本；NULL 直接失败
 * @return int 0 成功；-1 参数为空、编码转换失败或打不开剪贴板
 */
int cg_clip_set_text(const char *text)
{
    wchar_t *w;
    size_t   bytes;
    HGLOBAL  h;
    void    *p;
    int      ok = -1;

    if (!text) return -1;
    w = kit_w(text);
    if (!w) return -1;
    bytes = (wcslen(w) + 1) * sizeof(wchar_t);
    h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        p = GlobalLock(h);
        if (p) {
            memcpy(p, w, bytes);
            GlobalUnlock(h);
            if (OpenClipboard(NULL)) {
                EmptyClipboard();
                if (SetClipboardData(CF_UNICODETEXT, h)) ok = 0;
                CloseClipboard();
            }
            if (ok != 0) GlobalFree(h);
        } else {
            GlobalFree(h);
        }
    }
    free(w);
    return ok;
}

/**
 * @brief 用系统默认程序打开文件 / 文件夹 / 网址
 * @param path_or_url 目标路径或 URL（UTF-8）；NULL 直接失败
 * @return int 0 已被系统接受；-1 参数为空、编码转换失败或系统拒绝
 */
int cg_shell_open(const char *path_or_url)
{
    wchar_t  *w;
    HINSTANCE r;

    if (!path_or_url) return -1;
    w = kit_w(path_or_url);
    if (!w) return -1;
    r = ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
    free(w);
    return ((INT_PTR)r > 32) ? 0 : -1;
}

/**
 * @brief 启动外部程序
 * @param cmd_line 命令行（UTF-8，可带参数）；NULL 直接失败
 * @param wait 非 0 = 等它结束并取退出码；0 = 不等、立刻返回
 * @return int wait 非 0 时是子进程退出码（取不到按 0）；wait 为 0 时是 0；启动失败一律 -1
 */
int cg_run_process(const char *cmd_line, int wait)
{
    wchar_t            *wcmd;
    STARTUPINFOW        si;
    PROCESS_INFORMATION pi;
    int                 rc = 0;

    if (!cmd_line) return -1;
    wcmd = kit_w(cmd_line);
    if (!wcmd) return -1;

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));

    if (!CreateProcessW(NULL, wcmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        free(wcmd);
        return -1;
    }
    free(wcmd);
    if (wait) {
        DWORD code = 0;
        WaitForSingleObject(pi.hProcess, INFINITE);
        if (GetExitCodeProcess(pi.hProcess, &code)) rc = (int)code;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return rc;
}

/** @brief 毫秒时基（系统 uptime）：切帧、超时判定、去抖都用它
 *  @return unsigned long 自系统启动起的毫秒数
 *  @note 用 GetTickCount64 而不是 GetTickCount：后者 49.7 天回绕，长时间运行的上位机会撞上，
 *        而"时间倒退"会让切帧器的空档判定永远不成立（帧再也切不出来）。 */
unsigned long cg_now_ms(void)
{
    return (unsigned long)GetTickCount64();
}

/**
 * @brief 当前时间字符串
 * @param fmt strftime 格式串（如 "%Y-%m-%d"）；NULL 或空串用 "%Y-%m-%d %H:%M:%S"
 * @return char * 堆串，用完 cg_free；取时间失败返回 NULL
 */
char *cg_time_now(const char *fmt)
{
    time_t     t = time(NULL);
    struct tm *tmv = localtime(&t);
    char       buf[128];

    if (!tmv) return NULL;
    strftime(buf, sizeof(buf), fmt && *fmt ? fmt : "%Y-%m-%d %H:%M:%S", tmv);
    return cg_str_dup(buf);
}

/* ------------------------------------------------------------ 注册表 */

/**
 * @brief 读 HKCU 下的字符串值
 * @param subkey 子键路径（如 "Software\\Foo"）；NULL 时直接走回退
 * @param name 值名；NULL 时直接走回退
 * @param def 键 / 值不存在（或不是字符串类型）时的回退内容，允许 NULL（那就返回 NULL）
 * @return char * 堆串，用完 cg_free；取不到时是 def 的副本
 */
char *cg_reg_read(const char *subkey, const char *name, const char *def)
{
    wchar_t *wsub;
    wchar_t *wname;
    wchar_t  wbuf[2048];
    DWORD    size = sizeof(wbuf);
    DWORD    type = 0;
    HKEY     key = NULL;
    char    *out = NULL;

    if (!subkey || !name) return cg_str_dup(def);
    wsub = kit_w(subkey);
    wname = kit_w(name);
    if (wsub && wname &&
        RegOpenKeyExW(HKEY_CURRENT_USER, wsub, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        if (RegQueryValueExW(key, wname, NULL, &type, (LPBYTE)wbuf, &size) == ERROR_SUCCESS &&
            type == REG_SZ) {
            out = kit_u8(wbuf);
        }
        RegCloseKey(key);
    }
    free(wsub);
    free(wname);
    return out ? out : cg_str_dup(def);
}

/**
 * @brief 写 HKCU 下的字符串值（没有就新建）
 * @param subkey 子键路径；NULL 直接失败
 * @param name 值名；NULL 直接失败
 * @param value 要写的字符串；NULL 当空串写
 * @return int 0 成功；-1 参数为空、编码转换失败或注册表操作失败
 */
int cg_reg_write(const char *subkey, const char *name, const char *value)
{
    wchar_t *wsub;
    wchar_t *wname;
    wchar_t *wval;
    HKEY     key = NULL;
    int      ok = -1;
    size_t   bytes;

    if (!subkey || !name) return -1;
    wsub = kit_w(subkey);
    wname = kit_w(name);
    wval = kit_w(value ? value : "");
    if (wsub && wname && wval &&
        RegCreateKeyExW(HKEY_CURRENT_USER, wsub, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS) {
        bytes = (wcslen(wval) + 1) * sizeof(wchar_t);
        if (RegSetValueExW(key, wname, 0, REG_SZ, (const BYTE *)wval, (DWORD)bytes) == ERROR_SUCCESS) ok = 0;
        RegCloseKey(key);
    }
    free(wsub);
    free(wname);
    free(wval);
    return ok;
}

/* ------------------------------------------------------------ 系统信息
 *
 * 来历与取舍见 .codebuddy/plans/kit-sysinfo.md。要点：精易 `系统_` 那 254 条里
 * 头 34 条是"每个环境变量一个命令"，在 C 侧是**一个** cg_env()；
 * 而"取 MAC 地址"要 iphlpapi（实测确实没在链接表里），按"不引入新链接库"的约束不做。
 */

/**
 * @brief 读环境变量（UTF-8）
 * @param name 变量名（不区分大小写）；NULL 或空串返回 NULL
 * @return char * 堆串，用完 cg_free；变量不存在返回 NULL（存在但值为空则返回空串）
 */
char *cg_env(const char *name)
{
    wchar_t *wn;
    wchar_t *buf;
    DWORD    need, got;
    char    *out;

    if (!name || !*name) return NULL;
    wn = kit_w(name);
    if (!wn) return NULL;

    need = GetEnvironmentVariableW(wn, NULL, 0);
    if (need == 0) {
        /* 要区分"没有这个变量"和"有这个变量但值是空的"—— 后者也该给一个空串，
         * 否则调用方会把"设了但是空"当成"没设"。 */
        DWORD err = GetLastError();

        free(wn);
        return (err == ERROR_ENVVAR_NOT_FOUND) ? NULL : cg_str_dup("");
    }
    buf = (wchar_t *)malloc((size_t)need * sizeof(wchar_t));
    if (!buf) {
        free(wn);
        return NULL;
    }
    got = GetEnvironmentVariableW(wn, buf, need);
    free(wn);
    if (got == 0 || got >= need) {          /* 期间被人改长了：当失败，别返回半截 */
        free(buf);
        return NULL;
    }
    out = kit_u8(buf);
    free(buf);
    return out;
}

/**
 * @brief 读环境变量当十进制整数
 * @param name 变量名
 * @param fallback 变量不存在 / 整串不合法 / 超出 int 范围时返回它
 * @return int 解析出的整数；失败时是 fallback
 */
int cg_env_int(const char *name, int fallback)
{
    char     *v = cg_env(name);
    long long n;

    if (!v) return fallback;
    /* 复用进制片做的解析：整串必须合法、溢出算失败。atoi 会把 "abc" 静默当成 0，
     * 那种"错得看不出来"的结果比 fallback 更糟。 */
    if (!cg_base_to_int(v, 10, &n) || n < -2147483647LL - 1 || n > 2147483647LL) {
        cg_free(v);
        return fallback;
    }
    cg_free(v);
    return (int)n;
}

#ifndef PROCESSOR_ARCHITECTURE_ARM64
#define PROCESSOR_ARCHITECTURE_ARM64 12     /* 老 MinGW 头文件里没这个宏 */
#endif

/**
 * @brief 判断这台机器是不是 64 位 Windows
 * @return int 1 = 是（AMD64 / IA64 / ARM64）；0 = 不是
 */
int cg_sys_is64(void)
{
    SYSTEM_INFO si;

    memset(&si, 0, sizeof(si));
    /* GetNativeSystemInfo 给的是"这台机器"的架构，不受 WOW64 影响；
     * IsWow64Process 那套是在问"我是不是被 WOW64 套着"，两回事。 */
    GetNativeSystemInfo(&si);
    return (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ||
            si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64 ||
            si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) ? 1 : 0;
}

/** RTL_OSVERSIONINFOW 的布局（自己声明，免得为了一个结构体去依赖 ddk 头） */
typedef struct {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    WCHAR szCSDVersion[128];
} cg_osver_t;

/**
 * @brief 取操作系统可读名，形如 "Windows 10 (10.0.19045)"
 * @return char * 堆串，用完 cg_free；取不到系统符号或版本查询失败返回 NULL
 * @note 走的是 ntdll 的版本查询，所以不受兼容性清单里的版本谎报影响（与 GetVersionEx 的区别就在这）
 */
char *cg_sys_version(void)
{
    typedef LONG (WINAPI *rtl_get_version_fn)(cg_osver_t *);
    HMODULE            ntdll;
    rtl_get_version_fn fn;
    cg_osver_t         vi;
    const char        *name = "Windows";

    /* 走 ntdll 的 RtlGetVersion：GetVersionEx 会被 exe 清单里的兼容性声明骗
     * （本工程没有那条声明，但别人复制这段代码时多半有）。
     * GetProcAddress 取，不为此新增链接库。 */
    ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return NULL;
    fn = (rtl_get_version_fn)(void *)GetProcAddress(ntdll, "RtlGetVersion");
    if (!fn) return NULL;

    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = (ULONG)sizeof(vi);
    if (fn(&vi) != 0) return NULL;

    if (vi.dwMajorVersion == 6) {
        if (vi.dwMinorVersion == 1)      name = "Windows 7";
        else if (vi.dwMinorVersion == 2) name = "Windows 8";
        else if (vi.dwMinorVersion == 3) name = "Windows 8.1";
        else                             name = "Windows Vista";
    } else if (vi.dwMajorVersion == 10) {
        /* Win11 也报 10.0，靠 build 号分：22000 起算 11 */
        name = (vi.dwBuildNumber >= 22000) ? "Windows 11" : "Windows 10";
    }
    return cg_str_format("%s (%lu.%lu.%lu)", name,
                         (unsigned long)vi.dwMajorVersion,
                         (unsigned long)vi.dwMinorVersion,
                         (unsigned long)vi.dwBuildNumber);
}

/**
 * @brief 取路径所在磁盘的剩余 / 总容量（字节）
 * @param path 该盘上任意路径（UTF-8，如 "C:\\logs"）；NULL 或空串直接失败
 * @param free_bytes 出口：当前用户可用的字节数；可为 NULL
 * @param total_bytes 出口：磁盘总字节数；可为 NULL
 * @return int 1 成功；0 参数为空、编码转换失败或系统查询失败（此时两个出口都不动）
 */
int cg_disk_space(const char *path, unsigned long long *free_bytes, unsigned long long *total_bytes)
{
    wchar_t        *wp;
    ULARGE_INTEGER  avail, total;
    BOOL            ok;

    if (!path || !*path) return 0;
    wp = kit_w(path);
    if (!wp) return 0;
    memset(&avail, 0, sizeof(avail));
    memset(&total, 0, sizeof(total));
    /* 第一个出口是"当前用户可用的字节数" —— 写日志前问"还写得下吗"就该用它 */
    ok = GetDiskFreeSpaceExW(wp, &avail, &total, NULL);
    free(wp);
    if (!ok) return 0;
    if (free_bytes)  *free_bytes  = (unsigned long long)avail.QuadPart;
    if (total_bytes) *total_bytes = (unsigned long long)total.QuadPart;
    return 1;
}

/**
 * @brief 按可执行文件名查进程是否在跑（大小写不敏感）
 * @param exe_name 可执行文件名（如 "Foo.exe"，只比文件名不含路径）；NULL 或空串算没在跑
 * @return int 1 在跑；0 没找到（或进程快照建不起来）
 */
int cg_process_running(const char *exe_name)
{
    HANDLE         snap;
    PROCESSENTRY32W pe;
    wchar_t       *wn;
    int            found = 0;

    if (!exe_name || !*exe_name) return 0;
    wn = kit_w(exe_name);
    if (!wn) return 0;

    snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        free(wn);
        return 0;
    }
    memset(&pe, 0, sizeof(pe));
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            /* lstrcmpiW 在 kernel32 里，不用引 wchar.h 的 _wcsicmp */
            if (lstrcmpiW(pe.szExeFile, wn) == 0) {
                found = 1;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    free(wn);
    return found;
}
