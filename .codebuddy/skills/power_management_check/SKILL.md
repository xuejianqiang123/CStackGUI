---
name: power_management_check
description: 审查低功耗设计、睡眠模式、唤醒源配置、外设时钟门控、功耗优化风险。
auto_invoke: true
allowed-tools: [ReadFile, GrepSearch]
---
# 技能：低功耗与电源管理审查（专家级）

## 触发条件
识别到以下场景自动执行：
- 睡眠模式配置（`esp_light_sleep_start`、`esp_deep_sleep_start`、`HAL_PWR_EnterSLEEPMode`）
- 唤醒源配置（`esp_sleep_enable_timer_wakeup`、`gpio_wakeup_enable`、`PWR_WakeUpPinCmd`）
- 电源管理 API（`esp_pm_configure`、`HAL_PWREx_*`）
- 时钟门控、外设开关（`__HAL_RCC_*_CLK_DISABLE`、`periph_module_disable`）
- GPIO 配置（上拉/下拉/高阻态）
- 电池供电、ADC 检测电压

---

## 完整检查清单（11 项致命风险）

### 1. 【致命】睡眠前未保存关键状态数据
**问题原理**：
- Deep Sleep 会断电 RAM，所有变量丢失
- 唤醒后相当于重启，未保存的状态永久丢失

**❌ 错误示例**：
```c
typedef struct {
    uint32_t sensor_count;       // 传感器读取次数
    uint8_t connection_status;   // 连接状态
    uint16_t battery_voltage;    // 电池电压
} system_state_t;

system_state_t g_state = {0};

void enter_deep_sleep(void) {
    // 🔴 未保存 g_state，唤醒后全部丢失！
    esp_deep_sleep_start();
}

void app_main(void) {
    // 唤醒后 g_state 全是 0，之前的数据全丢了
    printf("Count: %d\n", g_state.sensor_count);  // 🔴 永远是 0
}
```

**✅ 正确示例**：
```c
// 方案1：使用 RTC 内存（Deep Sleep 不断电）
RTC_DATA_ATTR system_state_t g_state = {0};  // ✅ 存储在 RTC 内存

void enter_deep_sleep(void) {
    // RTC 内存自动保持，无需手动保存
    esp_deep_sleep_start();
}

void app_main(void) {
    // ✅ 唤醒后 RTC 内存数据仍然存在
    printf("Count: %d\n", g_state.sensor_count);
}

// 方案2：保存到 NVS（非易失性存储）
void enter_deep_sleep(void) {
    nvs_handle_t nvs_h;
    nvs_open("storage", NVS_READWRITE, &nvs_h);
    nvs_set_u32(nvs_h, "count", g_state.sensor_count);
    nvs_set_u8(nvs_h, "status", g_state.connection_status);
    nvs_commit(nvs_h);           // ✅ 持久化存储
    nvs_close(nvs_h);
    
    esp_deep_sleep_start();
}

void app_main(void) {
    nvs_handle_t nvs_h;
    nvs_open("storage", NVS_READONLY, &nvs_h);
    nvs_get_u32(nvs_h, "count", &g_state.sensor_count);  // ✅ 恢复数据
    nvs_close(nvs_h);
}

// 方案3：STM32 使用 Backup SRAM
void enter_standby(void) {
    // ✅ 关键数据写入 Backup SRAM（VBAT 供电）
    *(__IO uint32_t *)(BKPSRAM_BASE) = g_state.sensor_count;
    
    HAL_PWR_EnterSTANDBYMode();
}
```

**💥 后果**：
- 唤醒后系统状态丢失
- 计数器清零、配置丢失
- 设备功能异常（忘记已连接的 WiFi、传感器校准数据丢失）

---

### 2. 【致命】唤醒后硬件未重新初始化
**问题原理**：
- Deep Sleep 断电后外设寄存器复位
- 唤醒后必须重新配置外设

**❌ 错误示例**：
```c
void app_main(void) {
    // 首次启动时初始化
    gpio_init();
    uart_init();
    i2c_init();
    
    while (1) {
        read_sensor();
        esp_deep_sleep(60 * 1000000);  // 睡眠 60 秒
        
        // 🔴 唤醒后未重新初始化，外设配置全丢了！
        read_sensor();                 // 🔴 I2C 已失效，读取失败
    }
}
```

**✅ 正确示例**：
```c
void app_main(void) {
    // ✅ 检查唤醒原因，决定是否需要初始化
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED) {
        // 首次上电或复位
        ESP_LOGI(TAG, "Cold boot");
        full_system_init();
    } else {
        // Deep Sleep 唤醒
        ESP_LOGI(TAG, "Wake from deep sleep");
        reinit_peripherals();    // ✅ 重新初始化外设
    }
    
    while (1) {
        read_sensor();
        esp_deep_sleep(60 * 1000000);
    }
}

void reinit_peripherals(void) {
    gpio_init();
    uart_init();
    i2c_init();
    // ✅ 恢复所有外设配置
}

// STM32 版本
void main(void) {
    if (__HAL_PWR_GET_FLAG(PWR_FLAG_SB) != RESET) {
        // ✅ 从 Standby 唤醒
        __HAL_PWR_CLEAR_FLAG(PWR_FLAG_SB);
        reinit_peripherals();
    } else {
        // 首次上电
        full_system_init();
    }
}
```

**💥 后果**：
- 外设无法工作（I2C/SPI/UART 失效）
- 传感器读取失败
- GPIO 状态错误

---

### 3. 【致命】GPIO 未配置为高阻态导致漏电
**问题原理**：
- GPIO 默认配置可能有上拉/下拉电阻
- 睡眠时 GPIO 保持驱动状态会产生漏电流（μA 级别）
- 电池供电设备睡眠电流从 10μA 飙升到 500μA

**❌ 错误示例**：
```c
void enter_deep_sleep(void) {
    // 🔴 GPIO 保持默认配置（上拉/下拉），产生漏电
    esp_deep_sleep_start();
}

// STM32 版本
void enter_standby(void) {
    // 🔴 GPIO 保持输出高电平，驱动外部电路持续耗电
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0, GPIO_PIN_SET);
    HAL_PWR_EnterSTANDBYMode();
}
```

**✅ 正确示例**：
```c
// 方案1：配置为高阻态（输入+禁用上下拉）
void enter_deep_sleep(void) {
    // ✅ 所有 GPIO 配置为输入 + 禁用上下拉
    for (int i = 0; i < GPIO_NUM_MAX; i++) {
        if (i == GPIO_NUM_2) continue;  // 保留唤醒引脚
        gpio_set_direction(i, GPIO_MODE_INPUT);
        gpio_set_pull_mode(i, GPIO_FLOATING);
    }
    
    esp_deep_sleep_start();
}

// 方案2：根据外部电路配置上下拉
void enter_deep_sleep(void) {
    // ✅ 有外部上拉的引脚配置为输入+上拉（避免悬空）
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_4, GPIO_PULLUP_ONLY);
    
    // ✅ 有外部下拉的引脚配置为下拉
    gpio_set_direction(GPIO_NUM_5, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_5, GPIO_PULLDOWN_ONLY);
    
    esp_deep_sleep_start();
}

// STM32 版本
void enter_standby(void) {
    // ✅ 所有 GPIO 配置为模拟输入（最低功耗）
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_All;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    
    HAL_PWR_EnterSTANDBYMode();
}
```

**💥 后果**：
- 睡眠电流从 10μA 飙升到 500μA
- 电池寿命缩短 50 倍（预期 1 年变成 1 周）

---

### 4. 【高危】未关闭的外设时钟（ADC/SPI/I2C/UART）
**问题原理**：
- 外设时钟未关闭，持续耗电
- 某些外设（ADC）功耗高达 mA 级别

**❌ 错误示例**：
```c
void enter_light_sleep(void) {
    // 🔴 ADC/SPI/I2C 时钟仍然开启
    esp_light_sleep_start();
}

// STM32 版本
void enter_sleep(void) {
    // 🔴 未关闭外设时钟
    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
}
```

**✅ 正确示例**：
```c
// ESP32 版本
void enter_light_sleep(void) {
    // ✅ 关闭不需要的外设模块
    periph_module_disable(PERIPH_I2C0_MODULE);
    periph_module_disable(PERIPH_SPI2_MODULE);
    periph_module_disable(PERIPH_UART1_MODULE);
    periph_module_disable(PERIPH_SARADC_MODULE);
    
    esp_light_sleep_start();
    
    // ✅ 唤醒后重新启用
    periph_module_enable(PERIPH_I2C0_MODULE);
    periph_module_enable(PERIPH_SPI2_MODULE);
}

// STM32 版本
void enter_sleep(void) {
    // ✅ 关闭所有非必要外设时钟
    __HAL_RCC_GPIOA_CLK_DISABLE();
    __HAL_RCC_GPIOB_CLK_DISABLE();
    __HAL_RCC_ADC1_CLK_DISABLE();
    __HAL_RCC_SPI1_CLK_DISABLE();
    __HAL_RCC_I2C1_CLK_DISABLE();
    __HAL_RCC_USART1_CLK_DISABLE();
    
    HAL_PWR_EnterSLEEPMode(PWR_LOWPOWERREGULATOR_ON, PWR_SLEEPENTRY_WFI);
    
    // ✅ 唤醒后重新启用
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
}
```

**💥 后果**：
- Light Sleep 电流从 0.8mA 飙升到 5mA
- 睡眠模式功耗优化失效

---

### 5. 【高危】RTC 内存越界、数据丢失
**问题原理**：
- ESP32 RTC 内存只有 8KB（RTC_SLOW_MEM）
- 越界写入会破坏其他数据或崩溃

**❌ 错误示例**：
```c
// 🔴 RTC 内存越界
RTC_DATA_ATTR uint8_t huge_buffer[10240];  // 🔴 10KB 超出 8KB 限制！

void enter_deep_sleep(void) {
    memset(huge_buffer, 0, 10240);         // 🔴 越界访问
    esp_deep_sleep_start();
}
```

**✅ 正确示例**：
```c
// 方案1：限制 RTC 内存使用量
RTC_DATA_ATTR uint8_t buffer[1024];        // ✅ 1KB，远小于 8KB

// 方案2：大数据使用 NVS 存储
void enter_deep_sleep(void) {
    nvs_handle_t nvs_h;
    nvs_open("storage", NVS_READWRITE, &nvs_h);
    nvs_set_blob(nvs_h, "data", large_buffer, 10240);  // ✅ NVS 无大小限制
    nvs_commit(nvs_h);
    nvs_close(nvs_h);
    
    esp_deep_sleep_start();
}

// 方案3：检查 RTC 内存使用量
void check_rtc_memory(void) {
    extern uint32_t _rtc_data_start, _rtc_data_end;
    size_t used = &_rtc_data_end - &_rtc_data_start;
    ESP_LOGI(TAG, "RTC memory used: %d bytes", used);
    if (used > 7168) {  // ✅ 7KB 警告阈值（总共 8KB）
        ESP_LOGW(TAG, "RTC memory usage high!");
    }
}
```

**💥 后果**：
- 系统崩溃、启动失败
- RTC 数据损坏

---

### 6. 【高危】WiFi/BLE 未彻底关闭
**问题原理**：
- WiFi/BLE 未完全断开，底层协议栈仍在运行
- 睡眠电流从 10μA 飙升到 20mA

**❌ 错误示例**：
```c
void enter_light_sleep(void) {
    // 🔴 只是停止扫描，WiFi 驱动仍在运行
    esp_wifi_stop();
    esp_light_sleep_start();
}
```

**✅ 正确示例**：
```c
void enter_deep_sleep(void) {
    // ✅ 完全关闭 WiFi
    esp_wifi_stop();
    esp_wifi_deinit();           // ✅ 释放 WiFi 驱动
    
    // ✅ 完全关闭 BLE
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    
    esp_deep_sleep_start();
}

void wakeup_handler(void) {
    // ✅ 唤醒后重新初始化
    esp_bt_controller_init(&bt_cfg);
    esp_wifi_init(&wifi_cfg);
}
```

**💥 后果**：
- 睡眠电流高达 20mA（预期 10μA）
- 电池快速耗尽

---

### 7. 【中危】唤醒源优先级冲突
**问题原理**：
- 配置了多个唤醒源（GPIO、定时器、UART）
- 唤醒后无法判断是哪个源触发

**❌ 错误示例**：
```c
void configure_wakeup(void) {
    // 🔴 同时启用多个唤醒源，但未记录优先级
    esp_sleep_enable_timer_wakeup(60 * 1000000);  // 60 秒定时唤醒
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 0); // GPIO 下降沿唤醒
    esp_sleep_enable_uart_wakeup(0);              // UART 唤醒
}

void app_main(void) {
    configure_wakeup();
    esp_deep_sleep_start();
    
    // 🔴 唤醒后不知道是哪个源触发
    printf("Wake up!\n");  // 是定时器？GPIO？还是 UART？
}
```

**✅ 正确示例**：
```c
void app_main(void) {
    configure_wakeup();
    esp_deep_sleep_start();
    
    // ✅ 根据唤醒原因执行不同逻辑
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    
    switch (wakeup_reason) {
        case ESP_SLEEP_WAKEUP_TIMER:
            ESP_LOGI(TAG, "Timer wakeup");
            periodic_task();     // 定时任务
            break;
        case ESP_SLEEP_WAKEUP_EXT0:
            ESP_LOGI(TAG, "GPIO wakeup");
            handle_button();     // 按键事件
            break;
        case ESP_SLEEP_WAKEUP_UART:
            ESP_LOGI(TAG, "UART wakeup");
            handle_command();    // 串口命令
            break;
        default:
            ESP_LOGI(TAG, "Cold boot");
            break;
    }
}
```

**💥 后果**：
- 唤醒后执行错误的逻辑
- 无法区分唤醒原因

---

### 8. 【中危】定时器未停止持续耗电
**问题原理**：
- 硬件定时器未停止，睡眠期间持续中断
- 导致无法进入深度睡眠

**❌ 错误示例**：
```c
void enter_sleep(void) {
    // 🔴 定时器仍在运行，每 1ms 触发中断
    HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFI);
}
```

**✅ 正确示例**：
```c
void enter_sleep(void) {
    HAL_TIM_Base_Stop_IT(&htim2);  // ✅ 停止定时器中断
    HAL_PWR_EnterSLEEPMode(PWR_LOWPOWERREGULATOR_ON, PWR_SLEEPENTRY_WFI);
}

void wakeup_handler(void) {
    HAL_TIM_Base_Start_IT(&htim2);  // ✅ 唤醒后重启定时器
}
```

**💥 后果**：
- 无法进入深度睡眠
- 频繁中断导致功耗高

---

### 9. 【中危】睡眠时长计算错误
**问题原理**：
- 睡眠时长单位错误（秒 vs 微秒）
- 睡眠时间过短或过长

**❌ 错误示例**：
```c
void enter_deep_sleep(void) {
    // 🔴 单位错误：60 微秒而非 60 秒
    esp_sleep_enable_timer_wakeup(60);
    esp_deep_sleep_start();  // 🔴 60μs 后就唤醒了！
}
```

**✅ 正确示例**：
```c
void enter_deep_sleep(void) {
    uint64_t sleep_us = 60 * 1000000ULL;  // ✅ 60 秒 = 60,000,000 微秒
    esp_sleep_enable_timer_wakeup(sleep_us);
    esp_deep_sleep_start();
}

// 或使用宏
#define SLEEP_TIME_SEC(sec) ((uint64_t)(sec) * 1000000ULL)
esp_sleep_enable_timer_wakeup(SLEEP_TIME_SEC(60));
```

**💥 后果**：
- 睡眠时间错误
- 频繁唤醒浪费电量

---

### 10. 【低危】功耗测试点未预留
**问题原理**：
- PCB 未预留电流测试点
- 无法测量实际功耗

**✅ 正确设计**：
```
电池 ──┬── 测试点 ── MCU
       │
       └── 测试点 ── 外设模块

// 预留跳线帽，方便串联电流表测量
```

---

### 11. 【低危】电源管理配置未优化
**问题原理**：
- 未配置动态调频调压（DVFS）
- CPU 长期运行在高频

**✅ 正确示例**：
```c
// ESP32 自动功耗管理
void init_power_management(void) {
    esp_pm_config_esp32_t pm_config = {
        .max_freq_mhz = 240,         // 高负载 240MHz
        .min_freq_mhz = 80,          // 空闲时 80MHz
        .light_sleep_enable = true   // ✅ 自动进入 Light Sleep
    };
    esp_pm_configure(&pm_config);
}
```

---

## 强制规范

### 睡眠前必做
1. 关键数据保存到 RTC 内存或 NVS
2. 关闭所有非必要外设时钟
3. GPIO 配置为高阻态或合理的上下拉
4. WiFi/BLE 完全关闭
5. 停止所有定时器

### 唤醒后必做
1. 检查唤醒原因（`esp_sleep_get_wakeup_cause`）
2. 重新初始化被关闭的外设
3. 恢复 GPIO 配置
4. 重新连接 WiFi（如需要）

### 功耗优化检查清单
```c
// 睡眠电流目标值（仅供参考）
// - Deep Sleep: < 10μA
// - Light Sleep: < 1mA
// - Modem Sleep (WiFi): 15-20mA（周期性醒来接收 Beacon）
// - Active (240MHz): 80-160mA
```

---

## 输出格式
【风险等级】致命/高/中/低
【位置】文件名:行号:函数名
【问题类型】数据丢失/GPIO漏电/外设未关/唤醒源冲突
【功耗影响】预期 XXX μA，实际可能 XXX mA
【错误代码片段】
【问题原理】为什么会导致功耗升高/数据丢失
【修复方案 + 正确示例】
【功耗优化建议】
