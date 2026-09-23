/**
 * @file    cg_kit_fs.c
 * @brief   kit 文件与路径：文件/目录的读写枚举、路径拼接与分解、程序目录与数据目录
 * @details 从 cg_kit.c 按子系统纯搬移拆出（公共 API 未变）；跨文件复用的字符助手
 *          声明见 cg_kit_internal.h。
 */
#include "cg_kit_internal.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ 内部助手 */

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
