/**
 * @file    cg_kit.c
 * @brief   基础能力库（cg_kit）：文件、路径、配置、文本与编码、剪贴板、外部程序、注册表
 * @details 分层说明：core 不认识 Win32，win32 是界面后端，skin 是外观层；本层是
 *          **平台服务**（跟界面无关的那一半 Windows API）。业务代码只认 cstackgui.h，
 *          所以这些能力必须由运行时提供 —— 否则用户做个"读配置、写日志、转编码"的小工具
 *          就得自己 include windows.h，把"只 include cstackgui.h"的约定破掉。
 *          约定：返回 char* 的一律是 malloc 出来的，调用方用 cg_free() 释放。
 *          里面刻意只用到处都有的 Win32 API（不用 RegGetValue 这种 Vista+ 才有的），
 *          并且不依赖 POSIX 的 strtok_r/_stricmp，免得 -std=c11 下漏声明。
 */
#define UNICODE
#define _UNICODE

#include "cg_internal.h"    /* cg_log / cg_exe_path / cg_str_dup */

#include <windows.h>
#include <shellapi.h>       /* ShellExecuteW */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------ 内部助手 */

/** @brief UTF-8 -> UTF-16 堆串（调用方 free）；失败返回 NULL */
static wchar_t *kit_w(const char *s)
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

/** @brief UTF-16 -> UTF-8 堆串（调用方 free）；失败返回 NULL */
static char *kit_u8(const wchar_t *ws)
{
    int   n;
    char *out;

    if (!ws) return NULL;
    n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    out = (char *)malloc((size_t)n);
    if (!out) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, out, n, NULL, NULL);
    return out;
}

/** @brief 取文件名部分（兼容 \\ 与 /）；返回 path 内部指针 */
static const char *kit_base(const char *path)
{
    const char *s1;
    const char *s2;

    if (!path) return "";
    s1 = strrchr(path, '\\');
    s2 = strrchr(path, '/');
    if (s2 > s1) s1 = s2;
    return s1 ? s1 + 1 : path;
}

/** @brief 忽略大小写的字符比较（只处理 ASCII，够用于键名/节名） */
static char kit_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/* ------------------------------------------------------------ 内存 */

/** @brief 释放本库返回的堆内存（等价 free） */
void cg_free(void *p)
{
    free(p);
}

/* ------------------------------------------------------------ 文件 */

/** @brief 文件是否存在（目录不算） */
int cg_file_exists(const char *path)
{
    wchar_t *wp = kit_w(path);
    DWORD    a;
    int      ok = 0;

    if (!wp) return 0;
    a = GetFileAttributesW(wp);
    if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)) ok = 1;
    free(wp);
    return ok;
}

/** @brief 目录是否存在 */
int cg_dir_exists(const char *path)
{
    wchar_t *wp = kit_w(path);
    DWORD    a;
    int      ok = 0;

    if (!wp) return 0;
    a = GetFileAttributesW(wp);
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) ok = 1;
    free(wp);
    return ok;
}

/** @brief 读整个文件（去掉 UTF-8 BOM）；失败返回 NULL */
char *cg_file_read(const char *path)
{
    wchar_t *wp;
    HANDLE   h;
    DWORD    size, got = 0;
    char    *buf;

    if (!path) return NULL;
    wp = kit_w(path);
    if (!wp) return NULL;
    h = CreateFileW(wp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wp);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE) { CloseHandle(h); return NULL; }
    buf = (char *)malloc((size_t)size + 1);
    if (!buf) { CloseHandle(h); return NULL; }
    if (size > 0 && !ReadFile(h, buf, size, &got, NULL)) {
        free(buf);
        CloseHandle(h);
        return NULL;
    }
    CloseHandle(h);
    buf[got] = '\0';
    /* 记事本存 UTF-8 会加 BOM，直接当文本用会多出三个看不见的字符 */
    if (got >= 3 && (unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB &&
        (unsigned char)buf[2] == 0xBF) {
        memmove(buf, buf + 3, (size_t)(got - 3) + 1);
    }
    return buf;
}

/** @brief 写文件（覆盖，UTF-8 文本）；成功返回 0 */
int cg_file_write(const char *path, const char *text)
{
    wchar_t *wp;
    HANDLE   h;
    DWORD    wrote = 0;
    int      ok = -1;

    if (!path || !text) return -1;
    wp = kit_w(path);
    if (!wp) return -1;
    h = CreateFileW(wp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wp);
    if (h == INVALID_HANDLE_VALUE) return -1;
    if (WriteFile(h, text, (DWORD)strlen(text), &wrote, NULL)) ok = 0;
    CloseHandle(h);
    return ok;
}

/** @brief 追加到文件末尾（不存在就新建）；成功返回 0 */
int cg_file_append(const char *path, const char *text)
{
    wchar_t *wp;
    HANDLE   h;
    DWORD    wrote = 0;
    int      ok = -1;

    if (!path || !text) return -1;
    wp = kit_w(path);
    if (!wp) return -1;
    h = CreateFileW(wp, FILE_APPEND_DATA, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wp);
    if (h == INVALID_HANDLE_VALUE) return -1;
    SetFilePointer(h, 0, NULL, FILE_END);
    if (WriteFile(h, text, (DWORD)strlen(text), &wrote, NULL)) ok = 0;
    CloseHandle(h);
    return ok;
}

/** @brief 删文件；成功返回 0 */
int cg_file_delete(const char *path)
{
    wchar_t *wp = kit_w(path);
    int      ok;

    if (!wp) return -1;
    ok = DeleteFileW(wp) ? 0 : -1;
    free(wp);
    return ok;
}

/** @brief 拷文件（覆盖目标）；成功返回 0 */
int cg_file_copy(const char *from, const char *to)
{
    wchar_t *wf = kit_w(from);
    wchar_t *wt = kit_w(to);
    int      ok = -1;

    if (wf && wt && CopyFileW(wf, wt, FALSE)) ok = 0;
    free(wf);
    free(wt);
    return ok;
}

/** @brief 建目录（父目录必须存在）；成功返回 0 */
int cg_dir_create(const char *path)
{
    wchar_t *wp = kit_w(path);
    int      ok;

    if (!wp) return -1;
    if (CreateDirectoryW(wp, NULL)) ok = 0;
    else ok = (GetLastError() == ERROR_ALREADY_EXISTS) ? 0 : -1;
    free(wp);
    return ok;
}

/** @brief 枚举目录：index 从 0 起；返回 1 = 拿到名字，0 = 结束/出错 */
int cg_dir_list(const char *dir, const char *filter, int index, char *out, int out_size)
{
    wchar_t *wd;
    wchar_t *wf;
    wchar_t  wpat[1400];
    WIN32_FIND_DATAW fd;
    HANDLE   h;
    int      n = 0;
    int      found = 0;

    if (!dir || !out || out_size <= 0 || index < 0) return 0;
    out[0] = '\0';

    wd = kit_w(dir);
    wf = kit_w(filter && *filter ? filter : "*");
    if (!wd || !wf) { free(wd); free(wf); return 0; }
    _snwprintf(wpat, sizeof(wpat) / sizeof(wpat[0]), L"%ls\\%ls", wd, wf);
    free(wd);
    free(wf);

    h = FindFirstFileW(wpat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        char *u;
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (n != index) { n++; continue; }
        u = kit_u8(fd.cFileName);
        if (u) {
            snprintf(out, (size_t)out_size, "%s", u);
            free(u);
            found = 1;
        }
        break;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

/* ------------------------------------------------------------ 路径 */

/** @brief 拼路径（a 末尾已有分隔符就不重复加） */
char *cg_path_join(const char *a, const char *b)
{
    char   *out;
    size_t  la, lb;
    int     need_sep;

    if (!a) return cg_str_dup(b);
    if (!b) return cg_str_dup(a);
    la = strlen(a);
    lb = strlen(b);
    need_sep = (la > 0 && a[la - 1] != '\\' && a[la - 1] != '/');
    out = (char *)malloc(la + (size_t)need_sep + lb + 1);
    if (!out) return NULL;
    memcpy(out, a, la);
    if (need_sep) out[la] = '\\';
    memcpy(out + la + (size_t)need_sep, b, lb + 1);
    return out;
}

/** @brief 取目录部分（不含末尾分隔符；没有目录返回 "."） */
char *cg_path_dir(const char *path)
{
    const char *b;
    size_t      n;
    char       *out;

    if (!path) return NULL;
    b = kit_base(path);
    n = (size_t)(b - path);
    while (n > 1 && (path[n - 1] == '\\' || path[n - 1] == '/')) {
        if (n == 3 && path[1] == ':') break;
        n--;
    }
    if (n == 0) return cg_str_dup(".");
    out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, path, n);
    out[n] = '\0';
    return out;
}

/** @brief 取文件名部分；返回 path 内部指针，不用释放 */
const char *cg_path_name(const char *path)
{
    return path ? kit_base(path) : "";
}

/** @brief 取扩展名（含点）；没有返回 ""。返回内部指针 */
const char *cg_path_ext(const char *path)
{
    const char *base = kit_base(path);
    const char *dot = strrchr(base, '.');
    return (dot && dot != base) ? dot : "";
}

/** @brief exe 所在目录（堆串） */
char *cg_app_dir(void)
{
    char exe[1024];

    if (cg_exe_path(exe, sizeof(exe)) != 0) return NULL;
    return cg_path_dir(exe);
}

/** @brief 用户数据目录 %LOCALAPPDATA%\<exe名>（自动建；拿不到退回 exe 旁边） */
char *cg_app_data_dir(void)
{
    wchar_t wbuf[1024];
    DWORD   n = GetEnvironmentVariableW(L"LOCALAPPDATA", wbuf,
                                       (DWORD)(sizeof(wbuf) / sizeof(wbuf[0])));
    char    exe[1024];
    char    stem[128] = { 0 };
    char   *dir = NULL;

    if (cg_exe_path(exe, sizeof(exe)) == 0) {
        const char *base = kit_base(exe);
        const char *dot = strrchr(base, '.');
        size_t      len = dot ? (size_t)(dot - base) : strlen(base);
        if (len >= sizeof(stem)) len = sizeof(stem) - 1;
        memcpy(stem, base, len);
        stem[len] = '\0';
    }

    if (n > 0 && n < (DWORD)(sizeof(wbuf) / sizeof(wbuf[0])) && stem[0]) {
        char *base = kit_u8(wbuf);
        if (base) {
            dir = cg_path_join(base, stem);
            free(base);
        }
    }
    if (!dir && stem[0]) dir = cg_app_dir();

    if (dir) cg_dir_create(dir);
    return dir;
}

/* ------------------------------------------------------------ 配置（INI）
 * 格式就是 Windows 那套最朴素的 INI：`[节]` + `键=值`，# 或 ; 开头是注释行。
 * 读取时键名/节名不区分大小写；写入时保留文件里其它内容（只动目标那一行）。
 */

/** @brief 取下一行（就地写 NUL，跳过 \r\n）；没有更多返回 NULL */
static char *kit_next_line(char *text, size_t *pos)
{
    size_t i = *pos;
    char  *start;

    if (!text || !text[i]) return NULL;
    start = text + i;
    while (text[i] && text[i] != '\r' && text[i] != '\n') i++;
    if (text[i]) {
        text[i++] = '\0';
        while (text[i] == '\r' || text[i] == '\n') i++;
    }
    *pos = i;
    return start;
}

/** @brief 是节头就返回名字起点并写 *len（**不修改 line**）；不是返回 NULL */
static const char *kit_section_name(const char *line, size_t *len)
{
    const char *p = line;
    const char *end;

    while (*p == ' ' || *p == '\t') p++;
    if (*p != '[') return NULL;
    end = strchr(p, ']');
    if (!end) return NULL;
    if (len) *len = (size_t)(end - p - 1);
    return p + 1;
}

/** @brief line 是不是 [section] 这一节（忽略大小写） */
static int kit_section_is(const char *line, const char *section)
{
    size_t      len = 0;
    const char *name = kit_section_name(line, &len);
    size_t      i;

    if (!name) return 0;
    if (strlen(section) != len) return 0;
    for (i = 0; i < len; i++) {
        if (kit_lower(name[i]) != kit_lower(section[i])) return 0;
    }
    return 1;
}

/** @brief 命中 "key = ..." 就返回值指针（忽略键名大小写）；否则 NULL */
static const char *kit_key_value(char *line, const char *key, size_t klen)
{
    char *p = line;
    size_t i;

    while (*p == ' ' || *p == '\t') p++;
    for (i = 0; i < klen; i++) {
        if (kit_lower(p[i]) != kit_lower(key[i])) return NULL;
    }
    p += klen;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return NULL;      /* "key2=" 这种前缀不算命中 */
    p++;
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/** @brief 读 INI 的一项；不存在返回 def 的副本 */
char *cg_ini_read(const char *path, const char *section, const char *key, const char *def)
{
    char   *text;
    char   *line;
    char   *out = NULL;
    size_t  pos = 0;
    size_t  klen;
    int     in_section = 0;

    if (!path || !section || !key) return cg_str_dup(def);
    text = cg_file_read(path);
    if (!text) return cg_str_dup(def);
    klen = strlen(key);

    while ((line = kit_next_line(text, &pos)) != NULL) {
        if (kit_section_name(line, NULL)) {
            in_section = kit_section_is(line, section);
            continue;
        }
        if (!in_section) continue;
        {
            const char *v = kit_key_value(line, key, klen);
            if (v) {
                size_t len = strlen(v);
                while (len > 0 && (v[len - 1] == ' ' || v[len - 1] == '\t' || v[len - 1] == '\r')) len--;
                out = (char *)malloc(len + 1);
                if (out) { memcpy(out, v, len); out[len] = '\0'; }
                break;
            }
        }
    }
    free(text);
    return out ? out : cg_str_dup(def);
}

/** @brief 往缓冲追加（带上限）；用于拼写出的 INI 文本 */
static void kit_append(char *buf, size_t cap, size_t *len, const char *s)
{
    size_t n = strlen(s);

    if (*len + n + 1 > cap) n = (cap > *len + 1) ? (cap - *len - 1) : 0;
    memcpy(buf + *len, s, n);
    *len += n;
    buf[*len] = '\0';
}

/** @brief 写 INI 的一项（保留文件其它内容）；成功返回 0 */
int cg_ini_write(const char *path, const char *section, const char *key, const char *value)
{
    char  *text;
    char **lines = NULL;
    int    nlines = 0, cap_lines = 0;
    int    in_section = 0, sec_start = -1, sec_end = -1, found = -1, inserted = 0;
    size_t pos = 0, klen, total = 0, len = 0;
    char  *line, *out;
    char   entry[2048];
    int    rc;

    if (!path || !section || !key || !value) return -1;
    if (strchr(key, '=') || strchr(section, ']') || strchr(section, '[')) return -1;
    if (strlen(key) > 512 || strlen(section) > 512 || strlen(value) > 1024) return -1;

    text = cg_file_read(path);
    if (!text) text = cg_str_dup("");
    if (!text) return -1;
    klen = strlen(key);

    while ((line = kit_next_line(text, &pos)) != NULL) {
        if (nlines == cap_lines) {
            char **nl;
            int    nc = cap_lines ? cap_lines * 2 : 16;
            nl = (char **)realloc(lines, (size_t)nc * sizeof(char *));
            if (!nl) { free(lines); free(text); return -1; }
            lines = nl;
            cap_lines = nc;
        }
        lines[nlines++] = line;
    }

    /* 1. 定位节范围与该键所在行（解析一律不改缓冲区，否则写回时节头会被写坏） */
    for (int i = 0; i < nlines; i++) {
        if (kit_section_name(lines[i], NULL)) {
            if (in_section) { sec_end = i; break; }
            if (kit_section_is(lines[i], section)) { in_section = 1; sec_start = i; sec_end = nlines; }
            continue;
        }
        if (in_section) {
            const char *v = kit_key_value(lines[i], key, klen);
            if (v) { found = i; break; }
        }
    }
    if (in_section && sec_end < 0) sec_end = nlines;

    /* 2. 拼输出（容量先估够，kit_append 还会兜底截断） */
    for (int i = 0; i < nlines; i++) total += strlen(lines[i]) + 2;
    total += strlen(key) + strlen(value) + strlen(section) + 32;
    out = (char *)malloc(total);
    if (!out) { free(lines); free(text); return -1; }
    out[0] = '\0';

    snprintf(entry, sizeof(entry), "%s=%s", key, value);
    for (int i = 0; i < nlines; i++) {
        if (i == found) {
            kit_append(out, total, &len, entry);
            inserted = 1;          /* 不置位的话循环结束后会在末尾再写一遍 */
        } else {
            if (!inserted && found < 0 && sec_start >= 0 && i == sec_end) {
                kit_append(out, total, &len, entry);
                kit_append(out, total, &len, "\r\n");   /* 少了这句下一个节头会被粘到同一行 */
                inserted = 1;
            }
            kit_append(out, total, &len, lines[i]);
        }
        kit_append(out, total, &len, "\r\n");
    }
    if (!inserted) {
        if (sec_start >= 0) {
            kit_append(out, total, &len, entry);
            kit_append(out, total, &len, "\r\n");
        } else {
            /* 节还不存在：末尾补一个 [节] + 这一行（分几段拼，避免 %s 拼接被截断告警） */
            if (len > 0) kit_append(out, total, &len, "\r\n");
            kit_append(out, total, &len, "[");
            kit_append(out, total, &len, section);
            kit_append(out, total, &len, "]\r\n");
            kit_append(out, total, &len, entry);
            kit_append(out, total, &len, "\r\n");
        }
    }

    rc = cg_file_write(path, out);
    free(out);
    free(lines);
    free(text);
    return rc;
}

/* ------------------------------------------------------------ 文本与编码 */

/** @brief 拼字符串（printf 风格），返回堆内存 */
char *cg_str_format(const char *fmt, ...)
{
    va_list ap, ap2;
    int     n;
    char   *out;

    if (!fmt) return NULL;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return NULL; }
    out = (char *)malloc((size_t)n + 1);
    if (out) vsnprintf(out, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    return out;
}

/** @brief 去掉首尾空白，返回堆内存 */
char *cg_str_trim(const char *s)
{
    const char *b;
    const char *e;
    size_t      n;
    char       *out;

    if (!s) return NULL;
    b = s;
    while (*b == ' ' || *b == '\t' || *b == '\r' || *b == '\n') b++;
    e = b + strlen(b);
    while (e > b && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) e--;
    n = (size_t)(e - b);
    out = (char *)malloc(n + 1);
    if (!out) return NULL;
    memcpy(out, b, n);
    out[n] = '\0';
    return out;
}

/** @brief 比较字符串（忽略英文大小写）：相等返回 1 */
int cg_str_equal(const char *a, const char *b)
{
    if (!a || !b) return a == b;
    while (*a && *b) {
        if (kit_lower(*a) != kit_lower(*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}

/** @brief 把 src 里所有 from 换成 to，返回堆内存 */
char *cg_str_replace(const char *src, const char *from, const char *to)
{
    size_t      fl, tl, sl, i, cap, len = 0;
    char       *out;
    const char *p;

    if (!src) return NULL;
    if (!from || !*from) return cg_str_dup(src);
    fl = strlen(from);
    tl = to ? strlen(to) : 0;
    sl = strlen(src);
    cap = sl * (tl > fl ? tl : 1) + 1;
    out = (char *)malloc(cap);
    if (!out) return NULL;
    for (i = 0; i < sl;) {
        if (i + fl <= sl && memcmp(src + i, from, fl) == 0) {
            if (len + tl + 1 > cap) break;
            memcpy(out + len, to ? to : "", tl);
            len += tl;
            i += fl;
        } else {
            if (len + 2 > cap) break;
            out[len++] = src[i++];
        }
    }
    out[len] = '\0';
    (void)p;
    return out;
}

/** @brief UTF-8 -> GBK */
char *cg_utf8_to_gbk(const char *utf8)
{
    wchar_t *w;
    int      n;
    char    *out;

    if (!utf8) return NULL;
    w = kit_w(utf8);
    if (!w) return NULL;
    n = WideCharToMultiByte(CP_ACP, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) { free(w); return NULL; }
    out = (char *)malloc((size_t)n);
    if (out) WideCharToMultiByte(CP_ACP, 0, w, -1, out, n, NULL, NULL);
    free(w);
    return out;
}

/** @brief GBK -> UTF-8 */
char *cg_gbk_to_utf8(const char *gbk)
{
    wchar_t *w;
    int      n;
    char    *out;

    if (!gbk) return NULL;
    n = MultiByteToWideChar(CP_ACP, 0, gbk, -1, NULL, 0);
    if (n <= 0) return NULL;
    w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_ACP, 0, gbk, -1, w, n);
    out = kit_u8(w);
    free(w);
    return out;
}

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
