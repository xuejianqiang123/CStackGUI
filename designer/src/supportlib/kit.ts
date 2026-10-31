/* supportlib/kit.ts -- 「基础能力支持库」页签的索引数据
 *
 * 数据唯一真相是 runtime/include/cstackgui.h —— 本文件只做分组与中文说明。
 * 改完跑 node tools\check-support.mjs 对齐头文件（表里写了不存在的函数会红）。
 *
 * 2026-10-31 从 designer/src/supportlib.ts（1757 行）按顶层库拆出：
 * 单文件 131KB 时定位一条命令要滚很久，而三个库之间没有任何耦合。
 */
import { c } from "./types.js";
import type { SupLib } from "./types.js";

export const KIT_LIB: SupLib = {
    key: "kit",
    label: "基础能力支持库",
    hint: "文件、路径、配置(INI)、文本编码、剪贴板、外部程序、注册表、串口",
    groups: [
      {
        label: "文件",
        cmds: [
          c("cg_file_exists", "文件是否存在", "int cg_file_exists(const char *path)", "1 = 存在（目录不算）", {
            sample: `if (!cg_file_exists("config.ini")) cg_log(CG_LOG_INFO, "还没有配置文件，用默认值");`,
          }),
          c("cg_file_read", "读整个文件", "char *cg_file_read(const char *path)", "返回堆串（自动去掉 UTF-8 BOM）；失败 NULL", {
            params: ["用完记得 cg_free()"],
            ret: "文件内容；打不开返回 NULL",
            sample: `char *text = cg_file_read(path);
if (text) {
    cg_widget_set_text(cg_find(cg_app_of(self), "edt_body"), text);
    cg_free(text);
}`,
          }),
          c("cg_file_read_bytes", "读整个二进制文件（固件 / 镜像 / 抓包）", "unsigned char *cg_file_read_bytes(const char *path, int *out_len)",
            "**含 0x00 的数据必须用它** —— `cg_file_read` 返回的是字符串，`strlen` 会在第一个 0 处截断。0 字节文件也返回非 NULL（靠这个区分「空文件」与「打不开」）；不去 BOM", {
              params: ["path：文件路径", "out_len：长度出口（可传 NULL）；失败时不动"],
              ret: "堆缓冲（用完 cg_free）；打不开返回 NULL",
              sample: `int n = 0;\nunsigned char *fw = cg_file_read_bytes(fw_path, &n);\nif (!fw) { cg_log(CG_LOG_ERROR, "固件打不开"); return; }\ncg_log(CG_LOG_INFO, "固件 %d 字节，CRC32 = %08X", n, cg_crc32(fw, n));\ncg_free(fw);`,
            }),
          c("cg_file_write", "写文件（覆盖）", "int cg_file_write(const char *path, const char *text)", "整体覆盖成 UTF-8 文本；成功 0", {
            sample: `cg_file_write(path, cg_widget_text(cg_find(cg_app_of(self), "edt_body")));`,
          }),
          c("cg_file_write_bytes", "写二进制文件（覆盖）", "int cg_file_write_bytes(const char *path, const void *data, int len)",
            "按长度写，**数据里带 0x00 也不会截断**；len 传 0 = 建一个空文件。成功 0", {
              sample: `unsigned char frame[8] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B };\ncg_file_write_bytes("frame.bin", frame, (int)sizeof frame);`,
            }),
          c("cg_file_size", "问文件大小", "long long cg_file_size(const char *path)",
            "想「不读内容先问大小」时用它；不存在 / 目录 / 调不动返回 -1", {
              sample: `long long n = cg_file_size(path);\nif (n < 0) cg_status_set(sb, 0, "文件读不到");\nelse       cg_status_set(sb, 0, cg_str_format("%lld 字节", n));`,
            }),
          c("cg_file_append", "追加到文件", "int cg_file_append(const char *path, const char *text)", "加到末尾，文件不存在就新建（写日志常用）", {
            sample: `cg_file_append("log.txt", cg_str_format("%s 打开 %s\\r\\n", cg_time_now(NULL), path));`,
          }),
          c("cg_file_delete", "删文件", "int cg_file_delete(const char *path)", "成功返回 0"),
          c("cg_file_copy", "拷文件", "int cg_file_copy(const char *from, const char *to)", "覆盖目标；成功返回 0"),
          c("cg_dir_exists", "目录是否存在", "int cg_dir_exists(const char *path)", ""),
          c("cg_dir_create", "建目录", "int cg_dir_create(const char *path)", "父目录必须已存在；已存在也算成功"),
          c("cg_dir_list", "枚举目录", "int cg_dir_list(const char *dir, const char *filter, int index, char *out, int out_size)", "index 从 0 递增取文件名；返回 0 表示没有了", {
            params: ["filter：\"*.txt\" 这种；传 NULL 或 \"*\" 表示全部"],
            sample: `char name[256];
for (int i = 0; cg_dir_list("C:\\\\data", "*.txt", i, name, sizeof(name)); i++) {
    cg_widget_list_add(cg_find(cg_app_of(self), "lst_files"), name);
}`,
          }),
        ],
      },
      {
        label: "路径",
        cmds: [
          c("cg_path_join", "拼路径", "char *cg_path_join(const char *a, const char *b)", "自动补分隔符，返回堆串"),
          c("cg_path_dir", "取目录", "char *cg_path_dir(const char *path)", "去掉文件名部分，返回堆串"),
          c("cg_path_name", "取文件名", "const char *cg_path_name(const char *path)", "**返回内部指针，不用 cg_free**", {
            sample: `cg_widget_set_text(lab, cg_path_name("C:\\\\data\\\\a.txt"));   /* a.txt */`,
          }),
          c("cg_path_ext", "取扩展名", "const char *cg_path_ext(const char *path)", "含点（\".txt\"）；没有返回空串。内部指针，不用释放"),
          c("cg_app_dir", "程序所在目录", "char *cg_app_dir(void)", "exe 所在目录，读同目录的配置文件用"),
          c("cg_app_data_dir", "用户数据目录", "char *cg_app_data_dir(void)", "%LOCALAPPDATA%\\<exe名>（会自动建），存配置/缓存用", {
            sample: `char *dir = cg_app_data_dir();
char *ini = cg_path_join(dir, "config.ini");
cg_ini_write(ini, "窗口", "宽", "800");
cg_free(ini);
cg_free(dir);`,
          }),
        ],
      },
      {
        label: "配置（INI）",
        cmds: [
          c("cg_ini_read", "读配置项", "char *cg_ini_read(const char *path, const char *section, const char *key, const char *def)", "读 [节] 下的 键=值；没有就用 def（返回的堆串要 cg_free）", {
            sample: `char *w = cg_ini_read("config.ini", "窗口", "宽", "800");
int width = atoi(w);
cg_free(w);`,
          }),
          c("cg_ini_write", "写配置项", "int cg_ini_write(const char *path, const char *section, const char *key, const char *value)", "改一项（文件里其它内容保持不动），节/键不存在就补上；成功 0", {
            sample: `/* 关窗时记住窗口大小 */
cg_ini_write("config.ini", "窗口", "宽", cg_str_format("%d", cg_widget_w(w)));`,
          }),
        ],
      },
      {
        label: "文本与编码",
        cmds: [
          c("cg_str_format", "拼字符串", "char *cg_str_format(const char *fmt, ...)", "printf 风格拼好返回堆串（省一个临时缓冲）", {
            sample: `char *msg = cg_str_format("共 %d 个文件，合计 %d KB", n, kb);
cg_message_box(self, msg, "统计", 0);
cg_free(msg);`,
          }),
          c("cg_str_dup", "复制字符串", "char *cg_str_dup(const char *s)", "留下一个副本（NULL 进 NULL 出），返回堆串，用完 cg_free"),
          c("cg_str_trim", "去首尾空白", "char *cg_str_trim(const char *s)", "去掉两端空格/制表/换行，返回堆串"),
          c("cg_str_equal", "比较字符串", "int cg_str_equal(const char *a, const char *b)", "忽略英文大小写；相等返回 1"),
          c("cg_str_replace", "替换子串", "char *cg_str_replace(const char *src, const char *from, const char *to)", "把 src 里所有 from 换成 to，返回堆串"),
          c("cg_utf8_to_gbk", "UTF-8 转 GBK", "char *cg_utf8_to_gbk(const char *utf8)", "写老式 ANSI/GBK 文件前转一下，否则中文会乱"),
          c("cg_gbk_to_utf8", "GBK 转 UTF-8", "char *cg_gbk_to_utf8(const char *gbk)", "读进来的 GBK 老文件转成界面能显示的中文", {
            sample: `char *raw = cg_file_read("老文件.txt");       /* 可能是 GBK */
char *u8  = cg_gbk_to_utf8(raw);
cg_widget_set_text(edt, u8);
cg_free(u8);
cg_free(raw);`,
          }),
          c("cg_hex_encode", "字节 → 十六进制文本", "int cg_hex_encode(const void *data, int len, const char *sep, int upper, char *out, int out_size)",
            "把字节拼成 \"1A 2B\" 这种文本（发给对端或显示用）；sep 是分隔串（NULL 或 \"\" = 不隔），upper 非 0 用大写", {
              params: ["data / len：字节缓冲与长度", "sep：分隔串，显示常用 \" \"", "upper：非 0 用大写", "out / out_size：需要 len*(2+分隔串长)+1 字节"],
              ret: "写出的字符数；缓冲放不下返回 -1",
              sample: `char line[1200];
int  n = cg_hex_encode(bytes, len, " ", 1, line, (int)sizeof(line));
if (n > 0) cg_widget_list_add(cg_find(app, "lst_log"), line);`,
            }),
          c("cg_hex_decode", "十六进制文本 → 字节", "int cg_hex_decode(const char *text, void *out, int out_size)",
            "空格/制表/换行/逗号/连字符都当分隔符，大小写都认，\"1A2B\" 这种不写分隔的也算对", {
              params: ["text：像 \"1A 2B 2C\" 或 \"1A2B2C\"", "out / out_size：输出字节缓冲"],
              ret: "字节数；非法字符或只剩半个字节返回 -1；放不下返回 -2",
              sample: `unsigned char bytes[512];
int n = cg_hex_decode(cg_widget_text(cg_find(app, "edt_send")), bytes, (int)sizeof(bytes));
if (n < 0) cg_log(CG_LOG_WARN, "HEX 写错了：只允许 0-9 A-F，且数字个数要是偶数");
else       cg_serial_write(ser, bytes, n);`,
            }),
          c("cg_base64_encode", "Base64 编码", "int cg_base64_encode(const void *data, int len, char *out, int out_size)",
            "RFC 4648，带 '=' 填充；out 需要 ((len+2)/3)*4+1 字节；写不下返回 -1", {
              sample: `char b64[8192];
cg_base64_encode(json, (int)strlen(json), b64, (int)sizeof(b64));`,
            }),
          c("cg_base64_decode", "Base64 解码", "int cg_base64_decode(const char *text, void *out, int out_size)",
            "空白与换行忽略；**带不带 '=' 填充都认**（JWT 那类不填充的也能解）；长度不合法返回 -1", {
              sample: `unsigned char raw[8192];
int n = cg_base64_decode(body, raw, (int)sizeof(raw));`,
            }),
          c("cg_url_encode", "URL 编码", "int cg_url_encode(const char *text, int space_plus, char *out, int out_size)",
            "不编码 A-Za-z0-9-_.~，其余按**字节**编成 %XX（UTF-8 中文会变成多个 %XX）；space_plus 非 0 时空格编成 '+'", {
              sample: `char q[1024];
cg_url_encode("关键词=中文", 0, q, (int)sizeof(q));`,
            }),
          c("cg_url_decode", "URL 解码", "int cg_url_decode(const char *text, char *out, int out_size)",
            "'%XX' 与 '+'（'+' 还原成空格）；'%' 后面不是两位十六进制返回 -1", {
              sample: `char u8[1024];
cg_url_decode("%E4%B8%AD%E6%96%87", u8, (int)sizeof(u8));   /* -> 中文 */`,
            }),
          c("cg_int_to_base", "整数 → 任意进制文本", "int cg_int_to_base(long long v, int base, char *out, int out_size)",
            "2..36 进制都行，输出大写（跟数据手册一致）；负数带前导 '-'。工控里看寄存器值常用", {
              params: ["v：要转换的整数", "base：2..36", "out / out_size：输出缓冲（64 位整数最多 66 字符）"],
              ret: "写出的字符数；base 非法或放不下返回 -1",
              sample: `char buf[80];
cg_int_to_base(255, 16, buf, (int)sizeof(buf));   /* -> "FF" */
cg_int_to_base(0xFF, 2, buf, (int)sizeof(buf));   /* -> "11111111" */`,
            }),
          c("cg_base_to_int", "任意进制文本 → 整数", "int cg_base_to_int(const char *s, int base, long long *out)",
            "认 '-/+' 与 0x / 0b / 0o 前缀（只在与进制相符时），允许首尾空白；整串必须合法、溢出算失败", {
              params: ["s：像 \"FF\"、\"0x1A\"、\"0b1010\"", "base：2..36", "out：结果；可传 NULL（只想问合不合法）"],
              ret: "1 = 成功；0 = 失败（此时 out 不动）",
              sample: `long long v;
if (!cg_base_to_int(cg_widget_text(cg_find(app, "edt"), 16, &v))
    cg_log(CG_LOG_WARN, "不是合法的十六进制数");`,
            }),
          c("cg_base_convert", "进制互转", "int cg_base_convert(const char *s, int from_base, int to_base, char *out, int out_size)",
            "字符串进、字符串出 —— 不想自己在中间接一个 long long 时用它", {
              sample: `char buf[80];
cg_base_convert("FF", 16, 2, buf, (int)sizeof(buf));   /* -> "11111111" */`,
            }),
          c("cg_to_signed", "按位宽读补码", "long long cg_to_signed(unsigned long long v, int bits)",
            "报文给你 0x…、协议说的是「N 位有符号」时用它：cg_to_signed(0xFFF6, 12) = -10。位宽不同结论不同（同一个数按 16 位读是 65526）", {
              sample: `long long t = cg_to_signed(0xFFF6u, 12);   /* 温度就写进报文里的 12 位有符号 */`,
            }),
          c("cg_from_signed", "写成指定位宽补码", "unsigned long long cg_from_signed(long long v, int bits)",
            "反向：拼帧时用 —— 负数不能直接塞进报文，得先写成补码", {
              sample: `unsigned v = (unsigned)cg_from_signed(-10, 12);   /* -> 0xFF6 */`,
            }),
          c("cg_free", "释放内存", "void cg_free(void *p)", "上面这些返回的字符串都要用它释放（等价 free）"),
        ],
      },
      {
        label: "字节操作",
        cmds: [
          c("cg_bytes_reverse", "反转字节序", "void cg_bytes_reverse(void *data, int len)",
            "原地反转（精易的「字节集_反转」）—— 小端设备发的多字节字段要翻过来看 / 翻过去发。len ≤ 1 或 data 传 NULL 时什么都不做"),
          c("cg_bytes_slice", "取一段字节（取左 / 取右 / 取中）", "int cg_bytes_slice(const void *data, int len, int from, int count, void *out, int out_size)",
            "count 传 **-1 = 取到底**（最常用）；count 超剩余时按剩余算 —— 「取右边」用的就是这个语义", {
              params: ["from：起点（0 起）；越界返回 -1", "count：取几个字节；-1 = 取到底", "out / out_size：输出缓冲"],
              ret: "写出的字节数；from 非法 -1、缓冲放不下 -2",
              sample: `unsigned char head[4];\n/* 取前 4 字节（很多私有协议的帧头）*/\nif (cg_bytes_slice(rx, n, 0, 4, head, (int)sizeof(head)) != 4)\n    cg_log(CG_LOG_WARN, "帧太短");`,
            }),
          c("cg_bytes_find", "找子集", "int cg_bytes_find(const void *data, int len, const void *needle, int nlen, int from)",
            "返回第一次出现的下标（可从 from 往后接着找）；没找到 / 参数非法 / needle 为空返回 -1", {
              sample: `int i = cg_bytes_find(rx, n, "\\xAA\\x55", 2, 0);   /* 找同步头 */\nif (i >= 0) cg_log(CG_LOG_INFO, "同步头在 %d", i);`,
            }),
          c("cg_bytes_get_u16", "按字节序读 16 位", "int cg_bytes_get_u16(const void *data, int len, int offset, int big_endian)",
            "big_endian 非 0 = 大端（网络序 / Modbus 寄存器序），0 = 小端。越界返回 -1（16 位值到不了 -1，所以不撞车）"),
          c("cg_bytes_get_u32", "按字节序读 32 位", "long long cg_bytes_get_u32(const void *data, int len, int offset, int big_endian)",
            "越界返回 -1。**用 long long 返回**：若用 int，`0xFFFFFFFF` 正好也是 -1，错误与合法值撞车、调用方没法分辨"),
          c("cg_bytes_put_u16", "按字节序写 16 位", "int cg_bytes_put_u16(void *out, int out_size, int offset, unsigned v, int big_endian)",
            "拼帧用。**越界时什么都不写**（半个字段写进帧里比报错更难查）", {
              sample: `unsigned char f[8];\nf[0] = 0x01; f[1] = 0x03;                     /* 从站地址 + 功能码 */\ncg_bytes_put_u16(f, 8, 2, 0x0000, 1);         /* 寄存器地址（大端）*/\ncg_bytes_put_u16(f, 8, 4, 0x0002, 1);         /* 数量 */\nunsigned crc = cg_crc16(f, 6);                /* CRC 挂帧尾、低字节在前 */\nf[6] = (unsigned char)(crc & 0xFF); f[7] = (unsigned char)(crc >> 8);`,
            }),
          c("cg_bytes_put_u32", "按字节序写 32 位", "int cg_bytes_put_u32(void *out, int out_size, int offset, unsigned v, int big_endian)",
            "同 cg_bytes_put_u16：越界返回 0 且什么都不写"),
        ],
      },
      {
        label: "无线状态（WiFi / 蓝牙）",
        cmds: [
          c("cg_wifi_count", "有几块无线网卡", "int cg_wifi_count(void)",
            "0 = 本机没有无线网卡，或 WLAN 服务没起（原因在日志里）。IoT 上位机先看这一眼：设备连不上时先排除\"我这边的无线状态不对\""),
          c("cg_wifi_info", "无线网卡状态", "int cg_wifi_info(int idx, cg_wifi_info_t *out)",
            "取第 idx 块网卡：描述 / 状态 / 当前 SSID / 信号（0..100，没连是 -1）。**零新增链接依赖** —— 符号运行期从 wlanapi.dll 取", {
              params: ["idx：第几块（0 起）", "out：cg_wifi_info_t"],
              ret: "0 = 成功；-1 = 越界 / wlanapi 取不到",
              sample: `cg_wifi_info_t w;\nfor (int i = 0; i < cg_wifi_count(); i++)\n    if (cg_wifi_info(i, &w) == 0)\n        cg_log(CG_LOG_INFO, "%s：%s  信号 %d%%", w.description,\n               w.ssid[0] ? w.ssid : "(未连接)", w.signal);`,
            }),
          c("cg_wifi_scan", "让系统扫一次无线（异步）", "int cg_wifi_scan(void)",
            "**不等待**：几秒后结果自己出现在 cg_wifi_networks() 里。要立刻刷新周边 AP 列表就先调它。返回 0 = 已提交；-1 = 没网卡 / 系统不支持（见下方 cg_wifi_networks 的说明）",
            { ret: "0 = 已提交；-1 = 取不到" }),
          c("cg_wifi_networks", "周边可见网络列表", "int cg_wifi_networks(cg_wifi_net_t *out, int cap, int *count)",
            "同名的多个 BSSID 已合并（取信号最强的）。数据来自系统**最近一次扫描的结果** ⇒ 想立刻刷新就先调 cg_wifi_scan()（几秒后再读）；不调也行，Windows 自己会定期重扫", {
              params: ["out / cap：数组与容量", "count：出口，给**实际看到**的个数（可能比 cap 大）"],
              ret: "0 = 成功；-1 = 没有无线网卡或取不到",
              sample: `cg_wifi_net_t nets[24]; int n = 0;\nif (cg_wifi_networks(nets, 24, &n) == 0)\n    for (int i = 0; i < n; i++)\n        cg_log(CG_LOG_INFO, "  %s  %d%%%s", nets[i].ssid, nets[i].signal,\n               nets[i].connected ? "（当前）" : "");`,
            }),
          c("cg_ble_radio_count", "有几个蓝牙无线电", "int cg_ble_radio_count(void)",
            "0 = 没有蓝牙或蓝牙关着。符号运行期从 BluetoothApis.dll 取（不在 bthprops.cpl 里）"),
          c("cg_ble_radio_info", "蓝牙无线电信息", "int cg_ble_radio_info(int idx, cg_ble_radio_t *out)",
            "名字 + 本机蓝牙地址。**零新增链接依赖**", { ret: "0 = 成功；-1 = 越界 / 取不到" }),
          c("cg_ble_device_count", "有几个已知蓝牙设备", "int cg_ble_device_count(void)",
            "已配对 / 记得过的设备数。**不做主动搜索** —— 那要几十秒，且属于\"发现\"不是\"状态\""),
          c("cg_ble_device_info", "已知蓝牙设备信息", "int cg_ble_device_info(int idx, cg_ble_device_t *out)",
            "名字 / 地址（与 Windows 设置里显示的一致）/ 是否连着 / 是否配对过。板子在不在列表里，看它", {
              ret: "0 = 成功；-1 = 越界 / 取不到",
              sample: `cg_ble_device_t d;\nfor (int i = 0; i < cg_ble_device_count(); i++)\n    if (cg_ble_device_info(i, &d) == 0)\n        cg_log(CG_LOG_INFO, "[%d] %s  %s  已连接=%d", i, d.name, d.address, d.connected);`,
            }),
          c("cg_ble_find_device", "按地址/名字找已知设备", "int cg_ble_find_device(const char *addr_or_name, cg_ble_device_t *out)",
            "能解析成地址就按地址精确比，否则按名字做**不区分大小写的子串**匹配（现场记的名字常常只是设备名的一段）。链接设备前用它把地址对上",
            { ret: "0 = 找到；-1 = 没找到" }),
          c("cg_ble_addr_str", "BLE 地址 -> 文本", "void cg_ble_addr_str(unsigned long long addr, char *out, int out_size)",
            "与 Windows 设置一致：大写、冒号分隔（out 需要 18 字节）。**对外统一用一个 64 位整数表示地址** —— 结构体里那个 rgBytes 是**小端**，摆错就永远连不上，换算由框架负责"),
          c("cg_ble_addr_from_str", "文本 -> BLE 地址", "int cg_ble_addr_from_str(const char *text, unsigned long long *out)",
            "分隔符随便（冒号 / 连字符 / 空格 / 不写），大小写都认，但**必须正好 6 字节** —— 少一位算写错，不\"尽量猜\"",
            { ret: "0 = 成功；-1 = 格式不对" }),
          c("cg_ble_uuid16_str", "短 UUID -> 标准基文本", "int cg_ble_uuid16_str(unsigned short uuid16, char *out, int out_size)",
            "把手册上的 \"FFF1\" 写成协议栈那种 \"0000fff1-0000-1000-8000-00805f9b34fb\"（**小写**：蓝牙文档与 Windows 都这么写）",
            { ret: "36（写入长度）；放不下返回 -1" }),
          c("cg_ble_uuid16_parse", "文本 -> 16 位短 UUID", "int cg_ble_uuid16_parse(const char *text, unsigned short *out16)",
            "认 \"fff1\" / \"0xfff1\" / 长写法（带不带大括号都行）。**不是标准基形态的 128 位 UUID 返回 -1** —— 那不是 16 位能表达的",
            { ret: "0 = 成功；-1 = 认不出来" }),
          c("cg_ble_uuid_equal", "两个 UUID 是不是同一个", "int cg_ble_uuid_equal(const char *a, const char *b)",
            "能当 16 位解的两边按 16 位比 —— 所以 **\"fff1\" 与它那个长写法相等**（现场最常见的用法：拿手册的短 UUID 比设备报的长 UUID）",
            { ret: "1 = 相等；0 = 不等或认不出来" }),
          c("cg_ble_attr_flags", "特征属性 -> 能做什么", "int cg_ble_attr_flags(unsigned properties)",
            "把特征的那个 properties 字解成 CG_BLE_ATTR_READ / WRITE / WRITE_NR / NOTIFY / INDICATE 的按位或 —— 能不能读、能不能订阅，看它",
            {
              sample: `int f = cg_ble_attr_flags(props);\nif (f & CG_BLE_ATTR_NOTIFY) cg_log(CG_LOG_INFO, "这个特征可以订阅通知");`,
            }),
        ],
      },
      {
        label: "协议（Modbus）",
        cmds: [
          c("cg_modbus_build_read", "拼读请求帧（Modbus）", "int cg_modbus_build_read(int tcp, int tid, int slave, int func, int start, int count, void *out, int out_size)",
            "线圈 01 / 离散输入 02 / 保持寄存器 03 / 输入寄存器 04。**纯函数**：不碰串口网口、不持状态。tcp 传 CG_MODBUS_RTU(0) / CG_MODBUS_TCP(1)；RTU 会自动挂 CRC16（低字节在前），TCP 会带 MBAP 头（tid 是事务号，RTU 时忽略）。返回帧长度，参数非法或 out 放不下返回 -1", {
              params: ["tcp / tid：封装方式与事务号", "slave：从站 0..255；start：起始地址 0..65535", "count：位类 1..2000、寄存器类 1..125（规范上限）"],
              ret: "写入 out 的帧长度（RTU 8 / TCP 12）；-1 = 参数非法或放不下",
              sample: `unsigned char frame[8];\nint n = cg_modbus_build_read(CG_MODBUS_RTU, 0, 0x01, 0x03, 0x0000, 10, frame, sizeof frame);\n/* n = 8，frame = 01 03 00 00 00 0A C5 CD */\ncg_serial_write(ser, frame, n);`,
            }),
          c("cg_modbus_build_write", "拼写请求帧（Modbus）", "int cg_modbus_build_write(int tcp, int tid, int slave, int func, int start, const unsigned short *vals, int nvalues, void *out, int out_size)",
            "单线圈 05 / 单寄存器 06 / 多线圈 0F / 多寄存器 10。05/06 时 nvalues 必须为 1；0F/10 时 ≤ 123 个。**05 收 1/0 也认**（替你换成线上的 FF00/0000）。返回帧长度，参数非法返回 -1", {
              params: ["vals：要写的值（05/0F 里 1 = 合、0 = 断）", "nvalues：个数"],
              ret: "帧长度；-1 = 参数非法",
              sample: `unsigned short vals[2] = { 0x0102, 0x0304 };\nunsigned char  frame[16];\nint n = cg_modbus_build_write(CG_MODBUS_RTU, 0, 0x01, 0x10, 0x0001, vals, 2, frame, sizeof frame);\n/* 01 10 00 01 00 02 04 01 02 03 04 + CRC */`,
            }),
          c("cg_modbus_parse", "解响应帧（Modbus）", "int cg_modbus_parse(int tcp, const void *frame, int len, cg_modbus_rsp_t *out)",
            "读响应 / 写响应 / 异常响应都认，结果填进 cg_modbus_rsp_t（slave / func / exception / addr / nvalues / values）。**异常响应会替你剥掉 0x80**：out->func 给的是原功能码、out->exception 是异常码。RTU 会先校验 CRC", {
              params: ["tcp：CG_MODBUS_RTU / CG_MODBUS_TCP", "out：cg_modbus_rsp_t（值最多 CG_MODBUS_MAX_VALUES = 125 个）"],
              ret: "0 = 成功；-1 = 帧太短/参数非法；**-2 = CRC 不对**（线上数据坏了）；-3 = 格式不对（协议标识 / 长度 / 功能码 / 字节数对不上）",
              sample: `cg_modbus_rsp_t r;\nint ret = cg_modbus_parse(CG_MODBUS_RTU, buf, n, &r);\nif (ret == -2)      cg_log(CG_LOG_ERROR, "帧 CRC 不对，线上数据坏了");\nelse if (ret != 0)  cg_log(CG_LOG_ERROR, "响应格式不对（%d）", ret);\nelse if (r.exception) cg_log(CG_LOG_ERROR, "从站报异常码 %d", r.exception);\nelse for (int i = 0; i < r.nvalues; i++) cg_log(CG_LOG_INFO, "寄存器 %d = %u", i, r.values[i]);`,
            }),
        ],
      },
      {
        label: "字节流切帧 / 报文组装（串口 / TCP）",
        cmds: [
          c("cg_frame_init", "建切帧器（四种切法）", "int cg_frame_init(cg_frame_t *f, void *buf, int buf_size, int mode, int arg)",
            "串口和 TCP 给的都是字节流：一次读到半帧、一帧、也可能两帧粘一起。四种定界办法收成一个状态机：CG_FRAME_SILENCE（帧间静默，Modbus RTU 的 t3.5）/ CG_FRAME_FIXED（定长）/ CG_FRAME_DELIM（分隔符，留在帧里）/ CG_FRAME_LENFIELD（头里带长度，MBAP 就是它）。**纯状态机**：时间由调用方给，不碰 I/O", {
              params: ["buf / buf_size：调用方给的攒帧缓冲（够放最大的一帧）", "mode：CG_FRAME_*；arg：SILENCE = 静默毫秒 / FIXED = 每帧字节数 / DELIM = 结束字节 / LENFIELD = 长度字段偏移"],
              ret: "0 = 成功；-1 = 参数不合法",
              sample: `static cg_frame_t    fr;\nstatic unsigned char buf[512];\n\n/* Modbus/RTU：帧间静默 ≥ 3.5 个字符时间（9600 下约 4ms）*/\ncg_frame_init(&fr, buf, sizeof buf, CG_FRAME_SILENCE, 4);\ncg_frame_limits(&fr, 4, 256);      /* 短于 4 字节不像一帧，超过 256 当垃圾 */`,
            }),
          c("cg_frame_feed", "喂入刚收到的字节", "int cg_frame_feed(cg_frame_t *f, const void *data, int len, unsigned long now_ms)",
            "把 cg_serial_read / cg_net_read 拿到的字节原样喂进来，now_ms 传 cg_now_ms()。静默模式下**这一句会顺手把上一帧收尾**（下个字节到来时发现空档）", {
              params: ["data / len：刚收到的字节", "now_ms：当前毫秒"],
              ret: "0 = 已收下；-1 = 参数非法或缓冲被没取走的帧占满（先 take）",
              sample: `char buf[4096];\nint  n = cg_serial_read(ser, buf, sizeof buf);\nif (n > 0) cg_frame_feed(&fr, buf, n, cg_now_ms());`,
            }),
          c("cg_frame_take", "取一帧（切好的整帧）", "int cg_frame_take(cg_frame_t *f, void *out, int cap)",
            "按到达顺序取。取到的一帧**不补 NUL**，长度就是返回值", {
              ret: "> 0 = 帧长（已写进 out）；0 = 还没有完整帧；-1 = out 放不下（帧仍在队列里）",
              sample: `unsigned char frame[512];\nint flen;\nwhile ((flen = cg_frame_take(&fr, frame, sizeof frame)) > 0) {\n    cg_modbus_rsp_t r;\n    if (cg_modbus_parse(CG_MODBUS_RTU, frame, flen, &r) == 0) { /* 用它 */ }\n}`,
            }),
          c("cg_frame_tick", "静默收尾（配合定时器）", "void cg_frame_tick(cg_frame_t *f, unsigned long now_ms)",
            "对端只发一帧就不再发时，靠它让最后一帧立刻出来（有新的字节到来时同样会发现空档，所以没有定时器也能用）", { ret: "无" }),
          c("cg_frame_reset", "丢掉半帧与没取走的帧", "void cg_frame_reset(cg_frame_t *f)",
            "「清空接收」「断开重连」时用。**统计量不清零**（累计量）", { ret: "无" }),
          c("cg_frame_limits", "一帧的长短界", "void cg_frame_limits(cg_frame_t *f, int min_len, int max_len)",
            "低于 min_len 当噪声丢掉、高于 max_len 整帧丢掉，都记进 cg_frame_dropped。max_len 不许超过缓冲大小", { ret: "无" }),
          c("cg_frame_lenfield", "长度字段模式的细节", "void cg_frame_lenfield(cg_frame_t *f, int width, int little_endian, int add)",
            "缺省按 MBAP 的形状（宽 2、大端、帧总长 = 长度值 + 偏移 + 2）。要 1 字节长度 / 小端 / 别的加数时改这里", { ret: "无" }),
          c("cg_frame_dropped", "一共丢了多少字节", "unsigned long cg_frame_dropped(const cg_frame_t *f)",
            "噪声、超上限、队列满都算。**丢要丢得看得见**：界面上显示这个数，才知道线上是不是在乱", { ret: "累计字节数" }),
          c("cg_frame_frames", "一共切出多少帧", "unsigned long cg_frame_frames(const cg_frame_t *f)",
            "累计值（含还没取走的）", { ret: "累计帧数" }),
          c("cg_now_ms", "毫秒时基（系统 uptime）", "unsigned long cg_now_ms(void)",
            "切帧、超时判定、去抖都用它。与 cg_time_now()（给人看的格式化时间）不是一回事", { ret: "自系统启动起的毫秒数" }),
          c("cg_proto_build", "拼一帧私有协议报文", "int cg_proto_build(const cg_proto_t *p, void *out, int out_size)",
            "通用骨架：**帧头 / 地址 / 功能码 /[数据长度]/ 数据 / 校验 / 帧尾**，哪几段有你自己定。校验五种：CG_SUM_NONE / ADD8 / XOR8 / ADD16 / CRC16_MODBUS（低字节在前＝Modbus 写法）。校验覆盖「校验值之前的全部字节」", {
              params: ["p：cg_proto_t，**先 memset 成 0** 再填要用的段", "地址 / 功能码传 -1 = 这一帧不写这一段", "帧头 / 数据 / 帧尾长度传 0 = 没有这一段"],
              ret: "帧长（写进 out）；-1 = 参数非法或 out 放不下",
              sample: `unsigned char frame[64], data[3] = { 1, 2, 3 };\ncg_proto_t    p;\n\nmemset(&p, 0, sizeof p);\np.head = "\\xAA\\x55"; p.head_len = 2;\np.addr = 0x01; p.func = 0x03;\np.data = data; p.data_len = 3;\np.sum  = CG_SUM_ADD8;          /* 校验 = 帧头+地址+功能码+数据的累加和 */\np.tail = "\\r\\n"; p.tail_len = 2;\n\nint n = cg_proto_build(&p, frame, sizeof frame);   /* AA 55 01 03 01 02 03 09 0D 0A */\ncg_serial_write(ser, frame, n);`,
            }),
          c("cg_proto_parse", "按同一骨架验一帧", "int cg_proto_parse(const cg_proto_t *p, const void *frame, int len, int *out_addr, int *out_func, const unsigned char **out_data, int *out_data_len)",
            "收帧那一侧：核对帧头 / 地址 / 功能码 / 帧尾 / 长度字节，再算一遍校验。**地址不匹配会返回 -3** —— 所以它顺手替你筛掉了\"不是发给这一台的\"帧", {
              params: ["p：与组帧时同一份骨架（data_len 不参与判断）", "out_*：不想看的那一段传 NULL"],
              ret: "0 = 通过；**-1** 帧太短/参数非法；**-2 校验不对**（线上数据坏了）；**-3** 对不上骨架（帧头/地址/功能码/帧尾/长度字节 不一致）",
              sample: `int addr, func, dlen; const unsigned char *data;\nint ret = cg_proto_parse(&p, frame, flen, &addr, &func, &data, &dlen);\nif (ret == -2)      cg_log(CG_LOG_ERROR, "校验不对 —— 线上数据坏了");\nelse if (ret != 0)  cg_log(CG_LOG_ERROR, "这帧对不上骨架（%d）", ret);\nelse { /* addr / func / data[0..dlen) 就是拆出来的字段 */ }`,
            }),
        ],
      },
      {
        label: "校验（CRC / 哈希）",
        cmds: [
          c("cg_crc32", "CRC32 校验值", "unsigned cg_crc32(const void *data, int len)",
            "IEEE 802.3 / zlib 同一套（多项式 0xEDB88320，初值全 1，末尾取反）。文本 \"123456789\" 的校验值是 0xCBF43926；空输入返回 0", {
              params: ["data / len：数据与长度（len <= 0 按空消息算）"],
              ret: "32 位校验值",
              sample: `/* 校验文件有没有变过：读完整个文件算一遍 */
char *buf = cg_file_read(path);
if (buf) {
    cg_log(CG_LOG_INFO, "CRC32 = %08X", cg_crc32(buf, (int)strlen(buf)));
    cg_free(buf);
}`,
            }),
          c("cg_crc16", "CRC16/Modbus 校验值", "unsigned cg_crc16(const void *data, int len)",
            "工控设备协议帧最常用的那个（多项式 0xA001 反射，初值 0xFFFF，不取反）。文本 \"123456789\" 的校验值是 0x4B37；空输入返回 0xFFFF", {
              params: ["data / len：数据与长度（len <= 0 按空消息算）"],
              ret: "16 位校验值（高 16 位是 0）",
              sample: `/* 发一帧 Modbus RTU：CRC 挂在帧尾，**低字节在前** */
unsigned char frame[8] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00 };
unsigned      crc = cg_crc16(frame, 6);
frame[6] = (unsigned char)(crc & 0xFF);
frame[7] = (unsigned char)(crc >> 8);
cg_serial_write(ser, frame, 8);`,
            }),
          c("cg_crc32_file", "算文件的 CRC32", "int cg_crc32_file(const char *path, unsigned *out)",
            "分块读，不把文件整读进内存（固件镜像几十上百 MB 也能算）；成功才写 out", {
              params: ["path：文件路径", "out：校验值出口"],
              ret: "1 = 成功；0 = 打不开或中途读出错（out 不动）",
              sample: `unsigned crc;
char *p = cg_path_join(cg_app_dir(), "fw.bin");
if (cg_crc32_file(p, &crc)) cg_status_set(sb, 0, cg_str_format("CRC32 %08X", crc));
cg_free(p);`,
            }),
          c("cg_hash", "算摘要（MD5 / SHA-1 / SHA-256 / SHA-512）", "char *cg_hash(const void *data, int len, const char *algo)",
            "返回**小写十六进制**串（记得 cg_free）。算法名大小写不敏感；不认识的名字返回 NULL", {
              params: ["data / len：数据与长度（len <= 0 按空数据算）", "algo：md5 / sha1 / sha256 / sha512"],
              ret: "摘要串（32 / 40 / 64 / 128 个字符）；失败 NULL",
              sample: `const char *txt = cg_widget_text(edt);
char *h = cg_hash(txt, (int)strlen(txt), "sha256");
if (h) { cg_log(CG_LOG_INFO, "SHA-256 = %s", h); cg_free(h); }`,
            }),
          c("cg_hash_file", "算文件的摘要（核对固件用）", "char *cg_hash_file(const char *path, const char *algo)",
            "同样分块读，不整读进内存。打不开或中途读出错返回 NULL —— 宁可报失败，也不给一个「看着像」的值", {
              ret: "小写十六进制摘要串（记得 cg_free）；失败 NULL",
              sample: `char *fw = cg_path_join(cg_app_dir(), "fw.bin");
char *h  = cg_hash_file(fw, "md5");
if (!h) cg_message_box(win, "打不开固件文件", "核对", 0);
else    cg_status_set(sb, 0, h);
cg_free(h);
cg_free(fw);`,
            }),
        ],
      },
      {
        label: "系统信息",
        cmds: [
          c("cg_env", "读环境变量", "char *cg_env(const char *name)",
            "返回 UTF-8 值（记得 cg_free）；没有这个变量返回 NULL，名字大小写不敏感。计算机名 = COMPUTERNAME、用户名 = USERNAME、临时目录 = TEMP、系统目录 = SystemRoot", {
              ret: "环境变量值；没有 / 名字为空返回 NULL",
              sample: `char *host = cg_env("COMPUTERNAME");
cg_log(CG_LOG_INFO, "本机 %s", host ? host : "?");
cg_free(host);`,
            }),
          c("cg_env_int", "读环境变量当整数", "int cg_env_int(const char *name, int fallback)",
            "没有 / 值不是合法整数 / 溢出都返回 fallback —— 不会像 atoi 那样把非数字静默当成 0", {
              ret: "解析出来的整数；不合法返回 fallback",
              sample: `int n = cg_env_int("NUMBER_OF_PROCESSORS", 1);   /* 开几个工作线程 */`,
            }),
          c("cg_sys_is64", "是不是 64 位 Windows", "int cg_sys_is64(void)",
            "**不随本进程的位数骗人**：32 位程序跑在 64 位系统上照样返回 1（环境变量 PROCESSOR_ARCHITECTURE 给的是进程视角，做不到这点）", {
              ret: "1 = 64 位 Windows；0 = 不是",
            }),
          c("cg_sys_version", "操作系统可读名", "char *cg_sys_version(void)",
            "形如 \"Windows 10 (10.0.19045)\"（Win11 也报 10.0，按 build 号分）。日志头 / 工单信息用", {
              ret: "版本串（记得 cg_free）；失败 NULL",
            }),
          c("cg_disk_space", "磁盘剩余 / 总容量", "int cg_disk_space(const char *path, unsigned long long *free_bytes, unsigned long long *total_bytes)",
            "path 可以是盘符或盘上任意路径；两个出口都能传 NULL（只问其中一个）。剩余量是**当前用户可用**的那个", {
              ret: "1 = 成功；0 = 路径不存在或调不动（出口不动）",
              sample: `unsigned long long f = 0, t = 0;
char *dir = cg_app_dir();
if (cg_disk_space(dir, &f, NULL) && f < 64ULL * 1024 * 1024)
    cg_log(CG_LOG_WARN, "日志盘只剩 %llu MB 了", f / 1024 / 1024);
cg_free(dir);`,
            }),
          c("cg_process_running", "某个进程在不在跑", "int cg_process_running(const char *exe_name)",
            "按可执行文件名查，大小写不敏感。现场排查「设备厂商的服务 / 驱动起没起」用得上", {
              ret: "1 = 在跑；0 = 不在（或参数为空）",
              sample: `if (!cg_process_running("DeviceSrv.exe"))
    cg_status_set(sb, 0, "设备服务没起");`,
            }),
          c("cg_local_ip", "本机 IPv4 地址", "char *cg_local_ip(void)",
            "挑一条默认路由再问本地端地址 —— **不发任何包、不需要 DNS**（离线工控机也能用）。多网卡时给的是默认出口那张，未必是连设备那张", {
              ret: "点分十进制串（记得 cg_free）；拿不到 NULL",
              sample: `char *ip = cg_local_ip();
cg_status_set(sb, 0, ip ? ip : "本机 IP 未知");
cg_free(ip);`,
            }),
        ],
      },
      {
        label: "剪贴板与系统",
        cmds: [
          c("cg_clip_get_text", "读剪贴板", "char *cg_clip_get_text(void)", "剪贴板里的文字；没有返回 NULL（堆串要 cg_free）"),
          c("cg_clip_set_text", "写剪贴板", "int cg_clip_set_text(const char *text)", "成功返回 0", {
            sample: `cg_clip_set_text(cg_widget_text(cg_find(cg_app_of(self), "edt_body")));
cg_message_box(self, "已复制到剪贴板", "提示", 0);`,
          }),
          c("cg_clip_get_image", "把剪贴板里的图片存成 PNG", "int cg_clip_get_image(const char *path)",
            "示波器 / 相机截的图直接落进调试记录：优先取 CF_BITMAP、没有就取 CF_DIB 自己拼一张。剪贴板里没有图片返回 0（写日志）", {
              params: ["path：输出路径（UTF-8，编码固定 PNG）"],
              ret: "1 = 成功；0 = 剪贴板里没有图片 / 打不开剪贴板 / 存盘失败",
              sample: `/* 例：按 Ctrl+V 时把剪贴板里的图存下来（.cgui 里给 keydown 绑 on_paste）*/\nchar *p = cg_path_join(cg_app_dir(), "paste.png");\nif (!cg_clip_get_image(p)) cg_status_set(sb, 0, "剪贴板里没有图片");\ncg_free(p);`,
            }),
          c("cg_clip_set_image", "把图片文件写进剪贴板", "int cg_clip_set_image(const char *path)",
            "截图\"复制\"那一半：写 CF_DIB（24 位，各软件都认）+ CF_BITMAP 双格式。与 cg_clip_get_image 对称，需要 exe 真的在跑（--selftest 下返回 0）", {
              params: ["path：图片路径（png/jpg/bmp/gif）"],
              ret: "1 = 成功；0 = 失败（读不到图 / 打不开剪贴板 / 一个格式都没写进去，写日志）",
              sample: `/* 存图后直接可粘贴（完整链路见 examples\\shottool） */\nif (cg_widget_screenshot(cv, path) && cg_clip_set_image(path))\n    cg_log(CG_LOG_INFO, "已存图并复制：%s", path);`,
            }),
          c("cg_shell_open", "用默认程序打开", "int cg_shell_open(const char *path_or_url)", "打开文件 / 文件夹 / 网址（浏览器、记事本……都行）", {
            sample: `cg_shell_open("https://www.example.com");
cg_shell_open(cg_app_data_dir());      /* 打开数据目录 */`,
          }),
          c("cg_run_process", "启动外部程序", "int cg_run_process(const char *cmd_line, int wait)", "跑一条命令行；wait=1 时返回它的退出码"),
          c("cg_time_now", "取当前时间", "char *cg_time_now(const char *fmt)", "fmt 传 NULL 用 \"2026-09-21 02:33:53\"（strftime 格式）", {
            sample: `char *now = cg_time_now("%H:%M:%S");
cg_status_set(cg_find(cg_app_of(self), "sb_main"), 1, now);
cg_free(now);`,
          }),
        ],
      },
      {
        label: "注册表",
        cmds: [
          c("cg_reg_read", "读注册表", "char *cg_reg_read(const char *subkey, const char *name, const char *def)", "读 HKCU\\subkey 的字符串值；没有返回 def", {
            sample: `char *v = cg_reg_read("Software\\\\MyApp", "install_dir", "(未安装)");
cg_free(v);`,
          }),
          c("cg_reg_write", "写注册表", "int cg_reg_write(const char *subkey, const char *name, const char *value)", "写 HKCU 下的字符串值；做开机自启就写到 ...\\CurrentVersion\\Run", {
            sample: `/* 开机自启：把 exe 路径写进 Run */
char *dir = cg_app_dir();
cg_reg_write("Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Run", "MyApp",
             cg_path_join(dir, "MyApp.exe"));
cg_free(dir);`,
          }),
          c("cg_reg_subkeys", "枚举注册表子键", "int cg_reg_subkeys(const char *subkey, char *out, int cap)", "列 HKCU\\subkey 的直接子键名，'\\n' 分隔；返回写入字节数，键不存在返回 0", {
            sample: `/* 扫出右键菜单项（子键），再逐个读 MUIVerb / command */
char buf[8192];
int  n = cg_reg_subkeys("Software\\\\Classes\\\\Directory\\\\shell", buf, sizeof(buf));
for (int i = 0; i < n; ) {
    int j = i;
    while (j < n && buf[j] != '\\n') j++;
    buf[j] = '\\0';
    /* buf[i..j) 是一个子键名 */
    i = j + 1;
}`,
          }),
          c("cg_reg_del", "删注册表值", "int cg_reg_del(const char *subkey, const char *name)", "删 HKCU\\subkey 下的值（name 传 \"\" 是删默认值）；返回 0 = 调用后该值一定不存在（本来没有也算 0）", {
            sample: `/* 恢复 = 把值删掉，而不是写空串：空值在注册表里仍是"存在"的一条记录 */
cg_reg_del("Software\\\\Classes\\\\*\\\\shell\\\\MyTool", "LegacyDisable");`,
          }),
          c("cg_reg_del_key", "删注册表键", "int cg_reg_del_key(const char *subkey)", "删 HKCU\\subkey 一整棵（连子键带值）；返回 0 = 调用后该键一定不存在。破坏性，删前先备份", {
            sample: `/* 卸载自建的右键菜单项（先把整棵键删干净，别留空壳） */
cg_reg_del_key("Software\\\\Classes\\\\*\\\\shell\\\\MyTool");`,
          }),
          c("cg_reg_read_ex", "读注册表（跨根）", "char *cg_reg_read_ex(const char *root, const char *subkey, const char *name, const char *def)", "同 cg_reg_read，但 root 可选 \"HKCU\"/\"HKLM\"/\"HKCR\"（大小写不敏感，NULL = HKCU）；扫系统区、读 CLSID 名字用它", {
            sample: `/* 扫右键菜单的系统区（HKLM）要管理员，读到啥算啥 */
char *pn = cg_reg_read_ex("HKLM",
    "SOFTWARE\\\\Microsoft\\\\Windows NT\\\\CurrentVersion", "ProductName", "(未知)");
cg_free(pn);`,
          }),
          c("cg_reg_write_ex", "写注册表（跨根）", "int cg_reg_write_ex(const char *root, const char *subkey, const char *name, const char *value)", "同 cg_reg_write，但 root 可选 \"HKCU\"/\"HKLM\"/\"HKCR\"；写 HKLM 需要已提权（cg_reg_is_admin()），否则返回 -1", {
            sample: `if (cg_reg_is_admin())
    cg_reg_write_ex("HKLM", "Software\\\\MyApp", "channel", "beta");
else
    cg_log_append("系统区要管理员，没写");`,
          }),
          c("cg_reg_subkeys_ex", "枚举注册表子键（跨根）", "int cg_reg_subkeys_ex(const char *root, const char *subkey, char *out, int cap)", "同 cg_reg_subkeys，但 root 可选 \"HKCU\"/\"HKLM\"/\"HKCR\"；返回写入字节数，键不存在返回 0", {
            sample: `/* 扫系统区的文件右键菜单项 */
char buf[8192];
int  n = cg_reg_subkeys_ex("HKLM", "SOFTWARE\\\\Classes\\\\*\\\\shell", buf, sizeof(buf));`,
          }),
          c("cg_reg_del_ex", "删注册表值（跨根）", "int cg_reg_del_ex(const char *root, const char *subkey, const char *name)", "同 cg_reg_del，但 root 可选 \"HKCU\"/\"HKLM\"/\"HKCR\"；返回 0 = 调用后该值一定不存在（幂等）", {
            sample: `/* 恢复系统区那条被禁用的菜单项 */
cg_reg_del_ex("HKLM", "SOFTWARE\\\\Classes\\\\*\\\\shell\\\\MyTool", "LegacyDisable");`,
          }),
          c("cg_reg_del_key_ex", "删注册表键（跨根）", "int cg_reg_del_key_ex(const char *root, const char *subkey)", "同 cg_reg_del_key，但 root 可选 \"HKCU\"/\"HKLM\"/\"HKCR\"；返回 0 = 调用后该键一定不存在。破坏性，删前先备份", {
            sample: `cg_reg_del_key_ex("HKLM", "SOFTWARE\\\\Classes\\\\*\\\\shell\\\\MyTool");`,
          }),
          c("cg_reg_key_exists", "问注册表键在不在", "int cg_reg_key_exists(const char *root, const char *subkey)", "root 可选 \"HKCU\"/\"HKLM\"/\"HKCR\"；1 = 存在且打得开，0 = 不存在 / 参数不合法", {
            sample: `if (cg_reg_key_exists("HKCR", "CLSID\\\\{00021401-0000-0000-C000-000000000046}"))
    cg_log_append("快捷方式组件已注册");`,
          }),
          c("cg_reg_is_admin", "是否已提权", "int cg_reg_is_admin(void)", "1 = 进程已提权（能改 HKLM 系统区）；0 = 没提权。改系统区前先问它，权限问题要提前说清而不是静默失败", {
            sample: `if (!cg_reg_is_admin())
    cg_log_append("系统区要管理员身份，当前只读不改");`,
          }),
        ],
      },
      {
        label: "串口（cg_serial）",
        cmds: [
          c("cg_serial_list", "枚举串口", "int cg_serial_list(char *buf, int cap)", "把本机可用的串口名写进 buf（每行一个，如 \"COM3\\nCOM7\\n\"），返回写入的字节数；一个都没有返回 0（不是错误）。只给端口名，不给「USB-SERIAL CH340 (COM3)」那种友好名 —— 那必须走 SetupAPI，而系统库是在 exe 链接那步才连的，用它等于给每个应用的链接依赖加 -lsetupapi，这点好看不值得", {
            params: ["buf：够大的缓冲区，每行一个端口名", "cap：buf 的容量"],
            ret: "写入的字节数（按 '\\n' 切行就是端口名）；装不下时宁可少列也不截断半行",
            sample: `char buf[4096];
int  n = cg_serial_list(buf, (int)sizeof(buf));
if (n <= 0) cg_term_write(term, CG_TERM_SYS, "没找到任何串口", -1);
/* 内容形如 "COM3\\nCOM7\\n" —— 按 '\\n' 切行填进下拉框就行 */`,
          }),
          c("cg_serial_open", "打开串口", "cg_serial_t *cg_serial_open(const char *port, int baud)", "打开一条串口链路（固定 8N1），内部起一个读线程把字节搬进接收缓冲；失败返回 NULL", {
            params: ["port：\"COM3\" 或 \"\\\\\\\\.\\\\COM3\" 都收；空串算失败", "baud：波特率，<= 0 算失败"],
            ret: "链路句柄（用完必须 cg_serial_close）；失败返回 NULL，原因用 cg_serial_last_error(NULL) 取",
            sample: `/* 打开失败最要紧的是"为什么"，别只报一句"失败了" */
g_ser = cg_serial_open("COM3", 115200);
if (!g_ser) {
    cg_term_write(cg_find(app, "term_log"), CG_TERM_SYS,
                  cg_serial_last_error(NULL), -1);   /* 端口被占用 / 找不到… */
    return;
}
cg_set_timer(app, 50, poll, app);                    /* 连着就用时钟去取数据 */`,
          }),
          c("cg_serial_close", "关闭串口", "void cg_serial_close(cg_serial_t *s)", "关掉链路并回收读线程；**NULL 安全**（没连上时传 NULL 也行）。**但不是「幂等」**：这一调会把句柄释放掉、指针当场失效 —— 同一个句柄关两次就是 use-after-free（2026-09-27 真机实测：进程以 `0xC0000374` 堆损坏退出，崩点可能离得老远）", {
            params: ["s：cg_serial_open 的返回值（可以是 NULL）"],
            ret: "void 无返回值",
            sample: `/* 关完立刻置空 —— 这一行别省。
   断开按钮和窗口 destroy 回调里都调到同一个句柄时，第二处看到的是 NULL，安全跳过 */
cg_serial_close(g_ser);
g_ser = NULL;`,
          }),
          c("cg_serial_write", "发送数据", "int cg_serial_write(cg_serial_t *s, const void *data, int len)", "同步写（带 1s 超时），不是写队列；返回实际写出的字节数，< 0 表示失败", {
            params: ["data：要发的字节；文本直接传字符串", "len：字节数，**必须 > 0**（和 cg_term_write 不一样，它不认 -1）"],
            ret: "实际写出的字节数；失败返回 < 0（原因见 cg_serial_last_error）",
            sample: `const char *cmd = "AT+GMR\\r\\n";       /* 长度得自己算，len 不支持传 -1 */
if (cg_serial_write(g_ser, cmd, (int)strlen(cmd)) < 0) {
    cg_log(CG_LOG_ERROR, "发送失败：%s", cg_serial_last_error(g_ser));
}`,
          }),
          c("cg_serial_read", "取数据（时钟里读）", "int cg_serial_read(cg_serial_t *s, void *buf, int cap)", "**非阻塞**取最多 cap 字节；没数据返回 0（不是错误）", {
            params: ["buf：接收缓冲区", "cap：buf 的容量"],
            ret: "取到的字节数；0 = 这一拍没有数据",
            sample: `/* ★ 唯一被支持的读法：在时钟回调里主动"取"，而不是等着被"推"。
   原因：cg_serial_open 的读线程只把字节搬进环形缓冲，**它绝不碰控件**
   （控件只能在建它的线程里碰，这是 Win32 硬约束）。
   cg_set_timer 是一次性的，"收尾再挂一次"才是周期。 */
cg_set_timer(app, 50, poll, app);

static void poll(void *user)
{
    cg_app_t     *app  = (cg_app_t *)user;
    cg_widget_t  *term = cg_find(app, "term_log");
    unsigned char buf[4096];
    int           n;

    if (!g_ser) return;                                  /* 已断开：就此停表 */
    while (cg_serial_available(g_ser) > 0) {
        n = cg_serial_read(g_ser, buf, (int)sizeof(buf)); /* 非阻塞，没数据返回 0 */
        if (n <= 0) break;
        cg_term_write(term, CG_TERM_RX, buf, n);          /* 收到多少灌多少，会按 \\n 切行 */
    }
    cg_set_timer(app, 50, poll, app);                     /* 再挂一次 = 50ms 周期 */
}`,
          }),
          c("cg_serial_available", "还有多少没取", "int cg_serial_available(const cg_serial_t *s)", "接收缓冲里还剩多少字节 —— 用来判断「这一拍到底有没有东西」，省一次注定返回 0 的 read", {
            ret: "还没被取走的字节数；s 为 NULL 返回 0",
            sample: `/* 取空缓冲的标准循环：先问一句再读，读到空就收工 */
while (cg_serial_available(g_ser) > 0) {
    int n = cg_serial_read(g_ser, buf, (int)sizeof(buf));
    if (n <= 0) break;
    cg_term_write(term, CG_TERM_RX, buf, n);
}`,
          }),
          c("cg_serial_flush_rx", "丢掉未取数据", "void cg_serial_flush_rx(cg_serial_t *s)", "把接收缓冲里还没取走的字节扔掉（「清空接收」按钮用）；NULL 安全", {
            sample: `/* 清空按钮：缓冲清掉，日志视图也一起清 */
cg_serial_flush_rx(g_ser);
cg_term_clear(cg_find(cg_app_of(self), "term_log"));`,
          }),
          c("cg_serial_rx_bytes", "累计收到字节", "long long cg_serial_rx_bytes(const cg_serial_t *s)", "从打开到现在一共收到多少字节（只增不减，要归零就重连）", {
            ret: "累计字节数；s 为 NULL 返回 0",
            sample: `cg_status_setf(cg_find(app, "sb_main"), 1, "RX %lld B", cg_serial_rx_bytes(g_ser));`,
          }),
          c("cg_serial_dropped", "溢出丢弃字节", "long long cg_serial_dropped(const cg_serial_t *s)", "接收缓冲满了丢掉的字节数（业务取数跟不上时才 > 0）——怀疑「数据少了一段」先问它", {
            ret: "累计丢弃字节数；s 为 NULL 返回 0",
            sample: `if (cg_serial_dropped(g_ser) > 0)
    cg_term_write(cg_find(app, "term"), CG_TERM_SYS, "接收缓冲溢出过，已丢字节", -1);`,
          }),
          c("cg_serial_tx_bytes", "累计发出字节", "long long cg_serial_tx_bytes(const cg_serial_t *s)", "从打开到现在一共发出多少字节（同样只增不减）", {
            ret: "累计字节数；s 为 NULL 返回 0",
            sample: `cg_status_setf(cg_find(app, "sb_main"), 2, "TX %lld B", cg_serial_tx_bytes(g_ser));`,
          }),
          c("cg_serial_last_error", "最后一次错误", "const char *cg_serial_last_error(const cg_serial_t *s)", "最后一次出错的中文描述（\"端口被占用…\"这种）；没出错返回空串", {
            params: ["s：链路句柄；**传 NULL 表示问「最近一次打开为什么失败」**（打开失败时没有句柄可问）"],
            ret: "指向内部缓冲的字符串，不用 free；下次报错会被覆盖，要留就自己拷走",
            sample: `/* 打开失败：没有句柄，只能问 NULL */
if (!(g_ser = cg_serial_open(port, baud))) {
    cg_message_box(self, cg_serial_last_error(NULL), "打不开串口", 2);
    return;
}
/* 写入失败：手里的句柄上有原因 */
if (cg_serial_write(g_ser, buf, n) < 0) {
    cg_term_write(term, CG_TERM_SYS, cg_serial_last_error(g_ser), -1);
}`,
          }),
          c("cg_serial_open_ex", "打开串口（全参数）", "cg_serial_t *cg_serial_open_ex(const char *port, int baud, int bits, int stop, int parity)",
            "帧格式全参数版：老设备常见的 **7E1 / 8E2 靠它**。参数不合法时干净失败（不合法的值不往驱动送 —— 半开着的端口比打不开更坑人）", {
              params: ["bits：数据位 5..8", "stop：停止位 1..2", "parity：0 = 无 / 1 = 奇 / 2 = 偶"],
              ret: "链路句柄（用完 cg_serial_close）；失败 NULL，原因见 cg_serial_last_error(NULL)",
              sample: `/* 老仪表常见 7E1：7 数据位 + 偶校验 + 1 停止位 */\ncg_serial_t *s = cg_serial_open_ex("COM3", 9600, 7, 1, 2);`,
            }),
        ],
      },
      {
        label: "串口组件（serial 控件形态）",
        cmds: [
          c("cg_widget_serial", "取组件的链路句柄", "cg_serial_t *cg_widget_serial(cg_widget_t *w)",
            "无界面控件（.cgui 里 type: \"serial\"）的取句柄写法：**未打开 / 类型不对返回 NULL**，所以 arrive 回调第一件事就是判它 —— 自检模式会合成触发 arrive，那时链路不存在", {
              ret: "链路句柄（别自己 close，交给 cg_widget_serial_close）；没开返回 NULL",
              sample: `void on_arrive(cg_widget_t *self, cg_event_t ev, void *user) {\n    cg_serial_t *s = cg_widget_serial(self);\n    if (!s) return;                      /* 自检合成触发：链路不存在 */\n    /* ... 在时钟/回调里 cg_serial_read 取字节 */\n}`,
            }),
          c("cg_widget_serial_open", "按控件属性打开", "int cg_widget_serial_open(cg_widget_t *w, const char *port)",
            "端口 / 波特率 / 数据位 / 停止位 / 校验都从控件属性（.cgui 的 port / baud / bits / stop / parity）取；已打开则先关旧的再开。port 传 NULL 就用 .cgui 里配的那个", {
              ret: "0 = 成功；-1 = 失败（原因见 cg_serial_last_error(NULL)）",
              sample: `if (cg_widget_serial_open(cg_find(app, "ser_main"), port) != 0)\n    cg_term_write(term, CG_TERM_SYS, cg_serial_last_error(NULL), -1);`,
            }),
          c("cg_widget_serial_close", "关掉组件链路", "void cg_widget_serial_close(cg_widget_t *w)",
            "幂等、NULL 安全：没打开时调它也没事（app 退出时运行时会自己收，业务不必兜底）", { ret: "无" }),
          c("cg_widget_serial_set_baud", "改波特率", "void cg_widget_serial_set_baud(cg_widget_t *w, int baud)",
            "界面上的波特率下拉就是给这用的。链路**开着时按新参数原样重开**（关旧的再开新的），没开就只记住等下次打开用", { ret: "无" }),
          c("cg_widget_serial_set_format", "改数据位 / 停止位 / 校验", "void cg_widget_serial_set_format(cg_widget_t *w, int bits, int stop, int parity)",
            "帧格式（**7E1 这类老设备全靠它**）：链路开着就按新参数重开；任一个越界则**整个忽略**并记一条警告 —— 只改一半会造出「7 数据位 + 无校验」这种谁也没要的组合。底层就是 cg_serial_open_ex", {
              params: ["bits：5..8", "stop：1..2", "parity：0 = 无 / 1 = 奇 / 2 = 偶"],
              ret: "无（越界时记警告并忽略）",
              sample: `cg_widget_serial_set_baud(ser, 9600);\ncg_widget_serial_set_format(ser, 7, 1, 2);   /* 7E1 的老仪表 */\nif (cg_widget_serial_open(ser, "COM3") != 0) { /* 看 cg_serial_last_error */ }`,
            }),
        ],
      },
      {
        label: "网口（cg_net）",
        cmds: [
          c("cg_net_connect", "连接服务器", "cg_net_t *cg_net_connect(const char *host, int port, int timeout_ms)", "以客户端身份连到 host:port（TCP），内部起读线程把字节搬进接收缓冲；失败返回 NULL", {
            params: ["host：主机名或 IP；**NULL / 空串 = 127.0.0.1**（本机自测最常用）", "port：端口（1~65535）", "timeout_ms：连接超时；**<= 0 按 3000 算**（内部用非阻塞 connect + select，坏地址不会冻住界面）"],
            ret: "连接句柄（用完必须 cg_net_close）；失败返回 NULL，原因用 cg_net_last_error(NULL) 取",
            sample: `/* 连接失败最要紧的是"为什么"，别只报一句"失败了" */
g_cli = cg_net_connect("192.168.1.10", 8080, 3000);
if (!g_cli) {
    cg_term_write(term, CG_TERM_SYS, cg_net_last_error(NULL), -1);  /* 拒绝 / 超时 / 找不到主机… */
    return;
}
cg_set_timer(app, 50, poll, app);        /* 连着就用时钟去取数据（见 cg_net_read） */`,
          }),
          c("cg_net_listen", "监听端口", "cg_net_t *cg_net_listen(int port, int backlog)", "以服务端身份监听本机端口（TCP），等客户端连进来；**backlog <= 0 按 4 算**", {
            params: ["port：要监听的本机端口", "backlog：等待队列长度；<= 0 按 4"],
            ret: "监听句柄（它只是「等连接」，不收发数据；用完必须 cg_net_close）；绑不上（端口被占用）返回 NULL，原因见 cg_net_last_error(NULL)",
            sample: `/* 绑定失败最常见的原因就是端口被占用 */
g_srv = cg_net_listen(8080, 4);
if (!g_srv) {
    cg_term_write(term, CG_TERM_SYS, cg_net_last_error(NULL), -1);
    return;
}
cg_set_timer(app, 50, poll, app);        /* accept 在时钟里做，见 cg_net_accept */`,
          }),
          c("cg_net_accept", "收一个连接（时钟里轮询）", "cg_net_t *cg_net_accept(cg_net_t *srv, int timeout_ms)", "从监听句柄上收一条新连接；**timeout_ms <= 0 = 不等、立刻返回**，没连接时返回 NULL —— 那是正常情况，不是错误", {
            params: ["srv：cg_net_listen 的返回值", "timeout_ms：等多久；**传 0 = 立刻返回**（正因如此它才适合放在时钟里轮询）"],
            ret: "新连接的句柄（**一个句柄 = 一条连接**，用完 cg_net_close）；没连接返回 NULL（正常，不是错误）",
            sample: `/* ★ 服务端要同时管多个客户端，就自己拿一个句柄数组，在时钟里轮询 accept ——
   本层故意不做"客户端列表"这个概念，API 面保持一条线。 */
static cg_net_t *g_peer[8];
static int       g_npeer;

static void poll(void *user)
{
    cg_app_t *app = (cg_app_t *)user;
    cg_net_t *c;
    int       k;

    for (k = 0; k < 4; k++) {                        /* 一拍最多收几个，防"连接风暴" */
        c = cg_net_accept(g_srv, 0);                  /* 0 = 不等，立刻返回 */
        if (!c) break;                                /* NULL = 这拍没人连，不是错误 */
        if (g_npeer >= 8) { cg_net_close(c); break; } /* 数组满了就拒收，别漏了 close */
        g_peer[g_npeer++] = c;                        /* 存进自己的数组，稍后逐个 read */
        cg_term_write(cg_find(app, "term"), CG_TERM_SYS, cg_net_peer(c), -1);  /* 谁来了 */
    }
    cg_set_timer(app, 50, poll, app);                 /* 一次性定时器：再挂一次 = 周期 */
}`,
          }),
          c("cg_net_write", "发送数据", "int cg_net_write(cg_net_t *n, const void *data, int len)", "同步写（带 1s 超时，不是写队列）；返回实际写出的字节数，< 0 表示失败。**UDP 句柄上不可用：恒返回 -1**（UDP 没有「流写」这个概念，要用 cg_net_sendto）", {
            params: ["n：连接句柄（监听句柄不能写）", "data：要发的字节；文本直接传字符串", "len：字节数，**必须 > 0**（和 cg_term_write 不一样，它不认 -1）"],
            ret: "实际写出的字节数；失败返回 < 0（原因见 cg_net_last_error）",
            sample: `const char *cmd = "GET / HTTP/1.0\\r\\n\\r\\n";   /* 长度得自己算，len 不支持传 -1 */
if (cg_net_write(g_cli, cmd, (int)strlen(cmd)) < 0) {
    cg_log(CG_LOG_ERROR, "发送失败：%s", cg_net_last_error(g_cli));
}`,
          }),
          c("cg_net_read", "取数据（时钟里读）", "int cg_net_read(cg_net_t *n, void *buf, int cap)", "**非阻塞**取最多 cap 字节；没数据返回 0（不是错误）。**UDP 句柄上不可用：恒返回 0**，看着像「没数据」其实永远没数据（UDP 要用 cg_net_recvfrom）", {
            params: ["n：连接句柄", "buf：接收缓冲区", "cap：buf 的容量"],
            ret: "取到的字节数；0 = 这一拍没有数据；对端已关闭时也能把剩下的取完",
            sample: `/* ★ 唯一被支持的读法：在时钟回调里主动"取"，而不是等着被"推"。
   原因：cg_net_connect / cg_net_accept 的读线程只把字节搬进环形缓冲，
   **它绝不碰控件**（控件只能在建它的线程里碰，这是 Win32 硬约束）。
   cg_set_timer 是一次性的，"收尾再挂一次"才是周期。 */
cg_set_timer(app, 50, poll, app);

static void poll(void *user)
{
    cg_app_t     *app  = (cg_app_t *)user;
    cg_widget_t  *term = cg_find(app, "term");
    unsigned char buf[4096];
    int           n, i;

    if (g_cli) {                                       /* 客户端那条连接 */
        if (!cg_net_connected(g_cli) && cg_net_available(g_cli) <= 0) {
            cg_term_write(term, CG_TERM_SYS, "对端关闭了连接", -1);   /* 掉线了 */
            cg_net_close(g_cli);
            g_cli = NULL;
        } else while (cg_net_available(g_cli) > 0) {
            n = cg_net_read(g_cli, buf, (int)sizeof(buf));  /* 非阻塞，没数据返回 0 */
            if (n <= 0) break;
            cg_term_write(term, CG_TERM_RX, buf, n);        /* 收到多少灌多少，会按 \\n 切行 */
        }
    }
    for (i = 0; i < g_npeer; i++) {                    /* 服务端收上来的每条连接 */
        while (cg_net_available(g_peer[i]) > 0) {
            n = cg_net_read(g_peer[i], buf, (int)sizeof(buf));
            if (n <= 0) break;
            cg_term_write(term, CG_TERM_RX, buf, n);
        }
    }
    cg_set_timer(app, 50, poll, app);                  /* 再挂一次 = 50ms 周期 */
}`,
          }),
          c("cg_net_available", "还有多少没取", "int cg_net_available(const cg_net_t *n)", "接收缓冲里还剩多少字节 —— 用来判断「这一拍到底有没有东西」，省一次注定返回 0 的 read。**UDP 句柄上恒返回 0**（UDP 没有接收缓冲，数据报排在内核队列里，靠 cg_net_recvfrom 取）", {
            ret: "还没被取走的字节数；n 为 NULL 返回 0",
            sample: `/* 取空一条连接的标准循环：先问一句再读，读到空就收工 */
while (cg_net_available(g_cli) > 0) {
    int n = cg_net_read(g_cli, buf, (int)sizeof(buf));
    if (n <= 0) break;
    cg_term_write(term, CG_TERM_RX, buf, n);
}`,
          }),
          c("cg_net_connected", "对端还在吗", "int cg_net_connected(const cg_net_t *n)", "对端还在返回 1，对方关闭后变 0 —— **这是判断「掉线」的唯一信号**（监听句柄恒为 0）。**UDP 句柄上恒为 0，但这不代表掉线**：UDP 无连接，压根没有「对端在不在」这个概念", {
            ret: "1 = 连接还在；0 = 对端已关闭（或这是监听句柄）",
            sample: `/* 掉线检测：先把缓冲里剩下的取干净，再回收句柄 */
if (!cg_net_connected(g_cli) && cg_net_available(g_cli) <= 0) {
    cg_term_write(term, CG_TERM_SYS, "对端关闭了连接", -1);
    cg_net_close(g_cli);
    g_cli = NULL;
}`,
          }),
          c("cg_net_rx_bytes", "累计收到字节", "long long cg_net_rx_bytes(const cg_net_t *n)", "从连上到现在这条连接一共收到多少字节（只增不减，要归零就重连）", {
            ret: "累计字节数；n 为 NULL 返回 0",
            sample: `cg_status_setf(cg_find(app, "sb_main"), 2, "RX %lld B", cg_net_rx_bytes(g_cli));`,
          }),
          c("cg_net_dropped", "溢出丢弃字节", "long long cg_net_dropped(const cg_net_t *n)", "接收缓冲满了丢掉的字节数（业务取数跟不上时才 > 0）——与 cg_serial_dropped 同一口径", {
            ret: "累计丢弃字节数；n 为 NULL 返回 0",
            sample: `if (cg_net_dropped(g_cli) > 0)
    cg_term_write(cg_find(app, "term"), CG_TERM_SYS, "接收缓冲溢出过，已丢字节", -1);`,
          }),
          c("cg_net_tx_bytes", "累计发出字节", "long long cg_net_tx_bytes(const cg_net_t *n)", "从连上到现在这条连接一共发出多少字节（同样只增不减）", {
            ret: "累计字节数；n 为 NULL 返回 0",
            sample: `cg_status_setf(cg_find(app, "sb_main"), 3, "TX %lld B", cg_net_tx_bytes(g_cli));`,
          }),
          c("cg_net_peer", "对端地址", "const char *cg_net_peer(const cg_net_t *n)", "对端的 \"ip:port\"；监听句柄返回 \"（监听中）\"", {
            ret: "指向内部缓冲的字符串，不用 free；下次调用可能被覆盖，要留就自己拷走",
            sample: `/* 服务端最想知道"谁来了"：收上新连接时打一行 */
cg_term_write(term, CG_TERM_SYS, cg_net_peer(g_peer[g_npeer - 1]), -1);`,
          }),
          c("cg_net_last_error", "最后一次错误", "const char *cg_net_last_error(const cg_net_t *n)", "最后一次出错的中文描述（\"连接被拒绝…\"这种）；没出错返回空串", {
            params: ["n：连接句柄；**传 NULL 表示问「最近一次连接/监听为什么失败」**（失败时没有句柄可问）"],
            ret: "指向内部缓冲的字符串，不用 free；下次报错会被覆盖，要留就自己拷走",
            sample: `/* 连接 / 监听失败：没有句柄，只能问 NULL */
if (!(g_srv = cg_net_listen(port, 4))) {
    cg_term_write(term, CG_TERM_SYS, cg_net_last_error(NULL), -1);
    return;
}
/* 写入失败：手里的句柄上有原因 */
if (cg_net_write(g_cli, buf, n) < 0) {
    cg_term_write(term, CG_TERM_SYS, cg_net_last_error(g_cli), -1);
}`,
          }),
          c("cg_net_close", "关闭连接", "void cg_net_close(cg_net_t *n)", "关掉连接或监听并回收读线程（会等它退出，最坏约 50ms）；**幂等且 NULL 安全**", {
            params: ["n：cg_net_connect / cg_net_listen / cg_net_accept 的返回值（可以是 NULL）"],
            ret: "void 无返回值",
            sample: `/* 断开按钮和窗口 destroy 回调里，把每一个句柄都关掉 —— 不用自己判"到底连没连" */
cg_net_close(g_cli);   g_cli = NULL;
for (i = 0; i < g_npeer; i++) cg_net_close(g_peer[i]);
g_npeer = 0;
cg_net_close(g_srv);   g_srv = NULL;`,
          }),
          c("cg_net_udp_open", "打开 UDP 端口（本机）", "cg_net_t *cg_net_udp_open(int local_port, int broadcast)", "开一个 UDP 句柄：**句柄上没有读线程**，收全靠时钟里 cg_net_recvfrom 自己拉", {
            params: ["local_port：本机要绑定的端口；**传 0 = 让系统随便分一个**（分到几写在 cg_net_peer 里）", "broadcast：非 0 = 允许往广播地址（如 255.255.255.255）发"],
            ret: "UDP 句柄（用完 cg_net_close）；失败返回 NULL，原因用 cg_net_last_error(NULL) 取",
            sample: `/* ★ UDP 和上面那套 TCP 是两套模型，别混着用：
   这个句柄上**没有读线程**、也没有"连接"这回事 —— 数据报的报文边界与来源地址
   必须原样交到你手里，而"读线程 + 字节环"会把这两样一起毁掉。所以收是拉取式的（见 cg_net_recvfrom）。 */
g_udp = cg_net_udp_open(9000, 1);              /* 本地 9000、允许广播 */
if (!g_udp) {
    cg_term_write(term, CG_TERM_SYS, cg_net_last_error(NULL), -1);
    return;
}
cg_term_write(term, CG_TERM_SYS, cg_net_peer(g_udp), -1);   /* "UDP 本地 9000（可广播）" */
cg_set_timer(app, 50, poll, app);              /* 收在时钟里做，见 cg_net_recvfrom */`,
          }),
          c("cg_net_sendto", "发一个数据报", "int cg_net_sendto(cg_net_t *n, const char *host, int port, const void *data, int len)", "发一个 UDP 数据报给 host:port（每次调用解析一次主机名）；**len 可以是 0**（空数据报合法）", {
            params: ["n：cg_net_udp_open 的返回值", "host：目的主机名或 IP；**空串 / NULL 会直接失败**（它不像 cg_net_connect 那样默认 127.0.0.1，要自己兜底）", "port：目的端口，1~65535；不合法同样直接失败", "data：要发的字节；文本直接传字符串", "len：字节数；**可以是 0** —— 空数据报在 UDP 里是合法的"],
            ret: "实际发出的字节数；< 0 失败（原因见 cg_net_last_error(n)）",
            sample: `/* 广播发现：往广播地址发一句，谁在听谁回包，回来的包自带它的 IP */
const char *probe = "WHO?";
if (cg_net_sendto(g_udp, "255.255.255.255", 9000, probe, (int)strlen(probe)) < 0) {
    cg_log(CG_LOG_ERROR, "UDP 发送失败：%s", cg_net_last_error(g_udp));
}
/* ★ 目的地每次现填（UDP 没有"连上"这回事），所以"地址框留空"这种输入要自己兜成
   127.0.0.1 —— 本函数对空 host 是直接失败的，不会替你选默认值。 */`,
          }),
          c("cg_net_recvfrom", "收一个数据报（时钟里拉）", "int cg_net_recvfrom(cg_net_t *n, char *ip_out, int ip_cap, int *port_out, void *buf, int cap)", "**非阻塞**收一个完整数据报（一次一个，绝不和下一个粘连），并把来源 ip/port 一起带出来", {
            params: ["n：cg_net_udp_open 的返回值", "ip_out：来源 IP 的输出缓冲；可传 NULL = 不关心来源", "ip_cap：ip_out 的容量（IPv4 给 64 就够）", "port_out：来源端口的输出；同样可传 NULL", "buf：接收缓冲", "cap：buf 的容量（数据报比它大就会被截断）"],
            ret: "**> 0** = 收到的字节数（来源已经写进 ip_out / port_out）；**= 0** = 收到了一个空数据报（UDP 允许，是\"收到了一帧\"，不是错误）；**= -1** = 这一拍没有数据了（**也不是错误**）。想区分\"没有数据\"和\"出错\"，看 cg_net_last_error(n) 是不是空串",
            sample: `/* ★ 这是最容易写错的一处：> 0 / = 0 / = -1 三个返回值含义各不相同，
   而且 UDP 句柄上没有读线程 —— 唯一的收法就是在这里循环"拉"，拉到 -1 为止。
   1) 一次调用 = 一个完整数据报，报文边界不会丢（所以每条都单独打一行，别自己拼成流）；
   2) n == 0 是"收到了一个空数据报"，必须也打出来，否则界面上看着像什么都没收到；
   3) -1 只是"这一拍没有了"；真出错时 cg_net_last_error(n) 才有内容，而且那个串是
      **粘的**（报过一次就一直非空），别每拍都往日志里刷同一句。 */
static cg_net_t *g_udp;

static void poll(void *user)
{
    cg_app_t    *app  = (cg_app_t *)user;
    cg_widget_t *term = cg_find(app, "term");
    char         buf[1472], ip[64];
    int          n, k, port;

    for (k = 0; k < 16; k++) {                  /* 每拍给个上限，防饿死界面 */
        char *line;

        port  = 0;
        ip[0] = '\\0';                           /* inet_ntop 失败时别留着上一拍的地址 */
        n = cg_net_recvfrom(g_udp, ip, (int)sizeof(ip), &port, buf, (int)sizeof(buf));
        if (n < 0) break;                       /* -1 = 没有更多数据报了（不是错误） */

        if (n == 0) line = cg_str_format("[%s:%d] （空数据报）", ip, port);
        else        line = cg_str_format("[%s:%d] %.*s", ip, port, n, buf);
        if (line) { cg_term_write(term, CG_TERM_RX, line, -1); cg_free(line); }
    }
    cg_set_timer(app, 50, poll, app);           /* 一次性定时器：再挂一次 = 周期 */
}`,
          }),
        ],
      },
    ],
};
