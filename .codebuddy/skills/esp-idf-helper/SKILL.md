---
name: esp-idf-helper
description: ESP-IDF 构建/烧录/监控/调试命令行工作流。触发条件：配置 target、menuconfig、编译（idf.py build）、烧录（flash）、串口监控（monitor）、分区表、sdkconfig、加密烧录，或排查 build/flash/monitor 报错时。适配本项目多子项目结构（proj_c2l_mainboard_p4 / esp32_c2_slave）。
auto_invoke: true
argument-hint: [子项目 | target | 操作]
---

# 技能：ESP-IDF 命令行助手（ESP-IDF Helper）

**操作目标（可选）**：$ARGUMENTS

> 本 skill 提供可复用的 ESP-IDF 开发循环：**配置 → 编译 → 烧录 → 监控 → 调试/排错**。
> 已按本项目环境本地化：**Windows + Git Bash**，`idf.py` 直接可用（非 WSL/usbipd 场景）。

## ⚠️ 核心原则

- **先激活环境再操作**：`idf.py` 依赖 ESP-IDF 环境变量，每个新终端先激活一次。
- **选对子项目**：本仓库是多固件工程，`idf.py` 必须在**对应子项目目录**下运行。
- **报错先看第一段**：编译/烧录失败时，抓 **最靠前的错误行**，而不是最后的堆栈尾部。
- **烧录是真机操作**：`flash` / `erase-flash` 会覆盖设备，属中风险，执行前确认端口对应目标板。

## 本项目子项目

| 子项目目录 | 芯片 | 说明 |
|-----------|------|------|
| `proj_c2l_mainboard_p4` | ESP32-P4 | 主板主控固件 |
| `esp32_c2_slave` | ESP32-C2 | 从机固件 |

> **注**：`proj_c2l_mainboard_s3` 为废弃目录，不再维护。

---

## 典型开发循环

```bash
# 1) 激活 ESP-IDF 环境（每个新终端一次）
#    Git Bash 下调用 IDF 安装目录的 export 脚本
. $IDF_PATH/export.sh      # 若已设置 IDF_PATH
# 或指向具体安装路径： . /d/esp/esp-idf/export.sh

# 2) 进入目标子项目
cd proj_c2l_mainboard_p4

# 3) 设置芯片 target（每个子项目首次一次）
idf.py set-target esp32p4      # s3 用 esp32s3；c2 从机用 esp32c2

# 4) 编译
idf.py build

# 5) 烧录 + 监控（Windows 下端口形如 COM5）
idf.py -p COM5 flash monitor
```

## 常用命令速查

| 目的 | 命令 |
|------|------|
| 设置芯片 | `idf.py set-target esp32p4 / esp32s3 / esp32c2` |
| 配置（新窗口） | `idf.py menuconfig` |
| 编译 | `idf.py build` |
| 烧录固件 | `idf.py -p COM5 -b 921600 flash` |
| 串口监控 | `idf.py -p COM5 monitor`（退出 `Ctrl+]`） |
| 烧录+监控 | `idf.py -p COM5 flash monitor` |
| 仅烧 app | `idf.py -p COM5 app-flash` |
| 分区表 | `idf.py partition-table` / `partition-table-flash` |
| 固件体积 | `idf.py size` |
| 清理 | `idf.py fullclean` |
| 擦除（谨慎） | `idf.py -p COM5 erase-flash` |
| 更新组件依赖 | `idf.py update-dependencies` |

> `menuconfig` 是交互式 TUI，**必须在独立终端窗口**运行，不要放到非交互后台。

## 加密烧录（如项目启用 Flash 加密）

```bash
idf.py -p COM5 encrypted-flash        # 加密烧录整个固件
idf.py -p COM5 encrypted-app-flash    # 仅加密烧录 app 分区
idf.py -p COM5 app-flash              # 非加密烧录 app
```

---

## 排错决策树

- **还没有工程** → `idf.py create-project` 或从 example 创建，先确认 target 与 IDF 版本。
- **编译失败** → 抓最靠前的错误行；排查缺组件/工具链/CMake/Python 包；确认 IDF 环境已激活（`idf.py --version`）。
- **烧录失败** → 确认端口（`COMx` 是否存在/被占用）、波特率、boot 模式、芯片 target 是否匹配子项目。
- **监控乱码** → 波特率不对（monitor 用 app 日志波特率，默认 115200）或串口编码问题；**先排除波特率再怀疑代码**。
- **启动循环 / panic** → 抓 panic backtrace，用 `idf.py monitor` 内置解析或 `addr2line` 解码，检查分区表 / sdkconfig。
- **端口被占用** → 关闭占用该 COM 口的其它监控/串口工具后重试。

## 需要向用户确认的最小信息

1. 目标子项目（p4 / s3 / c2 从机）与芯片 target。
2. ESP-IDF 版本与安装/激活方式（`IDF_PATH`）。
3. 串口号（Windows 下 `COMx`）。
4. 出错时：**准确的命令 + 输出中最靠前的错误块**。

---

## 与本项目工作流的关系

- 是 `piv-validate` 第 1 级（编译门禁 `idf.py build`）和 `serial_verify`（第 3 级串口实测烧录/监控）的**执行手段**。
- 编译/烧录报错 → 回 `piv-implement` 修复；协议层问题配合 `communication_protocol_check` 定位根因。

## 附带资源

- `references/esp-idf-cli.md` —— 命令模式速查 + 报错时该回贴的信息。

> 注：原始 skill 的 WSL/usbipd 串口映射脚本（`usbipd_attach_serial.sh` 等）未纳入——本项目在 Windows + Git Bash 下 `idf.py` 直接访问 `COMx`，无需 WSL USB 透传。若未来切到 WSL2 开发，再补该套脚本。
