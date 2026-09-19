# ESP-IDF CLI 命令速查（Windows + Git Bash）

排错时保持简短；出问题就把下方"该回贴的信息"贴回来。

## 推荐工作流

```bash
# 1. 激活 ESP-IDF 环境（每个新终端一次）
. $IDF_PATH/export.sh
# 或： . /d/esp/esp-idf/export.sh

# 2. 进入子项目并编译
cd proj_c2l_mainboard_p4
idf.py set-target esp32p4
idf.py build
idf.py -p COM5 flash monitor
```

## 环境自检
- 确认 idf.py 存在：`command -v idf.py`、`idf.py --version`
- 若 `idf.py` 缺失：`. $IDF_PATH/export.sh`
- ESP-IDF 工程目录应包含：`CMakeLists.txt`、`main/`

## 常用命令
- 选芯片：`idf.py set-target esp32p4 / esp32s3 / esp32c2`
- 配置：`idf.py menuconfig`（独立窗口）
- 编译：`idf.py build`
- 烧录：`idf.py -p COM5 -b 921600 flash`
- 监控：`idf.py -p COM5 monitor`（退出 `Ctrl+]`）
- 烧录+监控：`idf.py -p COM5 flash monitor`
- 清理：`idf.py fullclean`
- 擦除（谨慎）：`idf.py -p COM5 erase-flash`

## 出错时该回贴的信息
1) 子项目 + 芯片 target
2) `idf.py --version`
3) 准确执行的命令
4) 输出中**最靠前**的错误行（通常在顶部）
5) 串口号是否存在（设备管理器 / `ls /dev/ttyS*` 视环境而定）

## 串口注意
- 监控输出乱码 → 大概率波特率不对。
- 烧录波特率（`-b`）与 app UART 波特率（menuconfig）可不同。
- 烧录不稳 → 降到 `-b 115200`。

## 本项目子项目对应芯片
- `proj_c2l_mainboard_p4` → esp32p4
- `proj_c2l_mainboard_s3` → esp32s3
- `esp32_c2_slave` → esp32c2
