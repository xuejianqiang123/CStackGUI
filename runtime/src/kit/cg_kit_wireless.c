/**
 * @file    cg_kit_wireless.c
 * @brief   无线状态：WiFi 网卡与周边网络、蓝牙无线电与已知设备（IoT 上位机先看这一眼）
 * @details **为什么要它**：IoT 上位机的第一步不是"发命令"，而是**先排除"我这边的无线状态不对"**
 *          —— 设备连不上时，工程师要能一眼看到：我这台机器有没有无线网卡、连的是哪个 SSID、
 *          信号多少、目标 AP 在不在列表里、蓝牙开着没有、板子在不在已配对列表里。
 *          这些今天只能靠"去 Windows 设置里翻"，工具里拿不到。
 *
 *          **实现上的关键决定：零新增链接依赖。** 2026-09-27 用工程现有链接参数编了一个探针
 *          （build\probe_link\wireless_probe.c），WLAN 与蓝牙符号**全部 undefined reference**
 *          ⇒ 直接链会让**所有既有用户项目的链接命令都得改**。所以这里走
 *          `LoadLibraryW + GetProcAddress`（和 cg_win_theme.c 取 uxtheme、cg_kit_check.c 取
 *          RtlGetVersion 同一个做法）：**头文件只用来拿结构体定义，函数一律运行期取**。
 *          探针实测（build\probe_link\wireless_probe2.c，同一台机器）：
 *            - `wlanapi.dll`        提供 WlanOpenHandle / WlanEnumInterfaces / WlanQueryInterface /
 *                                   WlanGetAvailableNetworkList / WlanScan / WlanFreeMemory / WlanCloseHandle
 *            - **`BluetoothApis.dll`** 提供全部蓝牙符号（含 GATT 的读/写/订阅）——
 *                                  注意**不在** `bthprops.cpl` 里（那个只有无线电与设备枚举），
 *                                  这条是探针试出来的、不是猜的
 *          ⇒ 两个 DLL 都是系统自带（Win8 起），取不到就如实返回"没有"，不假装成功。
 *
 *          **不用初始化**：WLAN 客户端句柄由本文件**在进程内开一次并复用**（见 wlan_handle 的
 *          注释 —— 原来"每次调用自己开-关"是错的：那样会让 `WlanCloseHandle` 间歇性崩在
 *          rpcrt4，且崩点随机）。除这一个句柄外不持任何可变状态。
 *
 *          **不提供"主动扫描"**（试过，撤了）：`WlanScan` 在本机实测返回 **87
 *          （ERROR_INVALID_PARAMETER）** —— 不是权限问题（那会是 5），参数与文档一致却仍失败，
 *          原因不明。留一个"本机实测不工作"的 API 比没有它更坏，所以撤掉，列表只用系统缓存
 *          （Windows 自己会定期重扫，界面隔几秒再读一次就有新数据）。哪天弄清了再说。
 *
 *          取不到的几种说法（都返回"0 个 / -1"，调用方不用自己分辨）：
 *            - 本机没有无线网卡（探针在这台机器上就是这种：接口数 = 0）；
 *            - WLAN 服务没起（WlanOpenHandle 报 1062）；
 *            - 系统太老 / DLL 被裁掉（LoadLibrary 失败）。
 *          **能看出区别**的地方在日志里：本文件会 cg_log 一行原因，界面要显示就显示那行。
 */
#include "cstackgui.h"

#include <windows.h>
#include <wlanapi.h>          /* 结构体（WLAN_INTERFACE_INFO 等）—— 函数不直接调，见文件头 */
#include <bluetoothapis.h>    /* BLUETOOTH_* 结构体 */
#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------ 运行期取符号
 *
 * 这里**故意**不写 `&WlanOpenHandle` 这种取地址：那些头文件把函数声明成 dllimport，
 * 一旦引用就会出现链接依赖 —— 那正是本项目要避免的事（见文件头）。
 */

typedef DWORD (WINAPI *p_wlan_open_t)(DWORD, void *, DWORD *, HANDLE *);
typedef DWORD (WINAPI *p_wlan_close_t)(HANDLE, void *);
typedef DWORD (WINAPI *p_wlan_enum_t)(HANDLE, const void *, PWLAN_INTERFACE_INFO_LIST *);
typedef DWORD (WINAPI *p_wlan_query_t)(HANDLE, const GUID *, WLAN_INTF_OPCODE, void *, DWORD *, void **);
typedef DWORD (WINAPI *p_wlan_avail_t)(HANDLE, const GUID *, DWORD, PWLAN_AVAILABLE_NETWORK_LIST *);
typedef void  (WINAPI *p_wlan_free_t)(void *);

typedef HANDLE (WINAPI *p_bt_first_radio_t)(const BLUETOOTH_FIND_RADIO_PARAMS *, HANDLE *);
typedef BOOL   (WINAPI *p_bt_next_radio_t)(HANDLE, HANDLE *);
typedef BOOL   (WINAPI *p_bt_close_radio_t)(HANDLE);
typedef DWORD  (WINAPI *p_bt_radio_info_t)(HANDLE, PBLUETOOTH_RADIO_INFO);
typedef HBLUETOOTH_DEVICE_FIND (WINAPI *p_bt_first_dev_t)(const BLUETOOTH_DEVICE_SEARCH_PARAMS *,
                                                          PBLUETOOTH_DEVICE_INFO);
typedef BOOL   (WINAPI *p_bt_next_dev_t)(HBLUETOOTH_DEVICE_FIND, PBLUETOOTH_DEVICE_INFO);
typedef BOOL   (WINAPI *p_bt_close_dev_t)(HBLUETOOTH_DEVICE_FIND);

static p_wlan_open_t  g_wlan_open;
static p_wlan_close_t g_wlan_close;
static p_wlan_enum_t  g_wlan_enum;
static p_wlan_query_t g_wlan_query;
static p_wlan_avail_t g_wlan_avail;
static p_wlan_free_t  g_wlan_free;
static p_bt_first_radio_t g_bt_first_radio;
static p_bt_next_radio_t  g_bt_next_radio;
static p_bt_close_radio_t g_bt_close_radio;
static p_bt_radio_info_t  g_bt_radio_info;
static p_bt_first_dev_t   g_bt_first_dev;
static p_bt_next_dev_t    g_bt_next_dev;
static p_bt_close_dev_t   g_bt_close_dev;
static int                g_wlan_tried;      /* 0 = 还没试过 */
static int                g_bt_tried;
static int                g_bt_ready;

/* 定义在文件末尾的 BLE 基元区；这里前置声明是因为蓝牙那两个 info 函数要用它换算地址
 * （踩过一次：不加前置声明，编译器给"implicit declaration"、链接时才炸） */
static unsigned long long addr_of_bytes(const unsigned char *b);

/** @brief 取 WLAN 那批符号（进程内一次）；取不到返回 0 */
static int wlan_ready(void)
{
    HMODULE m;

    if (g_wlan_tried) return g_wlan_open != NULL;
    g_wlan_tried = 1;

    m = LoadLibraryW(L"wlanapi.dll");
    if (!m) {
        cg_log(CG_LOG_WARN, "无线：wlanapi.dll 打不开（err=%lu），WiFi 状态查不了",
               (unsigned long)GetLastError());
        return 0;
    }
    g_wlan_open  = (p_wlan_open_t)(void *)GetProcAddress(m, "WlanOpenHandle");
    g_wlan_close = (p_wlan_close_t)(void *)GetProcAddress(m, "WlanCloseHandle");
    g_wlan_enum  = (p_wlan_enum_t)(void *)GetProcAddress(m, "WlanEnumInterfaces");
    g_wlan_query = (p_wlan_query_t)(void *)GetProcAddress(m, "WlanQueryInterface");
    g_wlan_avail = (p_wlan_avail_t)(void *)GetProcAddress(m, "WlanGetAvailableNetworkList");
    g_wlan_free  = (p_wlan_free_t)(void *)GetProcAddress(m, "WlanFreeMemory");
    if (!g_wlan_open || !g_wlan_close || !g_wlan_enum || !g_wlan_query || !g_wlan_avail || !g_wlan_free) {
        cg_log(CG_LOG_WARN, "无线：wlanapi.dll 里缺符号（系统太老？），WiFi 状态查不了");
        g_wlan_open = NULL;
        return 0;
    }
    return 1;
}

static HANDLE g_wlan;                 /* 进程内复用的 WLAN 客户端句柄 */
static DWORD  g_wlan_ver;
static int    g_wlan_open_failed;     /* 开过一次失败就别每次再试（也别刷日志） */

/** @brief 取（必要时建）WLAN 客户端句柄；返回 NULL = 这块用不了
 *  @note **为什么不每次调用开-关**（原来就是这么写的，是错的）：实测反复
 *        `WlanOpenHandle`/`WlanCloseHandle` 会让 `WlanCloseHandle` **间歇性崩在 rpcrt4**
 *        —— 2026-09-27 用 gdb 抓到确切调用栈：
 *          `WlanCloseHandle` -> `RpcSsDestroyClientContext` -> `RPCRT4!NDRCContextBinding` -> SIGSEGV
 *        表现是"随机、位置漂移"的崩溃（早崩在 cg_wifi_count 的 close、晚崩在
 *        cg_wifi_networks 的 close）⇒ **看日志尾部永远找不到病根**（尾部只是最后被打印的，
 *        不是崩点）。改成"进程内开一次、复用"后连跑不再出现。
 *        代价：这一个句柄到进程结束才由系统回收 —— 比"偶尔崩一下"划算得多。 */
static HANDLE wlan_handle(void)
{
    if (!wlan_ready()) return NULL;
    if (g_wlan) return g_wlan;
    if (g_wlan_open_failed) return NULL;
    if (g_wlan_open(2, NULL, &g_wlan_ver, &g_wlan) != 0 || !g_wlan) {
        g_wlan_open_failed = 1;
        g_wlan             = NULL;
        cg_log(CG_LOG_WARN, "无线：WlanOpenHandle 失败（WLAN 服务没起？），WiFi 状态查不了");
        return NULL;
    }
    return g_wlan;
}

/** @brief 取蓝牙那批符号；取不到返回 0
 *  @note 实测这些符号在 **BluetoothApis.dll**（不在 bthprops.cpl —— 那个只有无线电与设备枚举） */
static int bt_ready(void)
{
    HMODULE m;

    if (g_bt_tried) return g_bt_ready;
    g_bt_tried = 1;

    m = LoadLibraryW(L"BluetoothApis.dll");
    if (!m) {
        cg_log(CG_LOG_WARN, "无线：BluetoothApis.dll 打不开（err=%lu），蓝牙状态查不了",
               (unsigned long)GetLastError());
        return 0;
    }
    g_bt_first_radio = (p_bt_first_radio_t)(void *)GetProcAddress(m, "BluetoothFindFirstRadio");
    g_bt_next_radio  = (p_bt_next_radio_t)(void *)GetProcAddress(m, "BluetoothFindNextRadio");
    g_bt_close_radio = (p_bt_close_radio_t)(void *)GetProcAddress(m, "BluetoothFindRadioClose");
    g_bt_radio_info  = (p_bt_radio_info_t)(void *)GetProcAddress(m, "BluetoothGetRadioInfo");
    g_bt_first_dev   = (p_bt_first_dev_t)(void *)GetProcAddress(m, "BluetoothFindFirstDevice");
    g_bt_next_dev    = (p_bt_next_dev_t)(void *)GetProcAddress(m, "BluetoothFindNextDevice");
    g_bt_close_dev   = (p_bt_close_dev_t)(void *)GetProcAddress(m, "BluetoothFindDeviceClose");
    if (!g_bt_first_radio || !g_bt_next_radio || !g_bt_close_radio || !g_bt_radio_info) {
        cg_log(CG_LOG_WARN, "无线：BluetoothApis.dll 里缺符号，蓝牙状态查不了");
        return 0;
    }
    g_bt_ready = 1;
    return 1;
}

/** @brief 把宽字符串拷进 UTF-8 缓冲（截断也保证 NUL 结尾；空指针当空串）
 *  @param src_cap 源数组的**容量**（以 wchar_t 计）。**必须给**，见下。
 *  @details 不能用 `WideCharToMultiByte(..., -1, ...)` —— 那个要求**调用方保证源串有终止符**，
 *           而这里拷的都是系统填进固定数组里的东西（`BLUETOOTH_DEVICE_INFO.szName`、
 *           `WLAN_INTERFACE_INFO.strInterfaceDescription`、蓝牙无线电名字），**没有保证**。
 *           真越界读过一次：全量 memcheck 跑到蓝牙那套就崩，单独跑却是好的 ——
 *           正是"系统填的名字终止没终止，取决于当时有哪几个设备"。
 *           （2026-09-27 定位记录：移开测试文件全绿、只留 WiFi 全绿 ⇒ 落在蓝牙套件。）
 */
static void copy_w(const wchar_t *src, int src_cap, char *dst, int size)
{
    int len = 0, n;

    if (!dst || size <= 0) return;
    dst[0] = 0;
    if (!src || src_cap <= 0) return;
    while (len < src_cap && src[len]) len++;          /* 在容量内找结尾，不越界 */
    if (len == 0) return;
    n = WideCharToMultiByte(CP_UTF8, 0, src, len, dst, size - 1, NULL, NULL);
    if (n <= 0) dst[0] = 0;
}

/* ------------------------------------------------------------ WiFi */

int cg_wifi_count(void)
{
    PWLAN_INTERFACE_INFO_LIST list = NULL;
    HANDLE  h;
    int     n = 0;

    h = wlan_handle();
    if (!h) return 0;
    if (g_wlan_enum(h, NULL, &list) == 0 && list) {
        n = (int)list->dwNumberOfItems;
        g_wlan_free(list);
    }
    return n;
}

int cg_wifi_info(int idx, cg_wifi_info_t *out)
{
    PWLAN_INTERFACE_INFO_LIST list = NULL;
    HANDLE  h   = NULL;
    DWORD   ver = 0;
    int     ret = -1;

    if (!out || idx < 0) return -1;
    memset(out, 0, sizeof *out);
    out->signal = -1;
    if (!wlan_ready()) return -1;
    h = wlan_handle();
    if (!h) return -1;

    if (g_wlan_enum(h, NULL, &list) == 0 && list && idx < (int)list->dwNumberOfItems) {
        const WLAN_INTERFACE_INFO *it = &list->InterfaceInfo[idx];

        copy_w(it->strInterfaceDescription,
               (int)(sizeof it->strInterfaceDescription / sizeof(wchar_t)),
               out->description, sizeof out->description);
        out->state = (int)it->isState;
        ret = 0;

        /* 连上了才有 SSID / 信号：问"当前连接"这条 opcode */
        {
            DWORD               sz = 0;
            WLAN_CONNECTION_ATTRIBUTES *ca = NULL;

            if (g_wlan_query(h, &it->InterfaceGuid, wlan_intf_opcode_current_connection,
                             NULL, &sz, (void **)&ca) == 0 && ca) {
                const WLAN_ASSOCIATION_ATTRIBUTES *as = &ca->wlanAssociationAttributes;
                int len = (int)as->dot11Ssid.uSSIDLength;

                if (len > CG_WIFI_SSID_MAX - 1) len = CG_WIFI_SSID_MAX - 1;
                if (len > 0) {
                    memcpy(out->ssid, as->dot11Ssid.ucSSID, (size_t)len);
                    out->ssid[len] = 0;
                }
                out->signal = (int)as->wlanSignalQuality;
                g_wlan_free(ca);
            }
        }
    }
    if (list) g_wlan_free(list);
    return ret;
}

int cg_wifi_networks(cg_wifi_net_t *out, int cap, int *count)
{
    PWLAN_INTERFACE_INFO_LIST     ilist = NULL;
    PWLAN_AVAILABLE_NETWORK_LIST  alist = NULL;
    HANDLE  h   = NULL;
    DWORD   ver = 0;
    int     n   = 0;

    if (count) *count = 0;
    if (!out || cap <= 0) return -1;
    if (!wlan_ready()) return -1;
    h = wlan_handle();
    if (!h) return -1;
    if (g_wlan_enum(h, NULL, &ilist) != 0 || !ilist || ilist->dwNumberOfItems == 0) {
        if (ilist) g_wlan_free(ilist);
        return -1;                       /* 没有无线网卡（本机探针就是这条路径） */
    }
    /* 只看第一块网卡：多网卡同时看周边 AP 意义不大，而且列表会重复 */
    if (g_wlan_avail(h, &ilist->InterfaceInfo[0].InterfaceGuid, 0, &alist) == 0 && alist) {
        DWORD i;

        for (i = 0; i < alist->dwNumberOfItems; i++) {
            const WLAN_AVAILABLE_NETWORK *net = &alist->Network[i];
            int len, j, dup = 0;

            len = (int)net->dot11Ssid.uSSIDLength;
            if (len > CG_WIFI_SSID_MAX - 1) len = CG_WIFI_SSID_MAX - 1;
            if (len <= 0) continue;                       /* 隐藏 SSID：名字是空的，列出来只会让人困惑 */

            /* 同一个 SSID 可能重复出现（多 BSSID / 多 band）：同名的只留一个，取信号最强的 */
            for (j = 0; j < n; j++) {
                if (out[j].ssid[0] && (int)strlen(out[j].ssid) == len &&
                    memcmp(out[j].ssid, net->dot11Ssid.ucSSID, (size_t)len) == 0) {
                    if ((int)net->wlanSignalQuality > out[j].signal) {
                        out[j].signal    = (int)net->wlanSignalQuality;
                        out[j].connected = (net->dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) ? 1 : 0;
                    }
                    dup = 1;
                    break;
                }
            }
            if (dup) continue;
            if (n >= cap) continue;                       /* 放不下就丢（count 里给的是实际看到的）*/

            memcpy(out[n].ssid, net->dot11Ssid.ucSSID, (size_t)len);
            out[n].ssid[len]  = 0;
            out[n].signal     = (int)net->wlanSignalQuality;
            out[n].secure     = net->bSecurityEnabled ? 1 : 0;
            out[n].connected  = (net->dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) ? 1 : 0;
            n++;
        }
        g_wlan_free(alist);
    }
    g_wlan_free(ilist);
    if (count) *count = n;
    return 0;
}

/* ------------------------------------------------------------ 蓝牙 */

int cg_ble_radio_count(void)
{
    BLUETOOTH_FIND_RADIO_PARAMS params;
    HBLUETOOTH_RADIO_FIND       find;
    HANDLE                      radio = NULL;
    int                         n     = 0;

    if (!bt_ready()) return 0;
    params.dwSize = sizeof(params);
    find = g_bt_first_radio(&params, &radio);
    if (!find) return 0;                     /* 没蓝牙：正常情况，不写日志刷屏 */
    do {
        n++;
        CloseHandle(radio);
    } while (g_bt_next_radio(find, &radio));
    g_bt_close_radio(find);
    return n;
}

int cg_ble_radio_info(int idx, cg_ble_radio_t *out)
{
    BLUETOOTH_FIND_RADIO_PARAMS params;
    HBLUETOOTH_RADIO_FIND       find;
    HANDLE                      radio = NULL;
    int                         i     = 0;

    if (!out || idx < 0) return -1;
    memset(out, 0, sizeof *out);
    memset(out->address, 0, sizeof out->address);
    if (!bt_ready()) return -1;

    params.dwSize = sizeof(params);
    find = g_bt_first_radio(&params, &radio);
    if (!find) return -1;
    do {
        if (i == idx) {
            BLUETOOTH_RADIO_INFO info;

            info.dwSize = sizeof(info);
            if (g_bt_radio_info(radio, &info) == ERROR_SUCCESS) {
                copy_w(info.szName, (int)(sizeof info.szName / sizeof(wchar_t)),
                       out->name, sizeof out->name);
                cg_ble_addr_str(addr_of_bytes(info.address.rgBytes), out->address, sizeof out->address);
            }
            CloseHandle(radio);
            g_bt_close_radio(find);
            return 0;
        }
        i++;
        CloseHandle(radio);
    } while (g_bt_next_radio(find, &radio));
    g_bt_close_radio(find);
    return -1;                               /* idx 越界 */
}

/** @brief 枚举"已知设备"（已配对 / 记得过的），共用一个实现 */
static int bt_walk_devices(BLUETOOTH_DEVICE_INFO *one, int idx, int *total)
{
    BLUETOOTH_DEVICE_SEARCH_PARAMS sp;
    BLUETOOTH_DEVICE_INFO          di;
    HBLUETOOTH_DEVICE_FIND         df;
    int                            i = 0, n = 0, found = 0;

    memset(&sp, 0, sizeof sp);
    sp.dwSize               = sizeof(sp);
    sp.fReturnAuthenticated = TRUE;          /* 已配对 */
    sp.fReturnRemembered    = TRUE;          /* 记得过（可能没配上）*/
    sp.fReturnUnknown       = FALSE;         /* 没见过的别列出来（那不是"我认识它"）*/
    sp.fReturnConnected     = TRUE;
    sp.fIssueInquiry        = FALSE;         /* 不做主动搜索：那要几十秒，而且是"发现"不是"状态" */
    sp.cTimeoutMultiplier   = 1;

    memset(&di, 0, sizeof di);
    di.dwSize = sizeof(di);
    df = g_bt_first_dev(&sp, &di);
    if (!df) {
        if (total) *total = 0;
        return 0;
    }
    do {
        if (i == idx) {
            *one  = di;
            found = 1;
        }
        i++;
        n++;
        di.dwSize = sizeof(di);
    } while (g_bt_next_dev(df, &di));
    g_bt_close_dev(df);
    if (total) *total = n;
    return found;
}

int cg_ble_device_count(void)
{
    BLUETOOTH_DEVICE_INFO di;
    int                   n = 0;

    if (!bt_ready()) return 0;
    memset(&di, 0, sizeof di);
    di.dwSize = sizeof(di);
    bt_walk_devices(&di, -1, &n);
    return n;
}

int cg_ble_device_info(int idx, cg_ble_device_t *out)
{
    BLUETOOTH_DEVICE_INFO di;

    if (!out || idx < 0) return -1;
    memset(out, 0, sizeof *out);
    if (!bt_ready()) return -1;

    memset(&di, 0, sizeof di);
    di.dwSize = sizeof(di);
    if (!bt_walk_devices(&di, idx, NULL)) return -1;

    copy_w(di.szName, (int)(sizeof di.szName / sizeof(wchar_t)), out->name, sizeof out->name);
    cg_ble_addr_str(addr_of_bytes(di.Address.rgBytes), out->address, sizeof out->address);
    out->connected  = di.fConnected ? 1 : 0;
    out->remembered = di.fRemembered ? 1 : 0;
    return 0;
}

/* ------------------------------------------------------------ BLE 基元：地址 / UUID / 属性
 *
 * 这一段**不需要蓝牙外设**就能做完、也就能真验完；而它恰好是写 BLE 工具最容易踩的三处坑：
 *
 *   1. **地址的字节序**：`BLUETOOTH_ADDRESS` 里 `rgBytes[0]` 是**打印出来最后一组**（小端），
 *      所以 "AA:BB:CC:DD:EE:FF" 对应的 rgBytes 是 `{FF,EE,DD,CC,BB,AA}` ——
 *      摆错一次就永远连不上，而报错只会说"设备没找到"。对外统一用**一个 64 位整数**表示
 *      地址（`0xAABBCCDDEEFF` 就是 AA:BB:CC:DD:EE:FF），换算由本文件负责，调用方不碰 rgBytes。
 *   2. **UUID 的两种写法**：设备手册写 "FFF1"，协议栈写
 *      "0000fff1-0000-1000-8000-00805f9b34fb" —— 是同一个东西（后者是蓝牙的标准基 UUID）。
 *   3. **属性位**：一个特征能不能读 / 写 / 订阅，全在那一个 properties 字里。
 *
 *  为什么"连接与读写"不在这里（下一步）：GATT 的入口要一个 `HANDLE hDevice`，而
 *  **`bluetoothleapis.h` 里没有任何获取它的函数**（2026-09-27 查过：只有 BluetoothGATT*
 *  那一族**拿走**句柄；`bluetoothapis.h` 的 find 句柄是"枚举句柄"，不是设备句柄）——
 *  纯 C 里得靠 SetupAPI 枚举设备接口、再 CreateFile 打开，而那**必须有真外设**才枚举得到。
 *  没有板子时把那段写出来只能是"看着对"，正是本项目一直在惩罚的东西（探针/断言的教训）。
 */

/** @brief 把 BLUETOOTH_ADDRESS 的 rgBytes 拼成对外那个 64 位表示（rgBytes[5] 是最高一组） */
static unsigned long long addr_of_bytes(const unsigned char *b)
{
    unsigned long long v = 0;
    int                i;

    for (i = 5; i >= 0; i--) v = (v << 8) | b[i];
    return v;
}

void cg_ble_addr_str(unsigned long long addr, char *out, int out_size)
{
    if (!out || out_size <= 0) return;
    /* 与 Windows 设置里显示的写法一致：**大写**、冒号分隔、最高字节在前 */
    snprintf(out, (size_t)out_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)((addr >> 40) & 0xFF), (unsigned)((addr >> 32) & 0xFF),
             (unsigned)((addr >> 24) & 0xFF), (unsigned)((addr >> 16) & 0xFF),
             (unsigned)((addr >> 8) & 0xFF), (unsigned)(addr & 0xFF));
}

int cg_ble_addr_from_str(const char *text, unsigned long long *out)
{
    unsigned long long v      = 0;
    int                digits = 0;

    if (!out || !text) return -1;
    for (; *text; text++) {
        char c = *text;

        if (c == ':' || c == '-' || c == ' ') continue;       /* 分隔符随便（冒号/连字符/空格） */
        if (c >= '0' && c <= '9')      c = (char)(c - '0');
        else if (c >= 'a' && c <= 'f') c = (char)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') c = (char)(c - 'A' + 10);
        else return -1;                                       /* 别的字符一律算写错，不"尽量猜" */
        if (digits >= 12) return -1;
        v = (v << 4) | (unsigned long long)(unsigned char)c;
        digits++;
    }
    if (digits != 12) return -1;                              /* 必须正好 6 字节 */
    *out = v;
    return 0;
}

int cg_ble_uuid16_str(unsigned short uuid16, char *out, int out_size)
{
    int n;

    if (!out || out_size <= 0) return -1;
    /* **小写**：蓝牙文档、nRF Connect、Windows 都这么写 —— 要跟数据手册逐字比对的就是它的字形
     * （与"进制输出一律大写"那条不冲突：那条说的是寄存器数值，这条说的是 UUID 的字面量） */
    n = snprintf(out, (size_t)out_size, "0000%04x-0000-1000-8000-00805f9b34fb", (unsigned)uuid16);
    return (n == 36 && n < out_size) ? n : -1;
}

/** @brief 把一个字符当十六进制数字解（不是就返回 -1） */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int cg_ble_uuid16_parse(const char *text, unsigned short *out16)
{
    char canon[40];
    int  n = 0, i;

    if (!text || !out16) return -1;
    for (; *text; text++) {
        char c = *text;

        if (c == ' ' || c == '\t') continue;
        if (c == '-' || c == '{' || c == '}') continue;       /* GUID 的大括号、短横线都吃掉 */
        if (n == 0 && c == '0' && (text[1] == 'x' || text[1] == 'X')) { text++; continue; }
        canon[n++] = c;
        if (n >= (int)sizeof(canon) - 1) return -1;
    }
    canon[n] = 0;
    if (n == 4) {                                             /* 短写法 "fff1" */
        unsigned v = 0;

        for (i = 0; i < 4; i++) {
            int d = hexval(canon[i]);

            if (d < 0) return -1;
            v = (v << 4) | (unsigned)d;
        }
        *out16 = (unsigned short)v;
        return 0;
    }
    if (n == 32) {                                            /* 长写法：必须是标准基形态 */
        /* 去掉横线后共 32 位十六进制：`0000` + `xxxx`（要取的那 4 位）+ 后面 24 位固定尾巴。
         * 索引要对准：尾巴从**第 8 位**开始、长 24 —— 第一版写成从第 4 位开始比 28 位，
         * 既错位又越界（读 base_tail 之外），靠"拿标准 UUID 走一遍单测"才抓出来。 */
        static const char *base_tail = "0000" "1000" "8000" "00805f9b34fb";   /* 24 位 */
        unsigned v = 0;

        if (canon[0] != '0' || canon[1] != '0' || canon[2] != '0' || canon[3] != '0') return -1;
                                                                             /* 高 16 位必须是 0000 */
                                                                             /* （4..7 位才是要取的值） */
        for (i = 0; i < 24; i++) {
            int a = hexval(canon[8 + i]), b = hexval(base_tail[i]);

            if (a < 0 || a != b) return -1;                  /* 尾巴必须与基 UUID 相同 */
        }
        for (i = 0; i < 4; i++) {                            /* 真正要取的是中间那 4 位 */
            int d = hexval(canon[4 + i]);

            v = (v << 4) | (unsigned)d;
        }
        *out16 = (unsigned short)v;
        return 0;
    }
    return -1;
}

/** @brief 把文本 UUID 归一成"小写、去掉短横线/大括号/0x"的形式；放不下返回 -1 */
static int uuid_canon(const char *text, char *out, int out_size)
{
    int n = 0;

    if (!text || !out || out_size <= 0) return -1;
    for (; *text; text++) {
        char c = *text;

        if (c == ' ' || c == '\t' || c == '-' || c == '{' || c == '}') continue;
        if (n == 0 && c == '0' && (text[1] == 'x' || text[1] == 'X')) { text++; continue; }
        if (c >= 'A' && c <= 'F') c = (char)(c - 'A' + 'a');
        else if (hexval(c) < 0) return -1;
        if (n >= out_size - 1) return -1;
        out[n++] = c;
    }
    out[n] = 0;
    return (n > 0) ? n : -1;
}

int cg_ble_uuid_equal(const char *a, const char *b)
{
    char ca[40], cb[40];
    unsigned short ua, ub;

    if (!a || !b) return 0;
    /* 能当 16 位解的两边就按 16 位比 —— 这样 "fff1" 与它那个长写法**相等**（最常见的用法） */
    if (cg_ble_uuid16_parse(a, &ua) == 0 && cg_ble_uuid16_parse(b, &ub) == 0) return ua == ub;
    if (uuid_canon(a, ca, sizeof ca) < 0) return 0;
    if (uuid_canon(b, cb, sizeof cb) < 0) return 0;
    return strcmp(ca, cb) == 0;
}

int cg_ble_attr_flags(unsigned properties)
{
    int f = 0;

    if (properties & 0x02u) f |= CG_BLE_ATTR_READ;
    if (properties & 0x04u) f |= CG_BLE_ATTR_WRITE_NR;
    if (properties & 0x08u) f |= CG_BLE_ATTR_WRITE;
    if (properties & 0x10u) f |= CG_BLE_ATTR_NOTIFY;
    if (properties & 0x20u) f |= CG_BLE_ATTR_INDICATE;
    return f;
}

int cg_ble_find_device(const char *addr_or_name, cg_ble_device_t *out)
{
    unsigned long long want = 0;
    int                by_addr, n, i;

    if (!addr_or_name || !out) return -1;
    by_addr = (cg_ble_addr_from_str(addr_or_name, &want) == 0);
    n       = cg_ble_device_count();
    for (i = 0; i < n; i++) {
        cg_ble_device_t d;
        unsigned long long got;

        if (cg_ble_device_info(i, &d) != 0) continue;
        if (by_addr) {
            if (cg_ble_addr_from_str(d.address, &got) == 0 && got == want) { *out = d; return 0; }
        } else if (d.name[0]) {
            /* 名字用**不区分大小写的子串**匹配：现场记的名字常常只是设备名的一段 */
            const char *h = d.name, *t = addr_or_name;
            int         ok = 0;

            for (; *h; h++) {
                const char *p = h, *q = t;

                while (*p && *q) {
                    char cp = (*p >= 'A' && *p <= 'Z') ? (char)(*p + 32) : *p;
                    char cq = (*q >= 'A' && *q <= 'Z') ? (char)(*q + 32) : *q;

                    if (cp != cq) break;
                    p++; q++;
                }
                if (!*q) { ok = 1; break; }
            }
            if (ok) { *out = d; return 0; }
        }
    }
    return -1;                                                /* 没找到就是没找到，不编一个出来 */
}
