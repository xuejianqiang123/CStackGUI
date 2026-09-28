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
 *          **主动扫描（cg_wifi_scan）**：2026-09-27 曾经把它撤掉，理由是"实测返回 87
 *          （参数无效）、原因不明" —— **那个结论是错的**：真正的原因是本文件 `WlanScan` 的
 *          typedef 漏了最后那个 `PVOID pReserved`（同一个病，见上面的 typedef 注释），
 *          参数错位才报 87。修好后提交扫描返回 0，扫描接口已加回。
 *          ⇒ 记性不如头文件：**"实测不工作"之前，先把签名对一遍官方声明**。
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
/** @note **7 个参数，最后一个别漏！** 原来这里写成 6 个（少了 `PWLAN_OPCODE_VALUE_TYPE` 出口），
 *  x64 上第 7 个参数是**从栈上取**的 ⇒ wlanapi 会把"栈上捡到的垃圾"当指针写一个 4 字节出口值，
 *  等于往调用者栈帧里乱写：表现是**随构建布局漂移**的怪事（-O1 把别的函数的计数打坏成 62、
 *  -O2 崩在后面的 WLAN API 里），而 `-fstack-protector-all` 抓不到（写的是任意地址，不是本帧缓冲）。
 *  2026-09-27 追了一整天的"memcheck 套件数不对"就是它。
 *  ⇒ 运行期取符号 + 自制 typedef 时，**必须逐个参数对官方签名**。 */
typedef DWORD (WINAPI *p_wlan_query_t)(HANDLE, const GUID *, WLAN_INTF_OPCODE, void *,
                                       DWORD *, void **, WLAN_OPCODE_VALUE_TYPE *);
/** @note **5 个参数，第 4 个 `pReserved` 最容易漏**（原来只写了 4 个）：
 *  `WlanGetAvailableNetworkList(h, guid, dwFlags, pReserved, ppAvailableNetworkList)`。
 *  少一个参数的后果很隐蔽 —— 列表指针落进 pReserved 槽（我传的是 `&alist`），
 *  真正的出口参数成了**栈上捡到的垃圾地址**，wlanapi 于是往那儿写 8 字节 ⇒ **野写**：
 *    - 表现为"随构建布局漂移"的怪事（-O1 把别的函数的计数打坏成 62、-O2 崩在后面的 WLAN API 里）；
 *    - 而且 `alist` 永远是 NULL ⇒ **周边网络列表永远为空**，我还把它当成"系统扫描缓存是空的"
 *      解释掉了（空结果被解释掉，正是最该追的那种）。
 *  2026-09-27 追了一整天的"memcheck 套件数不对"就是这两个漏参数（`WlanQueryInterface` 漏第 7 个、
 *  这里漏第 4 个）。⇒ 运行期取符号 + 自制 typedef，**必须逐个参数对官方声明**（比对 MinGW 的
 *  wlanapi.h / bluetoothapis.h 就行，别凭记忆）。 */
typedef DWORD (WINAPI *p_wlan_avail_t)(HANDLE, const GUID *, DWORD, void *,
                                       PWLAN_AVAILABLE_NETWORK_LIST *);
/** @note **5 个参数，最后那个 `PVOID pReserved` 别漏** —— 原来写成 4 个，于是 `WlanScan`
 *  返回 **87（ERROR_INVALID_PARAMETER）**，而我把它当成"Windows 的行为"写进了文档与文件头 ✗。
 *  同一个病（漏参数 ⇒ 参数错位 + 栈上垃圾当指针）。修好后扫描是能用的，见 cg_wifi_scan()。 */
typedef DWORD (WINAPI *p_wlan_scan_t)(HANDLE, const GUID *, const PDOT11_SSID,
                                      const PWLAN_RAW_DATA, void *);
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
static p_wlan_scan_t  g_wlan_scan;
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

/**
 * @brief 把蓝牙地址的原始字节拼成对外那个 64 位表示（最高一组是最后一个字节）
 * @param b 指向 6 字节地址原始字节的数组，小端存放（b[0] 是打印出来最后一组）
 * @return unsigned long long 形如 0xAABBCCDDEEFF 的地址（即 AA:BB:CC:DD:EE:FF）
 */
static unsigned long long addr_of_bytes(const unsigned char *b);

/**
 * @brief 取 WLAN 那批符号（进程内一次）
 * @return int 1 必需符号齐备（可选的 WlanScan 看 g_wlan_scan）；0 打不开 wlanapi.dll 或缺符号
 * @note 试过就记住：第二次起只回报结果，不再 LoadLibrary，也不再刷日志
 */
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
    g_wlan_scan  = (p_wlan_scan_t)(void *)GetProcAddress(m, "WlanScan");   /* 可选：取不到就不提供扫描 */
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

/**
 * @brief 取（必要时建）WLAN 客户端句柄
 * @return HANDLE 进程内复用的客户端句柄；NULL 表示这块用不了（符号缺或开过一次失败）
 * @note 句柄按进程开一次、复用。**但别把这里当成哪个崩溃的解药**（更正一次归因）：
 *        2026-09-27 曾出现 `WlanCloseHandle` 崩在 `RPCRT4!NDRCContextBinding` 的调用栈，
 *        当时判断是"反复开关句柄"引起的，于是改成缓存 —— 后来查明那次崩溃的真正来源是
 *        **本文件两处 typedef 漏参数造成的野写**（见 `p_wlan_avail_t` / `p_wlan_query_t` 的注释），
 *        wlanapi 的内部状态被写坏了，才在后续调用里崩。缓存句柄本身没问题（一个够用、
 *        少一次开关），所以留着；但**因果要说对**：不是它在修 bug。
 *        代价：这一个句柄到进程结束才由系统回收。
 */
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

/**
 * @brief 取蓝牙那批符号（进程内一次）
 * @return int 1 无线电那 4 个必需符号齐备；0 打不开 DLL 或缺必需符号（设备枚举符号可以是 NULL）
 * @note 实测这些符号在 **BluetoothApis.dll**（不在 bthprops.cpl —— 那个只有无线电与设备枚举）
 * @note 与 WLAN 一样只试一次：失败后不再重复 LoadLibrary，也不重复刷日志
 */
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

/**
 * @brief 把宽字符串拷进 UTF-8 缓冲（截断也保证 NUL 结尾；空指针当空串）
 * @param src 源宽字符串；NULL 或源容量为 0 时按空串处理
 * @param src_cap 源数组的**容量**（以 wchar_t 计）。**必须给**，见下。
 * @param dst UTF-8 出口缓冲；NULL 或 size ≤ 0 时什么都不做
 * @param size dst 的字节容量（含结尾 NUL，内部只写 size-1 个字符）
 * @return void 无返回值
 * @details 不能用 `WideCharToMultiByte(..., -1, ...)` —— 那个要求**调用方保证源串有终止符**，
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

/**
 * @brief 数本机有几块无线网卡
 * @return int 无线网卡个数；0 = 本机没有无线网卡或 WLAN 服务没起（原因在日志里）
 */
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

/**
 * @brief 取第 idx 块无线网卡的状态
 * @param idx 网卡序号，0 起
 * @param out 状态出口；失败时只被清空并把 signal 置 -1，成功才填 description / state / ssid / signal
 * @return int 0 成功；-1 out 为空、序号越界或 wlanapi 取不到
 * @note 只问第 idx 块网卡：description 是网卡的 UTF-8 描述、state 是 WLAN 接口状态码、
 *       ssid 是当前连接（没连上是空串）、signal 是信号质量 0~100（没连上是 -1）
 */
int cg_wifi_info(int idx, cg_wifi_info_t *out)
{
    PWLAN_INTERFACE_INFO_LIST list = NULL;
    HANDLE  h;
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
            DWORD                      sz = 0;
            WLAN_OPCODE_VALUE_TYPE     vt = wlan_opcode_value_type_invalid;   /* 第 7 个参数：不关心也**必须给** */
            WLAN_CONNECTION_ATTRIBUTES *ca = NULL;

            if (g_wlan_query(h, &it->InterfaceGuid, wlan_intf_opcode_current_connection,
                             NULL, &sz, (void **)&ca, &vt) == 0 && ca) {
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

/**
 * @brief 让第一块无线网卡发起一次扫描
 * @return int 0 扫描请求已提交；-1 没有 WlanScan 符号、没有可用句柄或本机没有无线网卡
 * @note 结果是**异步**的：几秒后再调 cg_wifi_networks() 才看得到新列表
 * @note 只见 5（ERROR_ACCESS_DENIED）就是"位置权限"：Win10 起扫描要它，去「设置 > 隐私 > 位置」打开
 */
int cg_wifi_scan(void)
{
    PWLAN_INTERFACE_INFO_LIST ilist = NULL;
    HANDLE  h;
    int     ret = -1;

    if (!wlan_ready() || !g_wlan_scan) return -1;      /* 老系统没有 WlanScan：如实说不行 */
    h = wlan_handle();
    if (!h) return -1;
    if (g_wlan_enum(h, NULL, &ilist) == 0 && ilist && ilist->dwNumberOfItems > 0) {
        /* 只在第一块网卡上扫；结果是**异步**的 —— 几秒后自己去 cg_wifi_networks() 取。 */
        DWORD rc = g_wlan_scan(h, &ilist->InterfaceInfo[0].InterfaceGuid, NULL, NULL, NULL);

        if (rc == 0) {
            ret = 0;
        } else {
            /* 见到 5 就是"位置权限"：Win10 起扫描要它，去「设置 > 隐私 > 位置」打开 */
            cg_log(CG_LOG_WARN, "无线：触发扫描失败（err=%lu）%s", (unsigned long)rc,
                   rc == 5 ? " —— 去「设置 > 隐私 > 位置」把位置权限打开" : "");
        }
    }
    if (ilist) g_wlan_free(ilist);
    return ret;
}

/**
 * @brief 取周边 AP 列表（去重、只列有名字的），写进调用方给的缓冲
 * @param out 出口数组，调用方提供，一条网络一项
 * @param cap out 放得下多少条
 * @param count 出口：实际写进 out 的条数（去重后、最多 cap）；可为 NULL
 * @return int 0 查询已执行（一个都没看到也是 0）；-1 out 为空、cap ≤ 0、没有无线网卡或 wlanapi 取不到
 * @note 只看第一块网卡：多网卡同时看周边 AP 意义不大，而且列表会重复
 * @note 同名 SSID（多 BSSID / 多 band）只留一个、取信号最强的那条；隐藏 SSID（名字为空）不列出来
 */
int cg_wifi_networks(cg_wifi_net_t *out, int cap, int *count)
{
    PWLAN_INTERFACE_INFO_LIST     ilist = NULL;
    PWLAN_AVAILABLE_NETWORK_LIST  alist = NULL;
    HANDLE  h;
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
    if (g_wlan_avail(h, &ilist->InterfaceInfo[0].InterfaceGuid, 0, NULL, &alist) == 0 && alist) {
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

/**
 * @brief 数本机有几块蓝牙无线电
 * @return int 无线电块数；0 表示没有蓝牙或蓝牙符号取不到
 * @note 没有蓝牙是正常情况，不写日志刷屏
 */
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

/**
 * @brief 取第 idx 块蓝牙无线电的名字与地址
 * @param idx 无线电序号，0 起
 * @param out 出口；名字是 UTF-8、地址是 "AA:BB:CC:DD:EE:FF" 文本，成功时至少被清空
 * @return int 0 命中该序号（取属性失败时 name 与 address 仍为空）；-1 out 为空、序号越界或蓝牙取不到
 */
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

/**
 * @brief 枚举"已知设备"（已配对 / 记得过的），共用一个实现
 * @param one 出口：命中第 idx 个时写入该设备的原始信息，没命中就不动它
 * @param idx 要取的设备序号，0 起；给 -1 表示只要总数
 * @param total 出口：本次枚举到的设备总数（一台都没枚举到就写 0）；可为 NULL
 * @return int 1 命中并已写入 one；0 没命中
 * @note 不做主动搜索（fIssueInquiry = FALSE）：那要几十秒，而且是"发现"不是"状态"
 */
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

/**
 * @brief 数已知蓝牙设备（已配对 / 记得过的）有几台
 * @return int 设备台数；0 表示没有蓝牙、蓝牙符号取不到或一台都没记过
 */
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

/**
 * @brief 取第 idx 台已知蓝牙设备的名字、地址与连接状态
 * @param idx 设备序号，0 起
 * @param out 出口；成功才写：name 为 UTF-8 名字、address 为 "AA:BB:CC:DD:EE:FF" 文本、connected 与 remembered 为 0/1
 * @return int 0 成功；-1 out 为空、序号越界或蓝牙取不到
 */
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

/**
 * @brief 把 BLUETOOTH_ADDRESS 的 rgBytes 拼成对外那个 64 位表示（rgBytes[5] 是最高一组）
 * @param b 指向 6 字节地址原始字节的数组，小端存放（b[0] 是打印出来最后一组）
 * @return unsigned long long 形如 0xAABBCCDDEEFF 的地址（即 AA:BB:CC:DD:EE:FF）
 */
static unsigned long long addr_of_bytes(const unsigned char *b)
{
    unsigned long long v = 0;
    int                i;

    for (i = 5; i >= 0; i--) v = (v << 8) | b[i];
    return v;
}

/**
 * @brief 把 64 位地址写成 "AA:BB:CC:DD:EE:FF" 文本
 * @param addr 地址（0xAABBCCDDEEFF 就是 AA:BB:CC:DD:EE:FF）
 * @param out 出口缓冲，写完保证 NUL 结尾
 * @param out_size out 的字节容量；out 为空或容量 ≤ 0 时什么都不做
 * @return void 无返回值
 * @note 与 Windows 设置里显示的写法一致：**大写**、冒号分隔、最高字节在前
 */
void cg_ble_addr_str(unsigned long long addr, char *out, int out_size)
{
    if (!out || out_size <= 0) return;
    /* 与 Windows 设置里显示的写法一致：**大写**、冒号分隔、最高字节在前 */
    snprintf(out, (size_t)out_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)((addr >> 40) & 0xFF), (unsigned)((addr >> 32) & 0xFF),
             (unsigned)((addr >> 24) & 0xFF), (unsigned)((addr >> 16) & 0xFF),
             (unsigned)((addr >> 8) & 0xFF), (unsigned)(addr & 0xFF));
}

/**
 * @brief 把 "AA:BB:CC:DD:EE:FF" 这类文本解成 64 位地址
 * @param text 文本地址；分隔符随便（冒号 / 连字符 / 空格），十六进制位必须正好 12 个
 * @param out 出口：成功才写
 * @return int 0 成功；-1 text / out 为空、出现别的字符或位数不是 12
 */
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

/**
 * @brief 把 16 位 UUID 写成完整的 128 位标准基 UUID 文本
 * @param uuid16 16 位 UUID（0x0000 ~ 0xFFFF）
 * @param out 出口缓冲
 * @param out_size out 的字节容量，要 > 36 才放得下
 * @return int 写入的字符数（36）；-1 参数不合法或缓冲放不下
 * @note **小写**：蓝牙文档、nRF Connect、Windows 都这么写 —— 要跟数据手册逐字比对的就是它的字形
 *       （与"进制输出一律大写"那条不冲突：那条说的是寄存器数值，这条说的是 UUID 的字面量）
 */
int cg_ble_uuid16_str(unsigned short uuid16, char *out, int out_size)
{
    int n;

    if (!out || out_size <= 0) return -1;
    /* **小写**：蓝牙文档、nRF Connect、Windows 都这么写 —— 要跟数据手册逐字比对的就是它的字形
     * （与"进制输出一律大写"那条不冲突：那条说的是寄存器数值，这条说的是 UUID 的字面量） */
    n = snprintf(out, (size_t)out_size, "0000%04x-0000-1000-8000-00805f9b34fb", (unsigned)uuid16);
    return (n == 36 && n < out_size) ? n : -1;
}

/**
 * @brief 把一个字符当十六进制数字解（不是就返回 -1）
 * @param c 待解的字符（'0'~'9' / 'a'~'f' / 'A'~'F'）
 * @return int 0 ~ 15；-1 不是十六进制数字
 */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * @brief 从文本里解出 16 位 UUID（短写法和完整 128 位写法都认）
 * @param text 文本 UUID；空格 / 制表符 / 短横线 / 大括号 / 前缀 0x 都会被吃掉
 * @param out16 出口：成功才写解出的 16 位值
 * @return int 0 成功；-1 text / out16 为空、含非法字符、位数不对或长写法尾巴不是标准基 UUID
 * @note 长写法必须是标准基形态：去掉短横线后高 16 位是 0000、第 8 位起 24 位尾巴与基 UUID 一致，
 *       真正要取的是中间第 4~7 位
 */
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

/**
 * @brief 把文本 UUID 归一成"小写、去掉短横线/大括号/0x"的形式；放不下返回 -1
 * @param text 待归一的文本 UUID；空指针或含非十六进制字符都算非法
 * @param out 出口缓冲（归一到小写十六进制串）
 * @param out_size out 的字节容量（含结尾 NUL）
 * @return int 归一后的位数（大于 0）；-1 参数不合法、含非法字符或放不下
 */
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

/**
 * @brief 判断两个文本 UUID 是不是同一个
 * @param a 一侧文本 UUID（短写法或 128 位长写法都行）
 * @param b 另一侧文本 UUID
 * @return int 1 相等；0 不相等（任一为空或写错也算 0）
 * @note 能当 16 位解的两边就按 16 位比 —— 这样 "fff1" 与它那个长写法**相等**（最常见的用法）
 */
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

/**
 * @brief 把 GATT 特征的 properties 位翻译成 CG_BLE_ATTR_* 标志
 * @param properties 特征属性位原值（GATT 的 properties 字段）
 * @return int CG_BLE_ATTR_READ / WRITE / WRITE_NR / NOTIFY / INDICATE 的按位或；一位都不认时是 0
 */
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

/**
 * @brief 在已知设备里按地址或名字找一个
 * @param addr_or_name 地址文本（能解成地址就按地址找）或设备名（不区分大小写的子串）
 * @param out 出口：找到才写该设备的完整信息
 * @return int 0 找到；-1 参数为空或没找到（不编一个出来）
 * @note 名字用**不区分大小写的子串**匹配：现场记的名字常常只是设备名的一段
 */
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
