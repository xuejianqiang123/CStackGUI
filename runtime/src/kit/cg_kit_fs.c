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

/** @brief 取文件名部分（兼容 \\ 与 /）；返回 path 内部指针
 *  @param path 路径；可为 NULL
 *  @return const char * 文件名部分（指向 path 内部，不用释放）；path 为 NULL 返回 ""
 */
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

/** @brief 释放本库返回的堆内存（等价 free）
 *  @param p 堆指针；NULL 安全
 *  @return void 无返回值
 */
void cg_free(void *p)
{
    free(p);
}

/* ------------------------------------------------------------ 文件 */

/** @brief 文件是否存在（目录不算）
 *  @param path 路径（UTF-8）
 *  @return int 1 存在、0 不存在或路径上是个目录
 */
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

/** @brief 目录是否存在
 *  @param path 路径（UTF-8）
 *  @return int 1 存在、0 不存在或路径上不是目录
 */
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

/** @brief 读整个文件（去掉 UTF-8 BOM）；失败返回 NULL
 *  @param path 路径（UTF-8）；为 NULL 返回 NULL
 *  @return char * 堆串（已去 BOM、以 NUL 结尾），用完 cg_free；打不开或读失败返回 NULL
 */
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

/** @brief 读整个**二进制**文件：返回堆缓冲（调用方 cg_free）并把长度写进 *out_len
 *  @param path 路径（UTF-8）；为 NULL 返回 NULL
 *  @param out_len 长度出口：写入读到的字节数（0 字节文件写 0）；可传 NULL；失败时不动
 *  @return unsigned char* 内容（0 字节文件也返回非 NULL）；失败 NULL（此时 *out_len 不动）
 *  @note 与 cg_file_read 的分工：那个返回"字符串"（去 BOM、以 NUL 结尾），
 *        **含 0x00 的二进制用它读不全** —— `strlen` 到第一个 0 就截断了。
 *        固件/镜像/抓包/裸区日志都是含 0x00 的数据，所以要这个带长度出口的版本。
 *        顺带：这个版本**不去 BOM** —— 二进制就是二进制，一个字节都不许动。 */
unsigned char *cg_file_read_bytes(const char *path, int *out_len)
{
    wchar_t       *wp;
    HANDLE         h;
    DWORD          size, got = 0;
    unsigned char *buf;

    if (!path) return NULL;
    wp = kit_w(path);
    if (!wp) return NULL;
    h = CreateFileW(wp, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wp);
    if (h == INVALID_HANDLE_VALUE) return NULL;

    size = GetFileSize(h, NULL);
    if (size == INVALID_FILE_SIZE || size > 0x7FFFFFFFu) {   /* 长度出口是 int，超了直接报失败 */
        if (size > 0x7FFFFFFFu) cg_logm(CG_LOG_WARN, "fs", "文件超过 2GB，读不进来：%s", path);
        CloseHandle(h);
        return NULL;
    }
    /* 0 字节文件也要返回**非 NULL**：调用方靠"NULL = 失败"来区分"空文件"与"打不开" */
    buf = (unsigned char *)malloc((size_t)size + 1);
    if (!buf) { CloseHandle(h); return NULL; }
    if (size > 0 && !ReadFile(h, buf, size, &got, NULL)) {
        free(buf);
        CloseHandle(h);
        return NULL;
    }
    CloseHandle(h);
    buf[got] = '\0';                      /* 方便临时当字符串看，但不计进长度 */
    if (out_len) *out_len = (int)got;
    return buf;
}

/** @brief 写文件（覆盖，UTF-8 文本）；成功返回 0
 *  @param path 路径（UTF-8）；为 NULL 返回 -1
 *  @param text 文本内容；按 strlen 写，须以 NUL 结尾
 *  @return int 0 成功、-1 失败
 */
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

/** @brief 覆盖写**二进制**文件（len 可以为 0 = 建一个空文件）；成功 0，失败 -1
 *  @param path 路径（UTF-8）；为 NULL 返回 -1
 *  @param data 要写的字节；len 为 0 时可传 NULL
 *  @param len 字节数；可以为 0（建一个空文件），负数算失败
 *  @return int 0 成功、-1 失败
 *  @note 与 cg_file_write 的分工同读那边：那个按 `strlen` 写，**数据里带 0x00 就写不全**。 */
int cg_file_write_bytes(const char *path, const void *data, int len)
{
    wchar_t *wp;
    HANDLE   h;
    DWORD    wrote = 0;
    int      ok = -1;

    if (!path || len < 0 || (!data && len > 0)) return -1;
    wp = kit_w(path);
    if (!wp) return -1;
    h = CreateFileW(wp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    free(wp);
    if (h == INVALID_HANDLE_VALUE) return -1;
    if (len == 0 || WriteFile(h, data, (DWORD)len, &wrote, NULL)) ok = 0;
    CloseHandle(h);
    return ok;
}

/** @brief 文件大小（字节）；文件不存在 / 调不动 / 给的是目录都返回 -1
 *  @param path 路径（UTF-8）；为 NULL 返回 -1
 *  @return long long 字节数；不存在 / 目录 / 调不动返回 -1
 *  @note 想"不读内容先问大小"时用它（大文件，或只想显示大小）。 */
long long cg_file_size(const char *path)
{
    wchar_t                  *wp;
    WIN32_FILE_ATTRIBUTE_DATA fad;

    if (!path) return -1;
    wp = kit_w(path);
    if (!wp) return -1;
    if (!GetFileAttributesExW(wp, GetFileExInfoStandard, &fad)) {
        free(wp);
        return -1;
    }
    free(wp);
    if (fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return -1;
    return ((long long)fad.nFileSizeHigh << 32) | (long long)fad.nFileSizeLow;
}

/** @brief 追加到文件末尾（不存在就新建）；成功返回 0
 *  @param path 路径（UTF-8）；为 NULL 返回 -1
 *  @param text 追加的内容；按 strlen 写，须以 NUL 结尾
 *  @return int 0 成功、-1 失败
 */
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

/** @brief 删文件；成功返回 0
 *  @param path 路径（UTF-8）；为 NULL 或转不出宽路径返回 -1
 *  @return int 0 成功、-1 失败
 */
int cg_file_delete(const char *path)
{
    wchar_t *wp = kit_w(path);
    int      ok;

    if (!wp) return -1;
    ok = DeleteFileW(wp) ? 0 : -1;
    free(wp);
    return ok;
}

/** @brief 拷文件（覆盖目标）；成功返回 0
 *  @param from 源路径（UTF-8）
 *  @param to 目标路径（UTF-8）
 *  @return int 0 成功、-1 失败（含任一路径转不出宽路径）
 */
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

/** @brief 建目录（父目录必须存在）；成功返回 0
 *  @param path 路径（UTF-8）；父目录必须已存在
 *  @return int 0 成功（目录已经存在也算成功）、-1 失败
 */
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

/** @brief 枚举目录：index 从 0 起；返回 1 = 拿到名字，0 = 结束/出错
 *  @param dir 目录路径（UTF-8）；为 NULL 返回 0
 *  @param filter 通配符，如 "*.txt"；NULL 或 "" 表示所有条目
 *  @param index 第几个条目（0 起），跳过 "." 与 ".." 后计数
 *  @param out 名字的输出缓冲（UTF-8，只有名字、不带目录）；函数只往里写
 *  @param out_size out 的字节数
 *  @return int 1 = 拿到一个名字（已写进 out）、0 = 没有了或参数不对
 */
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

/** @brief 拼路径（a 末尾已有分隔符就不重复加，补的是 '\\'）
 *  @param a 前半段；为 NULL 时返回 b 的副本
 *  @param b 后半段；为 NULL 时返回 a 的副本
 *  @return char * 堆串，用完 cg_free；失败返回 NULL
 */
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

/** @brief 取目录部分（不含末尾分隔符；没有目录返回 "."）
 *  @param path 路径；为 NULL 返回 NULL
 *  @return char * 堆串，用完 cg_free；失败返回 NULL
 */
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

/** @brief 取文件名部分；返回 path 内部指针，不用释放
 *  @param path 路径；可为 NULL
 *  @return const char * path 内部的指针（不用释放）；path 为 NULL 返回 ""
 */
const char *cg_path_name(const char *path)
{
    return path ? kit_base(path) : "";
}

/** @brief 取扩展名（含点）；没有返回 ""。返回内部指针
 *  @param path 路径；可为 NULL
 *  @return const char * path 内部的指针（含点，不用释放）；没有扩展名返回 ""
 */
const char *cg_path_ext(const char *path)
{
    const char *base = kit_base(path);
    const char *dot = strrchr(base, '.');
    return (dot && dot != base) ? dot : "";
}

/** @brief exe 所在目录（堆串）
 *  @return char * 堆串（不含末尾分隔符），用完 cg_free；取不到 exe 路径返回 NULL
 */
char *cg_app_dir(void)
{
    char exe[1024];

    if (cg_exe_path(exe, sizeof(exe)) != 0) return NULL;
    return cg_path_dir(exe);
}

/** @brief 用户数据目录 %LOCALAPPDATA%\<exe名>（自动建；拿不到退回 exe 旁边）
 *  @return char * 堆串（已建好目录），用完 cg_free；连 exe 名都取不到时返回 NULL
 */
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
