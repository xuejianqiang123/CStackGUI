# 无线状态（WiFi / 蓝牙）与下一步的 BLE 组件

## 方向怎么定的

用户补充："我们偏向 IoT，希望有 wifi 和 ble 控件"。据此把原建议（Modbus 示例）降为排队项，
先做与"跟板子通信"直接相关的无线能力。两个"控件"按第一性原理拆开：

- **WiFi** = PC 本机的无线状态（我在哪个网络 / 目标 AP 在不在）—— 用来**排除自己这边**；
- **BLE** = 上位机跟板子的 BLE 服务通信 —— IoT 调试上位机的核心，但它是"组件"级的大片。

## 探针实测（先验证再设计，这是本工程的铁律）

两个探针都在 `build\probe_link\`：

1. `wireless_probe.c`：用**工程现有链接参数**编 ⇒ WLAN 与蓝牙符号全部 `undefined reference`
   ⇒ **直接链会让所有既有用户项目的链接命令都得改** ⇒ 只能 `LoadLibraryW + GetProcAddress`。
2. `wireless_probe2.c`（运行期）：
   - `wlanapi.dll` → 7/7 个 WLAN 符号；
   - **`BluetoothApis.dll` → 13/13 个蓝牙符号（含 GATT 读/写/订阅）**；
     `bthprops.cpl` 只有无线电与设备枚举 —— 这条**不是猜的**；
   - 本机硬件：蓝牙无线电 **1 个** ✓、无线网卡 **1 块**（正连着 ChinaNet-cGTw-5G，信号 90%）✓。
   - ⚠️ 第一版探针报"无线接口 0 块"是**探针自己的 bug**（按偏移 4 读 `dwNumberOfItems`，
     其实在偏移 0，偏移 4 是保留的 `dwIndex`）—— 我照它写了"这台机器没有无线网卡"的结论，
     被库代码的真机输出当场纠正。**探针自己也会有 bug，结论要跟真实输出对一遍。**

## 已交付（本次）

`cg_kit_wireless.c`：`cg_wifi_count/info/networks` + `cg_ble_radio_count/info` +
`cg_ble_device_count/info`。零新增链接依赖；不持状态；取不到时明确返回 0 个 / -1 并写日志。

**没做**：主动扫描（`WlanScan` 实测返回 87 = ERROR_INVALID_PARAMETER，参数与文档一致却失败、
原因不明 ⇒ 撤掉，列表只用系统缓存）。**没验**：周边网络列表非空的那条路径（本机缓存一直为空）。

## 下一步：BLE 组件（IoT 的核心片）

事实依据（探针已给）：GATT 全套符号在 `BluetoothApis.dll` 里，纯 C 可用，**不需要 WinRT**：

- `BluetoothGATTGetServices` / `GetCharacteristics` / `GetDescriptors`
- `BluetoothGATTGetCharacteristicValue` / `SetCharacteristicValue`
- `BluetoothGATTRegisterEvent` / `UnregisterEvent`（**订阅通知**）

设计要点（照串口组件的形状）：无界面组件 + 后台线程/回调 → 主线程派发事件；
`.cgui` 里是个图标；业务收 `arrive` 类事件后取数据。
**必须先说清的边界**：这套 API 面向**已配对/已知设备**，不是"扫描广播"（那要 WinRT）——
所以组件第一步是"连我配对过的板子 + 读写特征 + 订阅通知"，配对仍走 Windows 设置。

**验收前置条件**：需要一块真 BLE 外设（用户的板子）。本机有蓝牙无线电但**没有可测的外设** ⇒
届时要分清"代码对"与"没设备所以没验"，别混。
