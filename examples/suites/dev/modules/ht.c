/**
 * @file    ht.c
 * @brief   hextool —— 套件模块（原 examples\hextool 合并而来，函数名未改：static 编译单元隔离）
 * @note    由 tools\suite-merge.mjs（.cgui 侧）+ build\gen_modules.mjs（C 侧）的变换规则
 *          产出：CG_BIND 字符串带 ht_ 前缀（与 .cgui 的 events 一一对应）、
 *          绑定表导出为 ht_bindings、原 on_ready 变成 ht_init。
 *          各工具的回调都是 static，合并进同一次链接不冲突。
 */

/**
 * @file    main.c
 * @brief   进制与校验工具箱：把一段输入当文本或十六进制解析，做字节视图、校验、编码与报文组装
 * @details 面向工控 / 嵌入式调试的**纯计算**工具，不碰任何硬件：
 *
 *            输入区  多行文本框 + 「按 HEX 解析」复选框（同一段输入两种看法）
 *            字节区  字节数、HEX 串、可打印 ASCII、u16 / u32 的大小端解释
 *            校验区  ADD8 / XOR8 / CRC16-Modbus / CRC32 / MD5 / SHA1 / SHA256
 *            编码区  Base64 编解码 / URL 百分号编解码 / HEX 与文本互转
 *            报文区  帧头 + 地址 + 功能码 + 长度 + 载荷 + CRC16 + 帧尾，预览整帧 HEX
 *
 *          结果全部逐行写进右侧 terminal，状态栏显示最近一次操作。
 *          换算一律走 cg_kit 的现成接口（cg_hex_encode / cg_crc16 / cg_hash / cg_proto_build 等），
 *          本文件**只 include cstackgui.h 与标准头**，没有任何 Windows 头。
 *
 *          回调不依赖真实用户输入：编辑框在就绪钩子里预填了一段样例，所以 --selftest
 *          合成事件时每条路径都跑得到，且全程不弹窗、不阻塞。
 *
 * 界面在 hextool.cgui 里；本文件只有换算与输出。
 * 编译：在项目根跑 build-app.bat hextool
 * 自检：hextool.exe --selftest
 */
#include "cstackgui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 常量与业务状态 */

/** 解析出的字节上限：够看一帧报文与一小段固件片段 */
#define HX_BYTES 1024
/** 通用文本 / 编码输出缓冲：Base64、URL、帧预览都放得下 */
#define HX_BUF   8192
/** 终端单行缓冲：任意 HX_BUF 级别的载荷再加上前缀也不会被截断 */
#define HX_LINE  (HX_BUF + 256)

/**
 * @brief 工具的全部业务状态（回调需要时随时取）
 */
typedef struct {
    unsigned char bytes[HX_BYTES];  /**< 当前解析出的字节 */
    int           nbytes;           /**< bytes 里有效字节数 */
    int           as_hex;           /**< 1 = 输入按十六进制解析，0 = 按文本 */
    char          last[160];        /**< 最近一次操作的描述，显示在状态栏第一段 */
} hx_biz_t;

static hx_biz_t s_biz;

/* ------------------------------------------------------------ 公共小工具 */

/**
 * @brief 往结果终端写一行
 * @param app 应用
 * @param dir 方向：CG_TERM_RX 数据 / CG_TERM_TX 操作结果 / CG_TERM_SYS 程序说明
 * @param fmt printf 风格文字
 * @return void 无返回值
 */
static void term_line(cg_app_t *app, int dir, const char *fmt, ...)
{
    char    buf[HX_LINE];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    cg_term_write(cg_find(app, "term_out"), dir, buf, -1);
}

/**
 * @brief 把业务状态刷到状态栏（最近一次操作 + 字节数 + 当前解析模式）
 * @param app 应用
 * @return void 无返回值
 */
static void refresh_status(cg_app_t *app)
{
    cg_widget_t *sb = cg_find(app, "sb_main");

    cg_status_set(sb, 0, s_biz.last[0] ? s_biz.last : "就绪");
    cg_status_setf(sb, 1, "字节 %d · 按 %s 解析", s_biz.nbytes, s_biz.as_hex ? "HEX" : "文本");
}

/**
 * @brief 记下最近一次操作并刷新状态栏
 * @param app 应用
 * @param fmt printf 风格的操作描述
 * @return void 无返回值
 */
static void set_status(cg_app_t *app, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(s_biz.last, sizeof(s_biz.last), fmt, ap);
    va_end(ap);
    refresh_status(app);
}

/**
 * @brief 按当前模式把输入框内容解析成字节，结果存进业务状态
 * @param app 应用
 * @return void 无返回值
 * @details 勾选「按 HEX 解析」时走十六进制解码（空格 / 逗号 / 连字符都当分隔）；
 *          否则把文本原样按字节收下。解析失败时字节数归 0 并写一行说明。
 */
static void parse_input(cg_app_t *app)
{
    cg_widget_t *edt  = cg_find(app, "edt_input");
    cg_widget_t *chk  = cg_find(app, "chk_hex");
    const char  *text = edt ? cg_widget_text(edt) : "";

    if (!text) text = "";
    s_biz.as_hex = chk ? cg_widget_checked(chk) : 0;

    if (s_biz.as_hex) {
        int n = cg_hex_decode(text, s_biz.bytes, HX_BYTES);

        if (n == -1) {
            s_biz.nbytes = 0;
            term_line(app, CG_TERM_SYS, "HEX 解析失败：出现非十六进制字符，或只剩半个字节");
        } else if (n == -2) {
            s_biz.nbytes = 0;
            term_line(app, CG_TERM_SYS, "HEX 解析失败：字节数超过上限 %d", HX_BYTES);
        } else {
            s_biz.nbytes = n;
        }
    } else {
        int n = (int)strlen(text);

        if (n > HX_BYTES) n = HX_BYTES;
        if (n > 0) memcpy(s_biz.bytes, text, (size_t)n);
        s_biz.nbytes = n;
    }
}

/**
 * @brief 把一段字节按「HEX + 可打印 ASCII + u16 / u32 大小端」逐行输出
 * @param app 应用
 * @param data 字节流，不要求 0 结尾
 * @param len 字节数；超过上限的部分只输出前上限个
 * @return void 无返回值
 */
static void output_bytes(cg_app_t *app, const void *data, int len)
{
    const unsigned char *b = (const unsigned char *)data;
    char  hex[HX_BUF];
    char  ascii[HX_BYTES + 1];
    int   i;

    if (!data || len <= 0) {
        term_line(app, CG_TERM_SYS, "（0 字节）");
        return;
    }
    if (len > HX_BYTES) {
        term_line(app, CG_TERM_SYS, "（只显示前 %d 字节，共 %d 字节）", HX_BYTES, len);
        len = HX_BYTES;
    }

    if (cg_hex_encode(b, len, " ", 1, hex, sizeof(hex)) < 0) {
        snprintf(hex, sizeof(hex), "(HEX 编码失败)");
    }
    term_line(app, CG_TERM_RX, "HEX   ：%s", hex);

    for (i = 0; i < len; i++) {
        ascii[i] = (b[i] >= 0x20 && b[i] < 0x7F) ? (char)b[i] : '.';
    }
    ascii[len] = '\0';
    term_line(app, CG_TERM_RX, "ASCII ：%s", ascii);

    if (len >= 2) {
        int le = cg_bytes_get_u16(b, len, 0, 0);
        int be = cg_bytes_get_u16(b, len, 0, 1);

        term_line(app, CG_TERM_RX, "u16   ：小端 %5u (0x%04X) · 大端 %5u (0x%04X)",
                  (unsigned)le, (unsigned)le, (unsigned)be, (unsigned)be);
    }
    if (len >= 4) {
        long long le = cg_bytes_get_u32(b, len, 0, 0);
        long long be = cg_bytes_get_u32(b, len, 0, 1);

        term_line(app, CG_TERM_RX, "u32   ：小端 %llu (0x%08llX) · 大端 %llu (0x%08llX)",
                  (unsigned long long)le, (unsigned long long)le,
                  (unsigned long long)be, (unsigned long long)be);
    }
}

/**
 * @brief 输出当前字节的字节视图（字节数 + 三种表示）
 * @param app 应用
 * @return void 无返回值
 */
static void show_byte_view(cg_app_t *app)
{
    term_line(app, CG_TERM_SYS, "—— 字节视图 ——");
    term_line(app, CG_TERM_SYS, "字节数：%d", s_biz.nbytes);
    output_bytes(app, s_biz.bytes, s_biz.nbytes);
}

/**
 * @brief 输出当前字节的全部校验值（累加和 / 异或 / CRC16 / CRC32 / 三种摘要）
 * @param app 应用
 * @return void 无返回值
 * @note 累加和与异或和由本函数手算：框架里只有给报文组装用的 CG_SUM_* 常量，没有独立的一元接口
 */
static void show_checksums(cg_app_t *app)
{
    const unsigned char *b = s_biz.bytes;
    int      n   = s_biz.nbytes;
    unsigned add = 0;
    unsigned xr  = 0;
    char    *digest;
    int      i;

    term_line(app, CG_TERM_SYS, "—— 校验 ——");
    if (n <= 0) {
        term_line(app, CG_TERM_SYS, "（没有数据，按空输入算）");
    }
    for (i = 0; i < n; i++) {
        add = (add + b[i]) & 0xFFu;
        xr ^= b[i];
    }
    term_line(app, CG_TERM_RX, "ADD8         ：0x%02X", add);
    term_line(app, CG_TERM_RX, "XOR8         ：0x%02X", xr);
    term_line(app, CG_TERM_RX, "CRC16-Modbus ：0x%04X", cg_crc16(b, n));
    term_line(app, CG_TERM_RX, "CRC32(IEEE)  ：0x%08X", cg_crc32(b, n));

    digest = cg_hash(b, n, "md5");
    term_line(app, CG_TERM_RX, "MD5          ：%s", digest ? digest : "（算不出来）");
    cg_free(digest);

    digest = cg_hash(b, n, "sha1");
    term_line(app, CG_TERM_RX, "SHA1         ：%s", digest ? digest : "（算不出来）");
    cg_free(digest);

    digest = cg_hash(b, n, "sha256");
    term_line(app, CG_TERM_RX, "SHA256       ：%s", digest ? digest : "（算不出来）");
    cg_free(digest);
}

/* ------------------------------------------------------------ 输入区回调 */

/**
 * @brief 输入内容变了：重新解析并存字节（不刷屏，只更新状态栏）
 * @param self 输入框控件
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_input_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    parse_input(app);
    set_status(app, "输入已更新");
}

/**
 * @brief 「按 HEX 解析」复选框变化：切换解析模式并重新解析
 * @param self 复选框控件
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_mode_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    parse_input(app);
    set_status(app, "解析模式：%s", s_biz.as_hex ? "HEX" : "文本");
}

/**
 * @brief 「解析字节」按钮：输出字节视图与全部校验值
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_parse_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    (void)ev;
    (void)user;
    parse_input(app);
    term_line(app, CG_TERM_SYS, "==== 解析输入 ====");
    show_byte_view(app);
    show_checksums(app);
    set_status(app, "解析完成：%d 字节", s_biz.nbytes);
}

/* ------------------------------------------------------------ 编码区回调 */

/**
 * @brief 「Base64 编码」按钮：把解析出的字节编成 Base64
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_b64_enc_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char      out[HX_BUF];
    int       n;

    (void)ev;
    (void)user;
    parse_input(app);
    if (s_biz.nbytes <= 0) {
        term_line(app, CG_TERM_SYS, "Base64 编码：没有可编码的字节");
        set_status(app, "Base64 编码：无数据");
        return;
    }
    n = cg_base64_encode(s_biz.bytes, s_biz.nbytes, out, sizeof(out));
    if (n < 0) {
        term_line(app, CG_TERM_SYS, "Base64 编码失败：缓冲放不下");
        set_status(app, "Base64 编码：失败");
        return;
    }
    term_line(app, CG_TERM_TX, "Base64 编码（%d 字节 → %d 字符）：%s", s_biz.nbytes, n, out);
    set_status(app, "Base64 编码：%d 字符", n);
}

/**
 * @brief 「Base64 解码」按钮：把输入框文本当 Base64 解开并显示字节
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_b64_dec_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app  = cg_app_of(self);
    cg_widget_t *edt  = cg_find(app, "edt_input");
    const char  *text = edt ? cg_widget_text(edt) : "";
    char         out[HX_BUF];
    int          n;

    (void)ev;
    (void)user;
    if (!text) text = "";
    n = cg_base64_decode(text, out, sizeof(out));
    if (n == -1) {
        term_line(app, CG_TERM_SYS, "Base64 解码失败：内容不是合法 Base64");
        set_status(app, "Base64 解码：失败");
        return;
    }
    if (n == -2) {
        term_line(app, CG_TERM_SYS, "Base64 解码失败：结果放不下");
        set_status(app, "Base64 解码：失败");
        return;
    }
    term_line(app, CG_TERM_TX, "Base64 解码（输入 %d 字符 → %d 字节）：", (int)strlen(text), n);
    output_bytes(app, out, n);
    set_status(app, "Base64 解码：%d 字节", n);
}

/**
 * @brief 「URL 编码」按钮：把输入框文本做百分号编码
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_url_enc_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app  = cg_app_of(self);
    cg_widget_t *edt  = cg_find(app, "edt_input");
    const char  *text = edt ? cg_widget_text(edt) : "";
    char         out[HX_BUF];
    int          n;

    (void)ev;
    (void)user;
    if (!text) text = "";
    n = cg_url_encode(text, 0, out, sizeof(out));
    if (n < 0) {
        term_line(app, CG_TERM_SYS, "URL 编码失败：输入太长，缓冲放不下");
        set_status(app, "URL 编码：失败");
        return;
    }
    term_line(app, CG_TERM_TX, "URL 编码（%d 字符 → %d 字符）：%s", (int)strlen(text), n, out);
    set_status(app, "URL 编码：%d 字符", n);
}

/**
 * @brief 「URL 解码」按钮：把输入框文本里的百分号转义还原
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_url_dec_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app  = cg_app_of(self);
    cg_widget_t *edt  = cg_find(app, "edt_input");
    const char  *text = edt ? cg_widget_text(edt) : "";
    char         out[HX_BUF];
    int          n;

    (void)ev;
    (void)user;
    if (!text) text = "";
    n = cg_url_decode(text, out, sizeof(out));
    if (n == -1) {
        term_line(app, CG_TERM_SYS, "URL 解码失败：'%' 后面不是两位十六进制");
        set_status(app, "URL 解码：失败");
        return;
    }
    if (n == -2) {
        term_line(app, CG_TERM_SYS, "URL 解码失败：结果放不下");
        set_status(app, "URL 解码：失败");
        return;
    }
    term_line(app, CG_TERM_TX, "URL 解码（%d 字符 → %d 字节）：%s", (int)strlen(text), n, out);
    set_status(app, "URL 解码：%d 字节", n);
}

/**
 * @brief 「HEX → 文本」按钮：把输入框当十六进制解成文本
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 * @note 非可打印字节按点显示：终端是行文本视图，控制字符直接显示会把界面搅坏
 */
static void on_hex2txt_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t     *app  = cg_app_of(self);
    cg_widget_t  *edt  = cg_find(app, "edt_input");
    const char   *text = edt ? cg_widget_text(edt) : "";
    unsigned char buf[HX_BYTES];
    char          shown[HX_BYTES + 1];
    int           n;
    int           i;

    (void)ev;
    (void)user;
    if (!text) text = "";
    n = cg_hex_decode(text, buf, sizeof(buf));
    if (n == -1) {
        term_line(app, CG_TERM_SYS, "HEX→文本失败：出现非十六进制字符，或只剩半个字节");
        set_status(app, "HEX→文本：失败");
        return;
    }
    if (n == -2) {
        term_line(app, CG_TERM_SYS, "HEX→文本失败：字节数超过上限 %d", HX_BYTES);
        set_status(app, "HEX→文本：失败");
        return;
    }
    for (i = 0; i < n; i++) {
        shown[i] = (buf[i] >= 0x20 && buf[i] < 0x7F) ? (char)buf[i] : '.';
    }
    shown[n] = '\0';
    term_line(app, CG_TERM_TX, "HEX→文本（%d 字节）：%s", n, shown);
    set_status(app, "HEX→文本：%d 字节", n);
}

/**
 * @brief 「文本 → HEX」按钮：把输入框文本编成十六进制串
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_txt2hex_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app  = cg_app_of(self);
    cg_widget_t *edt  = cg_find(app, "edt_input");
    const char  *text = edt ? cg_widget_text(edt) : "";
    char         out[HX_BUF];
    int          n;

    (void)ev;
    (void)user;
    if (!text) text = "";
    n = (int)strlen(text);
    if (n <= 0) {
        term_line(app, CG_TERM_SYS, "文本→HEX：输入是空的");
        set_status(app, "文本→HEX：无数据");
        return;
    }
    if (n > HX_BYTES) n = HX_BYTES;
    if (cg_hex_encode(text, n, " ", 1, out, sizeof(out)) < 0) {
        term_line(app, CG_TERM_SYS, "文本→HEX 失败：缓冲放不下");
        set_status(app, "文本→HEX：失败");
        return;
    }
    term_line(app, CG_TERM_TX, "文本→HEX（%d 字节）：%s", n, out);
    set_status(app, "文本→HEX：%d 字节", n);
}

/* ------------------------------------------------------------ 报文组装回调 */

/**
 * @brief 「组装协议帧」按钮：按固定骨架拼一帧并预览整帧 HEX
 * @param self 被点的按钮
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 * @details 骨架：AA 55 | 地址 01 | 功能码 03 | 长度 | 数据 | CRC16-Modbus(低字节在前) | 0D 0A。
 *          走 cg_proto_build，校验覆盖「校验值之前的全部字节」。
 *          长度字节最大 255，所以载荷超过 255 字节时只取前 255 个并注明。
 */
static void on_frame_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    static const unsigned char head[2] = { 0xAA, 0x55 };
    static const unsigned char tail[2] = { 0x0D, 0x0A };
    cg_app_t     *app   = cg_app_of(self);
    unsigned char frame[512];
    char          hex[HX_BUF];
    cg_proto_t    p;
    int           dlen;
    int           n;

    (void)ev;
    (void)user;
    parse_input(app);
    if (s_biz.nbytes <= 0) {
        term_line(app, CG_TERM_SYS, "组装协议帧：没有载荷数据");
        set_status(app, "组装协议帧：无数据");
        return;
    }

    dlen = s_biz.nbytes;
    if (dlen > 255) {
        dlen = 255;
        term_line(app, CG_TERM_SYS, "载荷超过 255 字节，受长度字节限制只取前 255 个");
    }

    memset(&p, 0, sizeof(p));
    p.head           = head;
    p.head_len       = 2;
    p.addr           = 0x01;
    p.func           = 0x03;
    p.with_len       = 1;
    p.data           = s_biz.bytes;
    p.data_len       = dlen;
    p.sum            = CG_SUM_CRC16_MODBUS;
    p.sum_big_endian = 0;      /* Modbus 那套：CRC 低字节在前 */
    p.tail           = tail;
    p.tail_len       = 2;

    n = cg_proto_build(&p, frame, sizeof(frame));
    if (n < 0) {
        term_line(app, CG_TERM_SYS, "组装失败：参数非法或缓冲放不下");
        set_status(app, "组装协议帧：失败");
        return;
    }
    if (cg_hex_encode(frame, n, " ", 1, hex, sizeof(hex)) < 0) {
        term_line(app, CG_TERM_SYS, "组装成功但 HEX 预览编码失败");
        set_status(app, "组装协议帧：%d 字节（预览失败）", n);
        return;
    }
    term_line(app, CG_TERM_SYS, "—— 报文组装（AA 55 | 01 | 03 | 长度 | 数据 | CRC16 | 0D 0A）——");
    term_line(app, CG_TERM_TX, "帧长 %d 字节：%s", n, hex);
    set_status(app, "组装协议帧：%d 字节", n);
}

/* ------------------------------------------------------------ 分隔条回调 */

/**
 * @brief 分隔条拖动结束：只在松手时记一次（拖动中不发，免得刷屏）
 * @param self 分隔条控件
 * @param ev 事件类型
 * @param user 挂在该控件上的业务数据，本示例不用
 * @return void 无返回值
 */
static void on_split_release(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    set_status(cg_app_of(self), "分隔条 x=%d：左栏撑开、终端让位", cg_splitter_pos(self));
}

/* ------------------------------------------------------------ 启动 */

/**
 * @brief 界面就绪钩子：预填一段样例、解析一次并给出开场说明
 * @param app 应用
 * @param user 就绪钩子传进来的业务数据，本示例不用
 * @return void 无返回值
 * @note 预填样例让 --selftest 合成的每个回调都跑得到真实数据，且全程不弹窗
 */
void ht_init(cg_app_t *app, void *user)
{
    cg_widget_t *edt = cg_find(app, "edt_input");

    (void)user;
    if (edt) cg_widget_set_text(edt, "48 65 6C 6C 6F 20 43 53 74 61 63 6B");

    parse_input(app);
    term_line(app, CG_TERM_SYS, "进制与校验工具箱就绪：左栏填内容，点「解析字节」看结果。");
    term_line(app, CG_TERM_SYS, "提示：勾选「按 HEX 解析」时，空格 / 逗号 / 连字符都能当分隔符。");
    show_byte_view(app);
    show_checksums(app);
    set_status(app, "就绪");
}

const cg_binding_t ht_bindings[] = {
    CG_BIND("ht_on_input_change",   on_input_change),
    CG_BIND("ht_on_mode_change",    on_mode_change),
    CG_BIND("ht_on_parse_click",    on_parse_click),
    CG_BIND("ht_on_b64_enc_click",  on_b64_enc_click),
    CG_BIND("ht_on_b64_dec_click",  on_b64_dec_click),
    CG_BIND("ht_on_hex2txt_click",  on_hex2txt_click),
    CG_BIND("ht_on_url_enc_click",  on_url_enc_click),
    CG_BIND("ht_on_url_dec_click",  on_url_dec_click),
    CG_BIND("ht_on_txt2hex_click",  on_txt2hex_click),
    CG_BIND("ht_on_frame_click",    on_frame_click),
    CG_BIND("ht_on_split_release",  on_split_release),
    CG_BIND_END
};

/**
 * @brief 入口：登记绑定与 .cgui 路径后交给运行时
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组：argv[1] 是 .cgui 路径，写 "--selftest" 则跑不开窗自检
 * @return int 进程退出码：运行 / 自检的返回值，0 = 正常
 */
