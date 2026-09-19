---
name: flash_storage_check
description: 审查 Flash 擦写寿命、NVS 存储、分区表、OTA 升级、数据持久化风险。
auto_invoke: true
allowed-tools: [ReadFile, GrepSearch]
---
# 技能：Flash 存储与数据持久化审查（专家级）

## 触发条件
识别到以下场景自动执行：
- Flash 擦除/写入操作（`spi_flash_erase_sector`、`FLASH_EraseSector`）
- NVS（Non-Volatile Storage）读写（`nvs_set_*`、`nvs_get_*`）
- EEPROM 模拟（`HAL_FLASH_Program`）
- 分区表操作（`esp_partition_*`）
- OTA 固件升级（`esp_ota_begin`、`esp_ota_write`）
- 文件系统（`LittleFS`、`SPIFFS`、`FATFS`）

---

## 完整检查清单（13 项致命风险）

### 1. 【致命】Flash 擦除时看门狗超时复位
**问题原理**：
- Flash 擦除操作非常耗时（64KB 扇区需 200-500ms）
- 擦除期间 CPU 被阻塞，无法喂狗
- 看门狗超时触发复位，导致**擦除中断、数据损坏**

**❌ 错误示例**：
```c
void ota_upgrade(void) {
    esp_task_wdt_init(2, true);  // 看门狗 2 秒超时
    esp_task_wdt_add(NULL);
    
    // 🔴 擦除 16 个扇区，每个 500ms，总共 8 秒
    for (int i = 0; i < 16; i++) {
        spi_flash_erase_sector(i);  // 🔴 阻塞 500ms
    }
    // 🔴 擦除到第 4 个扇区时看门狗就复位了！
    
    esp_task_wdt_reset();
}

// STM32 版本
void flash_erase_pages(void) {
    HAL_IWDG_Init(&hiwdg);       // 看门狗 1 秒超时
    
    HAL_FLASH_Unlock();
    for (int i = 0; i < 10; i++) {
        FLASH_Erase_Sector(i, VOLTAGE_RANGE_3);  // 🔴 每个扇区 2 秒
    }
    HAL_FLASH_Lock();
    // 🔴 还没擦完就复位了
}
```

**✅ 正确示例**：
```c
// 方案1：擦除前临时禁用看门狗
void ota_upgrade(void) {
    esp_task_wdt_delete(NULL);   // ✅ 移除当前任务监控
    
    for (int i = 0; i < 16; i++) {
        spi_flash_erase_sector(i);
    }
    
    esp_task_wdt_add(NULL);      // ✅ 擦除完成后重新加入
    esp_task_wdt_reset();
}

// 方案2：擦除过程中周期性喂狗
void ota_upgrade(void) {
    esp_task_wdt_init(5, true);  // ✅ 增加超时时间到 5 秒
    esp_task_wdt_add(NULL);
    
    for (int i = 0; i < 16; i++) {
        spi_flash_erase_sector(i);
        if (i % 4 == 0) {
            esp_task_wdt_reset();  // ✅ 每擦除 4 个扇区喂一次狗
        }
    }
}

// 方案3：使用非阻塞异步擦除（ESP32）
void ota_upgrade_async(void) {
    for (int i = 0; i < 16; i++) {
        esp_partition_erase_range(partition, i * 4096, 4096);
        esp_task_wdt_reset();    // ✅ 每个扇区擦除后喂狗
        vTaskDelay(pdMS_TO_TICKS(10));  // 让其他任务运行
    }
}
```

**💥 后果**：
- Flash 擦除中断，数据损坏
- OTA 升级失败，设备变砖
- NVS 数据丢失

---

### 2. 【致命】关键数据未做掉电保护
**问题原理**：
- Flash 写入过程中掉电，数据只写了一半
- 下次启动时读取到损坏的数据，系统崩溃

**❌ 错误示例**：
```c
typedef struct {
    uint32_t magic;              // 魔数
    uint32_t sensor_count;       // 传感器读取次数
    char wifi_ssid[32];          // WiFi SSID
    char wifi_password[64];      // WiFi 密码
} config_t;

void save_config(config_t *cfg) {
    nvs_handle_t nvs_h;
    nvs_open("config", NVS_READWRITE, &nvs_h);
    
    // 🔴 直接写入，无 CRC 校验，掉电时数据损坏
    nvs_set_blob(nvs_h, "cfg", cfg, sizeof(config_t));
    nvs_commit(nvs_h);
    nvs_close(nvs_h);
}

void load_config(config_t *cfg) {
    nvs_handle_t nvs_h;
    nvs_open("config", NVS_READONLY, &nvs_h);
    
    size_t len = sizeof(config_t);
    nvs_get_blob(nvs_h, "cfg", cfg, &len);  // 🔴 读取损坏的数据
    nvs_close(nvs_h);
    
    // 🔴 未校验数据完整性，直接使用
    wifi_connect(cfg->wifi_ssid, cfg->wifi_password);
}
```

**✅ 正确示例**：
```c
// 方案1：数据 + CRC 校验
typedef struct {
    uint32_t magic;
    uint32_t sensor_count;
    char wifi_ssid[32];
    char wifi_password[64];
    uint32_t crc32;              // ✅ CRC 校验
} config_t;

uint32_t calculate_crc32(const void *data, size_t len) {
    // CRC32 计算实现
    return crc_value;
}

void save_config(config_t *cfg) {
    cfg->magic = 0x12345678;
    cfg->crc32 = calculate_crc32(cfg, sizeof(config_t) - 4);  // ✅ 计算 CRC
    
    nvs_handle_t nvs_h;
    nvs_open("config", NVS_READWRITE, &nvs_h);
    nvs_set_blob(nvs_h, "cfg", cfg, sizeof(config_t));
    nvs_commit(nvs_h);
    nvs_close(nvs_h);
}

void load_config(config_t *cfg) {
    nvs_handle_t nvs_h;
    nvs_open("config", NVS_READONLY, &nvs_h);
    
    size_t len = sizeof(config_t);
    esp_err_t ret = nvs_get_blob(nvs_h, "cfg", cfg, &len);
    nvs_close(nvs_h);
    
    // ✅ 校验数据完整性
    if (ret != ESP_OK || cfg->magic != 0x12345678) {
        ESP_LOGE(TAG, "Config invalid, use default");
        load_default_config(cfg);
        return;
    }
    
    uint32_t crc = calculate_crc32(cfg, sizeof(config_t) - 4);
    if (crc != cfg->crc32) {
        ESP_LOGE(TAG, "CRC mismatch, config corrupted");
        load_default_config(cfg);
        return;
    }
    
    // ✅ 数据完整，可以使用
    wifi_connect(cfg->wifi_ssid, cfg->wifi_password);
}

// 方案2：双备份 + 版本号
void save_config(config_t *cfg) {
    cfg->version++;
    cfg->crc32 = calculate_crc32(cfg, sizeof(config_t) - 4);
    
    nvs_handle_t nvs_h;
    nvs_open("config", NVS_READWRITE, &nvs_h);
    
    // ✅ 双备份：cfg_a 和 cfg_b
    nvs_set_blob(nvs_h, "cfg_a", cfg, sizeof(config_t));
    nvs_commit(nvs_h);
    
    nvs_set_blob(nvs_h, "cfg_b", cfg, sizeof(config_t));
    nvs_commit(nvs_h);
    
    nvs_close(nvs_h);
}

void load_config(config_t *cfg) {
    config_t cfg_a, cfg_b;
    bool a_valid = false, b_valid = false;
    
    nvs_handle_t nvs_h;
    nvs_open("config", NVS_READONLY, &nvs_h);
    
    // ✅ 读取两个备份
    size_t len = sizeof(config_t);
    if (nvs_get_blob(nvs_h, "cfg_a", &cfg_a, &len) == ESP_OK) {
        if (validate_config(&cfg_a)) {
            a_valid = true;
        }
    }
    
    len = sizeof(config_t);
    if (nvs_get_blob(nvs_h, "cfg_b", &cfg_b, &len) == ESP_OK) {
        if (validate_config(&cfg_b)) {
            b_valid = true;
        }
    }
    
    nvs_close(nvs_h);
    
    // ✅ 选择版本号更高的有效备份
    if (a_valid && b_valid) {
        *cfg = (cfg_a.version > cfg_b.version) ? cfg_a : cfg_b;
    } else if (a_valid) {
        *cfg = cfg_a;
    } else if (b_valid) {
        *cfg = cfg_b;
    } else {
        ESP_LOGE(TAG, "Both backups corrupted!");
        load_default_config(cfg);
    }
}
```

**💥 后果**：
- 关键配置损坏（WiFi 密码、设备 ID）
- 系统无法启动
- 设备需要重新配置（用户体验极差）

---

### 3. 【致命】OTA 升级失败无回滚机制
**问题原理**：
- OTA 升级过程中断电或写入失败
- 新固件损坏无法启动，旧固件已被覆盖
- 设备变砖，无法恢复

**❌ 错误示例**：
```c
void ota_upgrade(const uint8_t *firmware, size_t size) {
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, "factory");
    
    // 🔴 直接覆盖当前运行的分区
    esp_partition_erase_range(part, 0, size);
    esp_partition_write(part, 0, firmware, size);
    
    // 🔴 无校验，无回滚
    esp_restart();  // 🔴 如果新固件损坏，设备变砖！
}
```

**✅ 正确示例**：
```c
// 方案1：使用 ESP32 的 OTA 分区机制
void ota_upgrade(const uint8_t *firmware, size_t size) {
    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    
    esp_ota_handle_t ota_handle;
    esp_ota_begin(update_part, size, &ota_handle);  // ✅ 写入备用分区
    
    esp_ota_write(ota_handle, firmware, size);
    esp_ota_end(ota_handle);
    
    // ✅ 设置启动分区（但还未重启）
    esp_ota_set_boot_partition(update_part);
    
    esp_restart();
}

void app_main(void) {
    // ✅ 启动后检查固件有效性
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    esp_ota_get_state_partition(running, &ota_state);
    
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        // ✅ 首次启动，进行验证
        if (self_test_passed()) {
            ESP_LOGI(TAG, "OTA success, mark as valid");
            esp_ota_mark_app_valid_cancel_rollback();  // ✅ 确认有效
        } else {
            ESP_LOGE(TAG, "OTA failed, rollback");
            esp_ota_mark_app_invalid_rollback_and_reboot();  // ✅ 回滚到旧版本
        }
    }
}

// 方案2：STM32 双 Bank 切换
void stm32_ota_upgrade(const uint8_t *firmware, size_t size) {
    // ✅ 写入备用 Bank
    uint32_t backup_bank = (current_bank == FLASH_BANK_1) ? FLASH_BANK_2 : FLASH_BANK_1;
    
    HAL_FLASH_Unlock();
    FLASH_Erase_Bank(backup_bank);
    
    for (size_t i = 0; i < size; i += 8) {
        HAL_FLASH_Program(TYPEPROGRAM_DOUBLEWORD, backup_bank + i, *(uint64_t*)(firmware + i));
    }
    HAL_FLASH_Lock();
    
    // ✅ 切换启动 Bank（重启后从备用 Bank 启动）
    FLASH_OBProgramInitTypeDef ob_config;
    HAL_FLASHEx_OBGetConfig(&ob_config);
    ob_config.USERConfig ^= OB_BFB2_ENABLE;  // 切换 Bank
    HAL_FLASHEx_OBProgram(&ob_config);
    
    HAL_FLASH_OB_Launch();  // 重启
}
```

**💥 后果**：
- 设备变砖，无法恢复
- 需要硬件调试器重新烧录

---

### 4. 【高危】频繁写入同一扇区导致寿命耗尽
**问题原理**：
- Flash 擦写寿命有限（SLC: 10 万次，MLC: 1 万次，TLC: 3000 次）
- 频繁写入同一扇区会快速耗尽寿命
- 示例：每分钟写入一次，SLC Flash 69 天后损坏

**❌ 错误示例**：
```c
void save_sensor_data(uint16_t value) {
    // 🔴 每次都擦除同一扇区
    spi_flash_erase_sector(100);
    spi_flash_write(100 * 4096, &value, sizeof(value));
}

void main_loop(void) {
    while (1) {
        uint16_t temp = read_temperature();
        save_sensor_data(temp);  // 🔴 每分钟写入一次
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

// 🔴 寿命计算：100,000 次 / (60 次/小时 * 24 小时) = 69 天
```

**✅ 正确示例**：
```c
// 方案1：使用 NVS（内置磨损均衡）
void save_sensor_data(uint16_t value) {
    nvs_handle_t nvs_h;
    nvs_open("sensor", NVS_READWRITE, &nvs_h);
    nvs_set_u16(nvs_h, "temp", value);  // ✅ NVS 自动做磨损均衡
    nvs_commit(nvs_h);
    nvs_close(nvs_h);
}

// 方案2：累积写入，减少擦除次数
#define LOG_SIZE 256
typedef struct {
    uint16_t values[LOG_SIZE];
    uint16_t index;
} sensor_log_t;

static sensor_log_t g_log = {0};

void save_sensor_data(uint16_t value) {
    g_log.values[g_log.index++] = value;
    
    // ✅ 累积 256 个数据再写入一次
    if (g_log.index >= LOG_SIZE) {
        spi_flash_erase_sector(100);
        spi_flash_write(100 * 4096, &g_log, sizeof(g_log));
        g_log.index = 0;
    }
}

// 🔴 写入频率降低 256 倍：69 天 * 256 = 48 年

// 方案3：循环写入多个扇区（手动磨损均衡）
#define SECTOR_COUNT 16
static uint8_t current_sector = 0;

void save_sensor_data(uint16_t value) {
    uint32_t addr = (100 + current_sector) * 4096;
    spi_flash_erase_sector(100 + current_sector);
    spi_flash_write(addr, &value, sizeof(value));
    
    current_sector = (current_sector + 1) % SECTOR_COUNT;  // ✅ 轮流使用 16 个扇区
}

// 🔴 寿命延长 16 倍：69 天 * 16 = 3 年
```

**💥 后果**：
- Flash 扇区损坏，数据丢失
- 设备寿命缩短

---

### 5. 【高危】写入前未判断返回值
**问题原理**：
- Flash 写入可能失败（扇区损坏、电压不足）
- 未检查返回值，以为写入成功，实际失败

**❌ 错误示例**：
```c
void save_config(config_t *cfg) {
    spi_flash_erase_sector(100);
    spi_flash_write(100 * 4096, cfg, sizeof(config_t));  // 🔴 未检查返回值
    
    ESP_LOGI(TAG, "Config saved");  // 🔴 可能根本没写成功
}
```

**✅ 正确示例**：
```c
void save_config(config_t *cfg) {
    esp_err_t ret;
    
    ret = spi_flash_erase_sector(100);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Erase failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = spi_flash_write(100 * 4096, cfg, sizeof(config_t));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Write failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // ✅ 写入后读回校验
    config_t verify;
    ret = spi_flash_read(100 * 4096, &verify, sizeof(config_t));
    if (ret != ESP_OK || memcmp(cfg, &verify, sizeof(config_t)) != 0) {
        ESP_LOGE(TAG, "Verify failed!");
        return;
    }
    
    ESP_LOGI(TAG, "Config saved and verified");
}
```

**💥 后果**：
- 配置未保存，但程序以为保存成功
- 重启后丢失配置

---

### 6. 【高危】分区表越界写入
**问题原理**：
- 写入地址超出分区边界
- 覆盖其他分区数据（固件、NVS）

**❌ 错误示例**：
```c
void write_log(const uint8_t *data, size_t len) {
    static uint32_t offset = 0;
    
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    
    // 🔴 未检查边界
    esp_partition_write(part, offset, data, len);
    offset += len;  // 🔴 offset 可能超出分区大小
}
```

**✅ 正确示例**：
```c
void write_log(const uint8_t *data, size_t len) {
    static uint32_t offset = 0;
    
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    
    // ✅ 检查边界
    if (offset + len > part->size) {
        ESP_LOGW(TAG, "Partition full, wrap around");
        offset = 0;  // ✅ 循环写入
    }
    
    esp_partition_write(part, offset, data, len);
    offset += len;
}
```

**💥 后果**：
- 覆盖固件分区，设备变砖
- 覆盖 NVS 分区，配置丢失

---

### 7. 【高危】NVS 命名空间冲突
**问题原理**：
- 多个模块使用相同的 NVS 命名空间和键名
- 互相覆盖数据

**❌ 错误示例**：
```c
// 模块 A
void module_a_save(void) {
    nvs_handle_t nvs_h;
    nvs_open("config", NVS_READWRITE, &nvs_h);  // 🔴 命名空间 "config"
    nvs_set_u32(nvs_h, "value", 100);           // 🔴 键名 "value"
    nvs_commit(nvs_h);
    nvs_close(nvs_h);
}

// 模块 B
void module_b_save(void) {
    nvs_handle_t nvs_h;
    nvs_open("config", NVS_READWRITE, &nvs_h);  // 🔴 相同命名空间
    nvs_set_u32(nvs_h, "value", 200);           // 🔴 覆盖了模块 A 的数据！
    nvs_commit(nvs_h);
    nvs_close(nvs_h);
}
```

**✅ 正确示例**：
```c
// 方案1：使用独立命名空间
void module_a_save(void) {
    nvs_handle_t nvs_h;
    nvs_open("module_a", NVS_READWRITE, &nvs_h);  // ✅ 独立命名空间
    nvs_set_u32(nvs_h, "value", 100);
    nvs_commit(nvs_h);
    nvs_close(nvs_h);
}

void module_b_save(void) {
    nvs_handle_t nvs_h;
    nvs_open("module_b", NVS_READWRITE, &nvs_h);  // ✅ 独立命名空间
    nvs_set_u32(nvs_h, "value", 200);
    nvs_commit(nvs_h);
    nvs_close(nvs_h);
}

// 方案2：使用前缀区分键名
void module_a_save(void) {
    nvs_handle_t nvs_h;
    nvs_open("config", NVS_READWRITE, &nvs_h);
    nvs_set_u32(nvs_h, "a_value", 100);  // ✅ 前缀 "a_"
    nvs_commit(nvs_h);
    nvs_close(nvs_h);
}

void module_b_save(void) {
    nvs_handle_t nvs_h;
    nvs_open("config", NVS_READWRITE, &nvs_h);
    nvs_set_u32(nvs_h, "b_value", 200);  // ✅ 前缀 "b_"
    nvs_commit(nvs_h);
    nvs_close(nvs_h);
}
```

**💥 后果**：
- 数据互相覆盖
- 模块功能异常

---

### 8. 【中危】大块写入未分片导致阻塞
**问题原理**：
- 一次写入大块数据（如 1MB）
- 长时间阻塞 CPU，其他任务饿死

**❌ 错误示例**：
```c
void save_image(const uint8_t *img, size_t size) {
    // 🔴 一次写入 1MB，阻塞 10 秒
    esp_partition_write(part, 0, img, size);
}
```

**✅ 正确示例**：
```c
void save_image(const uint8_t *img, size_t size) {
    const size_t chunk_size = 4096;  // ✅ 每次写入 4KB
    
    for (size_t offset = 0; offset < size; offset += chunk_size) {
        size_t len = (size - offset > chunk_size) ? chunk_size : (size - offset);
        esp_partition_write(part, offset, img + offset, len);
        
        vTaskDelay(pdMS_TO_TICKS(10));  // ✅ 让其他任务运行
        esp_task_wdt_reset();           // ✅ 喂狗
    }
}
```

**💥 后果**：
- 系统卡死
- 看门狗复位

---

### 9. 【中危】未做写平衡（Wear Leveling）
**问题原理**：
- 手动管理 Flash 时未做磨损均衡
- 某些扇区频繁使用，寿命耗尽

**✅ 正确示例**：
```c
// 使用 ESP32 的 Wear Leveling 层
wl_handle_t wl_handle;

void init_storage(void) {
    const esp_partition_t *part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    
    // ✅ 使用 Wear Leveling
    wl_mount(part, &wl_handle);
}

void write_data(uint32_t addr, const void *data, size_t len) {
    // ✅ 通过 WL 层写入，自动磨损均衡
    wl_write(wl_handle, addr, data, len);
}
```

**💥 后果**：
- Flash 寿命缩短

---

### 10. 【中危】Flash 操作期间中断冲突
**问题原理**：
- Flash 操作期间禁止访问 Flash（包括中断向量）
- 中断触发时可能崩溃

**❌ 错误示例**：
```c
void flash_write(void) {
    spi_flash_write(addr, data, len);  // 🔴 期间 WiFi 中断触发，访问 Flash 代码，崩溃
}
```

**✅ 正确示例**：
```c
void flash_write(void) {
    // ✅ Flash 操作前禁用中断
    spi_flash_guard_funcs_t *guard = spi_flash_guard_get();
    if (guard && guard->start) {
        guard->start();
    }
    
    spi_flash_write(addr, data, len);
    
    // ✅ 恢复中断
    if (guard && guard->end) {
        guard->end();
    }
}

// 或使用 ESP-IDF 的安全 API
esp_partition_write(part, offset, data, len);  // ✅ 自动处理中断
```

---

### 11. 【低危】Flash 加密未启用
**问题原理**：
- Flash 内容明文存储
- 攻击者可读取固件和密钥

**✅ 正确配置**：
```
# ESP32 启用 Flash 加密
menuconfig → Security features → Enable flash encryption on boot
```

---

### 12. 【低危】未记录擦写次数
**问题原理**：
- 无法监控 Flash 寿命
- 无法预测何时需要更换设备

**✅ 正确示例**：
```c
static uint32_t g_erase_count = 0;

void flash_erase_with_counter(uint32_t sector) {
    spi_flash_erase_sector(sector);
    g_erase_count++;
    
    if (g_erase_count % 1000 == 0) {
        ESP_LOGW(TAG, "Flash erased %d times", g_erase_count);
    }
    
    // ✅ 定期保存到 NVS
    if (g_erase_count % 100 == 0) {
        nvs_set_u32(nvs_h, "erase_cnt", g_erase_count);
    }
}
```

---

### 13. 【低危】分区表配置不合理
**问题原理**：
- OTA 分区过小，无法升级
- NVS 分区过小，配置存不下

**✅ 正确配置**：
```csv
# partitions.csv
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x6000   # 24KB NVS
phy_init, data, phy,     0xf000,  0x1000   # 4KB
factory,  app,  factory, 0x10000, 0x180000 # 1.5MB 固件
ota_0,    app,  ota_0,   0x190000,0x180000 # 1.5MB OTA
ota_1,    app,  ota_1,   0x310000,0x180000 # 1.5MB OTA
storage,  data, spiffs,  0x490000,0x170000 # 1.4MB 存储
```

---

## 强制规范

### Flash 操作原则
1. **擦除前必须禁用看门狗或增加超时时间**
2. **关键数据必须 CRC 校验 + 双备份**
3. **OTA 必须支持回滚**
4. **所有写入必须检查返回值**
5. **频繁写入必须使用 NVS 或磨损均衡**

### 数据保护原则
1. 魔数（Magic Number）验证数据有效性
2. 版本号（Version）支持数据迁移
3. CRC32/CRC16 校验数据完整性
4. 双备份防止掉电损坏

### Flash 寿命管理
```c
// 擦写次数限制（仅供参考）
// - SLC Flash: 100,000 次
// - MLC Flash: 10,000 次
// - TLC Flash: 3,000 次
// - QLC Flash: 1,000 次

// 寿命计算公式
// 寿命（天） = 擦写次数 / (每天写入次数)
// 示例：100,000 / (60*24) = 69 天
```

---

## 输出格式
【风险等级】致命/高/中/低
【位置】文件名:行号:函数名
【问题类型】看门狗冲突/掉电损坏/寿命耗尽/越界写入
【擦写频率】每天 XXX 次，预计寿命 XXX 天
【错误代码片段】
【问题原理】为什么会导致数据损坏/寿命缩短
【修复方案 + 正确示例】
【寿命优化建议】
