/**
 * @file    test_kit_wireless.c
 * @brief   cg_kit 无线状态：WiFi 网卡 / 周边网络 / 蓝牙无线电与已知设备（cg_kit_wireless.c）
 * @details **只断言契约，不断言本机硬件** —— 有没有无线网卡、有几个蓝牙无线电是机器属性，
 *          不是代码属性；把它们写死会让测试在别的机器上红，那种红没有信息量。
 *          所以这里断言的是：越界给 -1、空指针不崩、计数与取值的边界一致、
 *          能取到时字段落在合法范围里（信号 0..100、地址是 17 字符的 AA:BB:… 形状）。
 *          硬件怎么走的两条路（有 / 没有）都会被执行到，而且结果会打印成 note ——
 *          跑一次就等于留下了一份"这台机器当时什么样"的现场记录。
 *
 *          另一件必须钉的事：**这些符号是运行期取的**（零新增链接依赖，见 cg_kit_wireless.c
 *          文件头）。所以本文件能编过、能跑过，本身就证明"没有把用户拖进新的链接依赖"。
 */
#include "cg_test.h"
#include "cstackgui.h"

#include <string.h>
#include <stdio.h>

/** @brief 地址是不是 "AA:BB:CC:DD:EE:FF" 这个形状（17 字符 + 5 个冒号） */
static int addr_looks_ok(const char *a)
{
    int i;

    if (!a || strlen(a) != 17) return 0;
    for (i = 0; i < 17; i++) {
        if (i % 3 == 2) {
            if (a[i] != ':') return 0;
        } else if (!((a[i] >= '0' && a[i] <= '9') || (a[i] >= 'A' && a[i] <= 'F'))) {
            return 0;
        }
    }
    return 1;
}

static void test_wifi(void)
{
    cg_wifi_info_t w;
    cg_wifi_net_t  nets[16];
    int            n, found, i;
    char           buf[384];    /* 要装得下 256 字节的网卡描述 + SSID + 状态，否则 -Wformat-truncation */

    n = cg_wifi_count();
    cg_test_int("无线网卡数量 >= 0（这台机器没有网卡也是合法结果）", n >= 0, 1);
    snprintf(buf, sizeof buf, "本机无线网卡：%d 块", n);
    cg_test_note(buf);

    cg_test_note("---- 边界：越界 / 空指针 ----");
    cg_test_int("负下标 -> -1", cg_wifi_info(-1, &w), -1);
    cg_test_int("出口为 NULL -> -1", cg_wifi_info(0, NULL), -1);
    /* 越界用 "数量 + 100"，**不用 "数量" 本身**：网卡/设备是**会变的** ——
     * 这条最初写成 `cg_wifi_info(n)`，而 `n` 是几步之前问出来的数；
     * 蓝牙那边同样写法就飘了（全量跑时正好有设备连上/断开，那个下标从"越界"变成"有效"）。
     * 测试要断的是**契约**（越界给 -1），不是"这一刻的硬件凑巧"。(2026-09-27) */
    cg_test_int("远超范围的下标 -> -1", cg_wifi_info(n + 100, &w), -1);

    if (n > 0) {
        cg_test_note("---- 本机有网卡：字段要落在合法范围 ----");
        cg_test_int("第 0 块能取到", cg_wifi_info(0, &w), 0);
        cg_test_int("  网卡描述非空", w.description[0] != 0, 1);
        cg_test_int("  信号在 -1..100 之间", w.signal >= -1 && w.signal <= 100, 1);
        cg_test_int("  SSID 是 NUL 结尾（长度 < 上限）", (int)strlen(w.ssid) < CG_WIFI_SSID_MAX, 1);
        snprintf(buf, sizeof buf, "  第 0 块：%s  状态=%d  SSID=%s  信号=%d%%", w.description,
                 w.state, w.ssid[0] ? w.ssid : "(未连接)", w.signal);
        cg_test_note(buf);
    } else {
        cg_test_note("（这台机器没有无线网卡 -> 上面那条路走不到，"
                     "没有网卡的路径由下面的列表接口覆盖）");
    }

    cg_test_note("---- 周边网络列表：参数边界 ----");
    cg_test_int("出口为 NULL -> -1", cg_wifi_networks(NULL, 4, &found), -1);
    cg_test_int("cap <= 0 -> -1", cg_wifi_networks(nets, 0, &found), -1);
    cg_test_int("count 出口为 NULL 也不崩（只是不给个数）",
                cg_wifi_networks(nets, 4, NULL) == 0 || cg_wifi_networks(nets, 4, NULL) == -1, 1);

    cg_test_note("---- 主动扫描（异步提交，几秒后结果才出现在上面那个列表里）----");
    i = cg_wifi_scan();
    snprintf(buf, sizeof buf, "cg_wifi_scan() 返回 %d（0 = 已提交；-1 = 没网卡 / 系统不支持）", i);
    cg_test_note(buf);
    /* 断契约：有网卡时"提交得出去或被系统挡住"都算正常（位置权限会挡住），没网卡时必须 -1 */
    if (cg_wifi_count() > 0) cg_test_int("有网卡时是 0 或 -1，不是别的", i == 0 || i == -1, 1);
    else                     cg_test_int("没网卡时如实返回 -1", i, -1);

    found = -123;
    i     = cg_wifi_networks(nets, 16, &found);
    snprintf(buf, sizeof buf, "周边网络接口返回 %d，count=%d", i, found);
    cg_test_note(buf);
    if (i == -1) {
        cg_test_int("取不到时 count 被清成 0（不留下垃圾值）", found, 0);
    } else {
        int k;

        cg_test_int("成功时 count >= 0", found >= 0, 1);
        for (k = 0; k < found && k < 16; k++) {
            cg_test_int("  列表里的 SSID 非空（隐藏 SSID 已被跳过）", nets[k].ssid[0] != 0, 1);
            cg_test_int("  信号 0..100", nets[k].signal >= 0 && nets[k].signal <= 100, 1);
        }
        if (found > 0) {
            /* 只用 [0] 报一条：k 出了循环就等于 min(found,16)，再拿它当下标就越界了 */
            snprintf(buf, sizeof buf, "  看到 %d 个网络，第一个是 %s（%d%%）",
                     found, nets[0].ssid, nets[0].signal);
            cg_test_note(buf);
        }
    }
}

static void test_bluetooth(void)
{
    cg_ble_radio_t  r;
    cg_ble_device_t d;
    int             nr, nd, i;
    char            buf[320];

    nr = cg_ble_radio_count();
    nd = cg_ble_device_count();
    cg_test_int("蓝牙无线电数量 >= 0", nr >= 0, 1);
    cg_test_int("已知蓝牙设备数量 >= 0", nd >= 0, 1);
    cg_test_int("两次调用结果一致（不持状态，也不该抖动）", cg_ble_radio_count() == nr, 1);
    snprintf(buf, sizeof buf, "本机：蓝牙无线电 %d 个，已知设备 %d 个", nr, nd);
    cg_test_note(buf);

    cg_test_note("---- 边界：越界 / 空指针 ----");
    cg_test_int("无线电：负下标 -> -1", cg_ble_radio_info(-1, &r), -1);
    cg_test_int("无线电：出口 NULL -> -1", cg_ble_radio_info(0, NULL), -1);
    cg_test_int("无线电：远超范围的下标 -> -1", cg_ble_radio_info(nr + 100, &r), -1);
    cg_test_int("设备：负下标 -> -1", cg_ble_device_info(-1, &d), -1);
    cg_test_int("设备：出口 NULL -> -1", cg_ble_device_info(0, NULL), -1);
    /* 同样不用 "数量" 本身当下标：蓝牙设备是**会连上/断开**的，几毫秒前问出来的数
     * 可能已经变成合法下标（这条就是全量 memcheck 里飘掉的那一条） */
    cg_test_int("设备：远超范围的下标 -> -1", cg_ble_device_info(nd + 100, &d), -1);

    if (nr > 0) {
        cg_test_note("---- 本机有蓝牙：字段要落在合法范围 ----");
        cg_test_int("第 0 个无线电能取到", cg_ble_radio_info(0, &r), 0);
        cg_test_int("  地址是 AA:BB:CC:DD:EE:FF 形状", addr_looks_ok(r.address), 1);
        snprintf(buf, sizeof buf, "  %s  %s", r.name[0] ? r.name : "(无名)", r.address);
        cg_test_note(buf);
    }

    if (nd > 0) {
        cg_test_note("---- 本机有已知设备 ----");
        cg_test_int("第 0 个设备能取到", cg_ble_device_info(0, &d), 0);
        cg_test_int("  地址是 AA:BB:CC:DD:EE:FF 形状", addr_looks_ok(d.address), 1);
        for (i = 0; i < nd && i < 8; i++) {
            if (cg_ble_device_info(i, &d) != 0) continue;
            snprintf(buf, sizeof buf, "  [%d] %s  %s  已连接=%d 记得过=%d", i,
                     d.name[0] ? d.name : "(无名)", d.address, d.connected, d.remembered);
            cg_test_note(buf);
        }
    }
}

/* ------------------------------------------------------------ BLE 基元（不需要外设就能验完） */

static void test_ble_basics(void)
{
    unsigned long long v = 0;
    unsigned short     u = 0;
    char               buf[64];

    cg_test_note("---- 地址：文本 <-> 64 位（字节序最容易错的一处）----");
    cg_test_int("解析 AA:BB:CC:DD:EE:FF", cg_ble_addr_from_str("AA:BB:CC:DD:EE:FF", &v), 0);
    cg_test_int("  = 0xAABBCCDDEEFF（最高那组在最高位）", (long long)v, 0xAABBCCDDEEFFLL);
    cg_ble_addr_str(0xAABBCCDDEEFFULL, buf, sizeof buf);
    cg_test_str("格式化回去一致", buf, "AA:BB:CC:DD:EE:FF");

    v = 0;
    cg_test_int("小写、无分隔符也认", cg_ble_addr_from_str("aabbccddeeff", &v), 0);
    cg_test_int("  -> 同一个值", (long long)v, 0xAABBCCDDEEFFLL);
    cg_test_int("连字符分隔也认", cg_ble_addr_from_str("AA-BB-CC-DD-EE-FF", &v), 0);
    cg_test_int("  -> 同一个值", (long long)v, 0xAABBCCDDEEFFLL);
    cg_test_int("空格分隔也认", cg_ble_addr_from_str("aa bb cc dd ee ff", &v), 0);

    cg_test_int("少一位 -> 写错（不许猜）", cg_ble_addr_from_str("AA:BB:CC:DD:EE", &v), -1);
    cg_test_int("多一位 -> 写错", cg_ble_addr_from_str("AA:BB:CC:DD:EE:FF:00", &v), -1);
    cg_test_int("非十六进制字符 -> 写错", cg_ble_addr_from_str("AA:BB:CC:DD:EE:GG", &v), -1);
    cg_test_int("空串 -> 写错", cg_ble_addr_from_str("", &v), -1);
    cg_test_int("NULL -> -1", cg_ble_addr_from_str(NULL, &v), -1);
    cg_test_int("出口 NULL -> -1", cg_ble_addr_from_str("AA:BB:CC:DD:EE:FF", NULL), -1);
    {
        char small[6];

        memset(small, '#', sizeof small);
        cg_ble_addr_str(0xAABBCCDDEEFFULL, small, (int)sizeof small);   /* 缓冲太小不许写越界 */
        cg_test_int("缓冲太小：仍然 NUL 结尾且没越界", (int)strlen(small) <= 5, 1);
    }

    cg_test_note("---- UUID：短写法 <-> 标准基写法 ----");
    cg_test_int("fff1 -> 长写法", cg_ble_uuid16_str(0xFFF1, buf, sizeof buf), 36);
    cg_test_str("  文本（小写，与蓝牙文档一致）", buf, "0000fff1-0000-1000-8000-00805f9b34fb");
    cg_test_int("短写法能解回来", cg_ble_uuid16_parse("fff1", &u), 0);
    cg_test_int("  = 0xFFF1", (long long)u, 0xFFF1);
    cg_test_int("长写法也能解回同一个值", cg_ble_uuid16_parse(buf, &u), 0);
    cg_test_int("  = 0xFFF1", (long long)u, 0xFFF1);
    cg_test_int("大写 + 大括号也认", cg_ble_uuid16_parse("{0000FFF1-0000-1000-8000-00805F9B34FB}", &u), 0);
    cg_test_int("  = 0xFFF1", (long long)u, 0xFFF1);
    cg_test_int("0x 前缀也认", cg_ble_uuid16_parse("0xfff1", &u), 0);
    cg_test_int("  = 0xFFF1", (long long)u, 0xFFF1);
    cg_test_int("标准的心率测量 2A37", cg_ble_uuid16_parse("00002a37-0000-1000-8000-00805f9b34fb", &u), 0);
    cg_test_int("  = 0x2A37", (long long)u, 0x2A37);

    cg_test_note("---- UUID：不是标准基形态的必须拒绝 ----");
    cg_test_int("自定义 128 位 UUID -> -1（不是 16 位能表达的）",
                cg_ble_uuid16_parse("12345678-1234-5678-1234-567812345678", &u), -1);
    /* 下面这条是我自己写错过的那一处：高 16 位不是 0000 时不能当标准基（索引基准对错就差在这） */
    cg_test_int("高 16 位非 0000 -> -1", cg_ble_uuid16_parse("ffee0000-0000-1000-8000-00805f9b34fb", &u), -1);
    cg_test_int("尾巴有一位不对 -> -1", cg_ble_uuid16_parse("0000fff1-0000-1000-8000-00805f9b34fZ", &u), -1);
    cg_test_int("位数不对 -> -1", cg_ble_uuid16_parse("fff", &u), -1);
    cg_test_int("空串 -> -1", cg_ble_uuid16_parse("", &u), -1);
    cg_test_int("NULL -> -1", cg_ble_uuid16_parse(NULL, &u), -1);

    cg_test_note("---- UUID：比较（现场拿手册的短 UUID 比设备报的长 UUID）----");
    cg_test_int("fff1 与它的长写法相等", cg_ble_uuid_equal("fff1", "0000fff1-0000-1000-8000-00805f9b34fb"), 1);
    cg_test_int("大小写不敏感", cg_ble_uuid_equal("FFF1", "0xFFF1"), 1);
    cg_test_int("不同的不等", cg_ble_uuid_equal("fff1", "fff2"), 0);
    cg_test_int("自定义 128 位：带不带大括号相等",
                cg_ble_uuid_equal("12345678-1234-5678-1234-567812345678",
                                  "{12345678-1234-5678-1234-567812345678}"), 1);
    cg_test_int("自定义 128 位 vs 标准基短写法：不等",
                cg_ble_uuid_equal("12345678-1234-5678-1234-567812345678", "fff1"), 0);
    cg_test_int("NULL -> 0", cg_ble_uuid_equal(NULL, "fff1"), 0);

    cg_test_note("---- 特征属性位 ----");
    cg_test_int("只有读（0x02）", cg_ble_attr_flags(0x02), CG_BLE_ATTR_READ);
    cg_test_int("通知（0x10）", cg_ble_attr_flags(0x10), CG_BLE_ATTR_NOTIFY);
    cg_test_int("读 + 写(要响应) + 通知（0x1A）",
                cg_ble_attr_flags(0x1A), CG_BLE_ATTR_READ | CG_BLE_ATTR_WRITE | CG_BLE_ATTR_NOTIFY);
    cg_test_int("0 -> 什么都不能", cg_ble_attr_flags(0), 0);

    cg_test_note("---- 按地址/名字找已知设备（这台机器没有已知设备，走'找不到'那条）----");
    {
        cg_ble_device_t d;

        cg_test_int("不存在的地址 -> -1", cg_ble_find_device("AA:BB:CC:DD:EE:FF", &d), -1);
        cg_test_int("不存在的名字 -> -1", cg_ble_find_device("zzz-没有这个设备-zzz", &d), -1);
        cg_test_int("NULL -> -1", cg_ble_find_device(NULL, &d), -1);
        cg_test_int("出口 NULL -> -1", cg_ble_find_device("AA:BB:CC:DD:EE:FF", NULL), -1);
        snprintf(buf, sizeof buf, "已知设备 %d 个（有的话上面那两条会变成 0）", cg_ble_device_count());
        cg_test_note(buf);
    }
}

CG_TEST_SUITE("无线：WiFi 状态与周边网络", test_wifi);
CG_TEST_SUITE("无线：蓝牙无线电与已知设备", test_bluetooth);
CG_TEST_SUITE("无线：BLE 地址 / UUID / 属性基元", test_ble_basics);
