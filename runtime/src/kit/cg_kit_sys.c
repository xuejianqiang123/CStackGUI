/**
 * @file    cg_kit_sys.c
 * @brief   kit 系统能力：剪贴板、外部程序（打开/启动）、当前时间、HKCU 注册表
 * @details 从 cg_kit.c 按子系统纯搬移拆出（公共 API 未变）；跨文件复用的字符助手
 *          声明见 cg_kit_internal.h。
 */
#include "cg_kit_internal.h"

#include <shellapi.h>       /* ShellExecuteW */
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------ 剪贴板 / 外部程序 / 时间 */

/** @brief 取剪贴板文本（没有返回 NULL；用完 cg_free） */
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

/** @brief 写剪贴板文本；成功返回 0 */
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

/** @brief 用系统默认程序打开文件/文件夹/网址；成功返回 0 */
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

/** @brief 启动外部程序；wait=1 返回其退出码（启动失败 -1），wait=0 返回 0 */
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

/** @brief 当前时间字符串；fmt 传 NULL 用 "2026-09-21 02:33:53" */
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

/** @brief 读 HKCU 下的字符串值；没有返回 def 的副本 */
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

/** @brief 写 HKCU 下的字符串值（没有就新建）；成功返回 0 */
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
