/**
 * @file    cg_kit_ini.c
 * @brief   kit INI 配置读写：读一项（缺省回退）、写一项（保留文件其它内容）
 * @details 从 cg_kit.c 按子系统纯搬移拆出（公共 API 未变）。本文件内的解析助手只在
 *          INI 里用，保持 static；跨文件复用的字符助手声明见 cg_kit_internal.h。
 */
#include "cg_kit_internal.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ 配置（INI）
 * 格式就是 Windows 那套最朴素的 INI：`[节]` + `键=值`，# 或 ; 开头是注释行。
 * 读取时键名/节名不区分大小写；写入时保留文件里其它内容（只动目标那一行）。
 */

/**
 * @brief 取下一行（就地写 NUL，跳过行尾换行）
 * @param text 整个文本缓冲（行尾会被就地改成 NUL）；NULL 返回 NULL
 * @param pos 读写游标：进来是本次起点，出去指向下一行的开头
 * @return char * 本行首字符（内容不含行尾换行）；已到末尾返回 NULL
 */
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

/**
 * @brief 是节头就返回名字起点并写出长度（**不修改 line**）
 * @param line 一行文本
 * @param len 出口：节名长度（方括号内字符数）；可为 NULL
 * @return const char * 节名起点（指向 line 内部）；不是节头返回 NULL
 */
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

/**
 * @brief 判断 line 是不是 [section] 这一节（忽略大小写）
 * @param line 一行文本
 * @param section 要比的节名（不带方括号）
 * @return int 1 是；0 不是
 */
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

/**
 * @brief 命中 "key = ..." 就返回值指针（忽略键名大小写）
 * @param line 一行文本（只读，不改动它）
 * @param key 键名
 * @param klen key 的字符数，由调用方算好（省一次求长度）
 * @return const char * 值起点（已跳过等号后与行尾前的空白）；不是这一项返回 NULL
 */
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

/**
 * @brief 读 INI 的一项
 * @param path ini 文件路径（UTF-8）；NULL 直接走回退
 * @param section 节名（读时忽略大小写）；NULL 直接走回退
 * @param key 键名（读时忽略大小写）；NULL 直接走回退
 * @param def 文件 / 节 / 键不存在时的回退内容（副本顶上），可为 NULL
 * @return char * 堆串（已去掉值尾部的空白），用完 cg_free；取不到时是 def 的副本
 */
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

/**
 * @brief 往缓冲追加字符串（带上限，放不下就截断）
 * @param buf 目标缓冲
 * @param cap buf 的字节容量（含结尾 NUL）
 * @param len 读写：当前已写长度，追加后被更新
 * @param s 要追加的 NUL 结尾字符串
 * @return void 无返回值（始终保证结尾 NUL）
 */
static void kit_append(char *buf, size_t cap, size_t *len, const char *s)
{
    size_t n = strlen(s);

    if (*len + n + 1 > cap) n = (cap > *len + 1) ? (cap - *len - 1) : 0;
    memcpy(buf + *len, s, n);
    *len += n;
    buf[*len] = '\0';
}

/**
 * @brief 写 INI 的一项（保留文件其它内容，只动目标那一行）
 * @param path ini 文件路径（UTF-8）；文件不存在就当空文件；NULL 直接失败
 * @param section 节名；节不存在就在文件末尾补一个
 * @param key 键名；不能含等号，且不超过 512 字符
 * @param value 值（原样写，不转义）；NULL 直接失败，不超过 1024 字符
 * @return int 0 成功；-1 参数为空、键 / 节名非法或超长、内存不足或写文件失败
 */
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
