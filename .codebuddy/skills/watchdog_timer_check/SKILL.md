---
name: watchdog_timer_check
description: 审查看门狗配置、定时器溢出、硬件定时器冲突、RTOS任务超时风险。
auto_invoke: true
allowed-tools: [ReadFile, GrepSearch]
---
# 技能：看门狗与定时器安全审查（专家级）

## 触发条件
识别到以下场景自动执行：
- 看门狗初始化（`IWDG_Init`、`esp_task_wdt_init`、`WWDG_Init`）
- 看门狗喂狗操作（`HAL_IWDG_Refresh`、`esp_task_wdt_reset`）
- 硬件定时器配置（`TIM_Init`、`timer_init`、`GPTIMER`）
- 软件定时器（`xTimerCreate`、`osTimerNew`）
- 长延时函数（`vTaskDelay`、`HAL_Delay`、`usleep`）
- 阻塞等待（`while(1)`、无限循环、死等标志位）

---

## 完整检查清单（12 项致命风险）

### 1. 【致命】看门狗超时时间过短导致误复位
**问题原理**：
- 看门狗超时设置为 1 秒，但某个任务需要执行 2 秒的 Flash 擦除操作
- 操作未完成就触发看门狗复位，导致**频繁重启**
- 系统无法正常工作（启动 → 运行 → 看门狗复位 → 再启动 → 死循环）

**❌ 错误示例**：
```c
// 🔴 看门狗超时 1 秒，但 Flash 擦除需要 2 秒
void init_watchdog(void) {
    esp_task_wdt_init(1, true);  // 1 秒超时
}

void ota_upgrade(void) {
    esp_task_wdt_add(NULL);      // 当前任务加入看门狗监控
    
    // Flash 擦除需要 2 秒
    for (int i = 0; i < 16; i++) {
        flash_erase_sector(i);   // 每个扇区 120ms，总共 1.92s
    }
    // 🔴 看门狗 1 秒就超时复位了！
    
    esp_task_wdt_reset();
}

// 🔴 STM32 版本
void MX_IWDG_Init(void) {
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 1000;    // 🔴 超时 1 秒
    HAL_IWDG_Init(&hiwdg);
}

void update_firmware(void) {
    HAL_FLASH_Unlock();
    for (int i = 0; i < 10; i++) {
        FLASH_Erase_Sector(i, VOLTAGE_RANGE_3);  // 🔴 擦除需要 2 秒
    }
    HAL_FLASH_Lock();
    // 🔴 还没喂狗就复位了！
}
```

**✅ 正确示例**：
```c
// 方案1：增加看门狗超时时间
void init_watchdog(void) {
    esp_task_wdt_init(5, true);  // ✅ 超时 5 秒（最长操作 2s * 2 倍余量）
}

void ota_upgrade(void) {
    esp_task_wdt_add(NULL);
    
    for (int i = 0; i < 16; i++) {
        flash_erase_sector(i);
    }
    esp_task_wdt_reset();        // ✅ 操作完成后喂狗
}

// 方案2：长操作中途喂狗
void ota_upgrade(void) {
    esp_task_wdt_add(NULL);
    
    for (int i = 0; i < 16; i++) {
        flash_erase_sector(i);
        if (i % 4 == 0) {
            esp_task_wdt_reset();  // ✅ 每擦除 4 个扇区喂一次狗
        }
    }
}

// 方案3：临时禁用看门狗（STM32）
void update_firmware(void) {
    // ✅ 擦除前删除看门狗任务监控
    esp_task_wdt_delete(NULL);
    
    HAL_FLASH_Unlock();
    for (int i = 0; i < 10; i++) {
        FLASH_Erase_Sector(i, VOLTAGE_RANGE_3);
    }
    HAL_FLASH_Lock();
    
    // ✅ 擦除后重新加入监控
    esp_task_wdt_add(NULL);
}
```

**💥 后果**：
- 系统频繁重启，无法完成长操作
- OTA 升级失败，设备变砖

---

### 2. 【致命】多任务环境单点喂狗失效
**问题原理**：
- 多任务系统中，某个任务卡死但其他任务正常，单点喂狗无法检测
- 示例：WiFi 任务死锁，但主任务继续喂狗，看门狗无法复位系统

**❌ 错误示例**：
```c
// 🔴 只有主任务喂狗，其他任务卡死无法检测
void main_task(void *param) {
    while (1) {
        esp_task_wdt_reset();    // 🔴 单点喂狗
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void wifi_task(void *param) {
    while (1) {
        xSemaphoreTake(mutex, portMAX_DELAY);  // 🔴 死锁卡在这里
        wifi_process();
        xSemaphoreGive(mutex);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

**✅ 正确示例**：
```c
// 方案1：使用任务看门狗（Task Watchdog）
void init_watchdog(void) {
    esp_task_wdt_init(5, true);  // 5 秒超时
}

void main_task(void *param) {
    esp_task_wdt_add(NULL);      // ✅ 主任务加入监控
    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void wifi_task(void *param) {
    esp_task_wdt_add(NULL);      // ✅ WiFi 任务也加入监控
    while (1) {
        if (xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            wifi_process();
            xSemaphoreGive(mutex);
            esp_task_wdt_reset();  // ✅ 各自喂狗
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// 方案2：关键任务必须周期性报活
static uint32_t g_task1_heartbeat = 0;
static uint32_t g_task2_heartbeat = 0;

void task1(void *param) {
    while (1) {
        g_task1_heartbeat = xTaskGetTickCount();  // ✅ 报活
        do_work();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void watchdog_monitor_task(void *param) {
    while (1) {
        uint32_t now = xTaskGetTickCount();
        // ✅ 检测所有任务是否报活
        if ((now - g_task1_heartbeat) > pdMS_TO_TICKS(3000)) {
            log_fatal("Task1 timeout!");
            esp_restart();
        }
        if ((now - g_task2_heartbeat) > pdMS_TO_TICKS(3000)) {
            log_fatal("Task2 timeout!");
            esp_restart();
        }
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

**💥 后果**：
- 关键任务卡死无法检测
- 系统假死（部分功能正常，部分功能失效）

---

### 3. 【致命】阻塞函数未喂狗导致复位
**问题原理**：
- 某些操作会阻塞很长时间（等待 I2C 响应、等待网络连接）
- 阻塞期间未喂狗，看门狗超时复位

**❌ 错误示例**：
```c
void network_connect(void) {
    esp_task_wdt_add(NULL);
    
    // 🔴 WiFi 连接可能需要 10 秒，但看门狗只有 5 秒
    wifi_connect("SSID", "password");  // 阻塞等待
    
    esp_task_wdt_reset();       // 🔴 连接未成功就复位了
}

// 🔴 等待 I2C 响应
void read_sensor(void) {
    while (HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY) {
        // 🔴 死等，期间不喂狗
    }
    HAL_I2C_Master_Receive(&hi2c1, addr, buf, len, 1000);
}
```

**✅ 正确示例**：
```c
// 方案1：阻塞前临时禁用看门狗
void network_connect(void) {
    esp_task_wdt_delete(NULL);  // ✅ 移除监控
    
    wifi_connect("SSID", "password");
    
    esp_task_wdt_add(NULL);     // ✅ 连接后重新加入
    esp_task_wdt_reset();
}

// 方案2：超时分段喂狗
void network_connect(void) {
    esp_task_wdt_add(NULL);
    
    for (int retry = 0; retry < 10; retry++) {
        if (wifi_try_connect(1000) == ESP_OK) {  // ✅ 每次尝试 1 秒
            break;
        }
        esp_task_wdt_reset();    // ✅ 每次重试都喂狗
    }
}

// 方案3：使用非阻塞 API
void read_sensor(void) {
    uint32_t start = HAL_GetTick();
    while (HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY) {
        if ((HAL_GetTick() - start) > 2000) {
            log_error("I2C timeout");
            return;              // ✅ 超时退出
        }
        HAL_IWDG_Refresh(&hiwdg);  // ✅ 等待期间喂狗
        HAL_Delay(10);
    }
}
```

**💥 后果**：
- 正常操作被看门狗误杀
- 连接操作永远无法完成

---

### 4. 【致命】调试模式下看门狗未暂停
**问题原理**：
- 单步调试时程序暂停，但看门狗继续计时
- 调试到一半就复位，无法排查问题

**❌ 错误示例**：
```c
// 🔴 调试时看门狗仍然运行
void MX_IWDG_Init(void) {
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 4095;
    HAL_IWDG_Init(&hiwdg);       // 🔴 无论调试与否都启用
}
```

**✅ 正确示例**：
```c
// 方案1：调试模式下禁用看门狗（STM32）
void MX_IWDG_Init(void) {
    // ✅ 配置调试时冻结看门狗
    __HAL_DBGMCU_FREEZE_IWDG();
    
    hiwdg.Instance = IWDG;
    hiwdg.Init.Prescaler = IWDG_PRESCALER_32;
    hiwdg.Init.Reload = 4095;
    HAL_IWDG_Init(&hiwdg);
}

// 方案2：条件编译（ESP32）
void init_watchdog(void) {
#ifndef CONFIG_DEBUG_MODE
    esp_task_wdt_init(5, true);  // ✅ 生产环境才启用
#endif
}

// 方案3：调试端口检测
void init_watchdog(void) {
    if (!is_debugger_attached()) {
        esp_task_wdt_init(5, true);  // ✅ 只在无调试器时启用
    }
}
```

**💥 后果**：
- 无法单步调试
- 调试过程中频繁复位

---

### 5. 【高危】定时器溢出未处理
**问题原理**：
- 32 位定时器以 1MHz 计数，72 分钟后溢出
- 16 位定时器以 1MHz 计数，65.5ms 后溢出
- 溢出后计时逻辑错误（时间戳回绕）

**❌ 错误示例**：
```c
// 🔴 定时器回绕导致超时判断失效
uint32_t start_time = HAL_GetTick();  // 假设当前 0xFFFFFF00
// ... 等待 500ms ...
uint32_t elapsed = HAL_GetTick() - start_time;  // 🔴 0x000001F4 - 0xFFFFFF00 = 负数！

if (elapsed > 1000) {
    log_error("Timeout");       // 🔴 永远不会触发
}
```

**✅ 正确示例**：
```c
// 方案1：使用带符号差值比较
uint32_t start_time = HAL_GetTick();
// ... 等待 500ms ...
int32_t elapsed = (int32_t)(HAL_GetTick() - start_time);  // ✅ 带符号计算

if (elapsed > 1000) {
    log_error("Timeout");
}

// 方案2：使用库函数处理回绕
uint32_t start_time = xTaskGetTickCount();
// ... 等待 500ms ...
if ((xTaskGetTickCount() - start_time) > pdMS_TO_TICKS(1000)) {
    log_error("Timeout");       // ✅ FreeRTOS 自动处理回绕
}

// 方案3：64 位时间戳（ESP32）
int64_t start_us = esp_timer_get_time();  // 微秒级时间戳
// ... 等待 ...
int64_t elapsed_us = esp_timer_get_time() - start_us;
if (elapsed_us > 1000000) {     // ✅ 64 位不会溢出（584942 年）
    log_error("Timeout");
}
```

**💥 后果**：
- 超时判断失效
- 时间戳计算错误

---

### 6. 【高危】硬件定时器冲突（TIM 重复使用）
**问题原理**：
- STM32 有多个硬件定时器（TIM1-TIM14），但数量有限
- PWM、输入捕获、定时中断可能重复配置同一个定时器

**❌ 错误示例**：
```c
// 🔴 模块 A 使用 TIM2 做 PWM
void pwm_init(void) {
    __HAL_RCC_TIM2_CLK_ENABLE();
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 72 - 1;
    HAL_TIM_PWM_Init(&htim2);
}

// 🔴 模块 B 使用 TIM2 做定时中断
void timer_init(void) {
    __HAL_RCC_TIM2_CLK_ENABLE();
    htim2.Instance = TIM2;       // 🔴 重复配置 TIM2！
    htim2.Init.Prescaler = 7200 - 1;
    HAL_TIM_Base_Init(&htim2);
}
```

**✅ 正确示例**：
```c
// 方案1：统一管理定时器分配
// timer_alloc.h
typedef enum {
    TIMER_PWM_CH1 = TIM2,
    TIMER_PWM_CH2 = TIM3,
    TIMER_CAPTURE = TIM4,
    TIMER_PERIOD  = TIM5,
} timer_usage_t;

// 方案2：使用不同定时器
void pwm_init(void) {
    htim2.Instance = TIM2;       // ✅ PWM 使用 TIM2
    HAL_TIM_PWM_Init(&htim2);
}

void timer_init(void) {
    htim3.Instance = TIM3;       // ✅ 定时中断使用 TIM3
    HAL_TIM_Base_Init(&htim3);
}

// 方案3：软件定时器替代硬件定时器
TimerHandle_t timer = xTimerCreate("timer", pdMS_TO_TICKS(100), 
                                    pdTRUE, NULL, timer_callback);
xTimerStart(timer, 0);
```

**💥 后果**：
- 定时器配置被覆盖
- PWM 输出异常、定时中断丢失

---

### 7. 【高危】定时器中断优先级配置错误
**问题原理**：
- 定时器中断优先级高于 RTOS 临界区保护
- 导致死锁或数据竞争

**❌ 错误示例**：
```c
// 🔴 定时器中断优先级 = 0（最高）
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* htim) {
    HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);  // 🔴 优先级 0 高于 FreeRTOS
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
}

void TIM2_IRQHandler(void) {
    xQueueSendFromISR(queue, &data, NULL);  // 🔴 在高优先级中断调用 RTOS API
}
```

**✅ 正确示例**：
```c
// ✅ 定时器优先级必须 ≥ configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY
void HAL_TIM_Base_MspInit(TIM_HandleTypeDef* htim) {
    // FreeRTOS 要求：调用 API 的中断优先级必须 ≥ 5
    HAL_NVIC_SetPriority(TIM2_IRQn, 5, 0);  // ✅ 优先级 5
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
}
```

**💥 后果**：
- RTOS 调度器崩溃
- 死锁、数据损坏

---

### 8. 【中危】软件定时器回调函数过长
**问题原理**：
- FreeRTOS 软件定时器回调在定时器任务中执行
- 回调函数过长会阻塞其他定时器

**❌ 错误示例**：
```c
void timer_callback(TimerHandle_t xTimer) {
    // 🔴 回调函数中执行耗时操作
    for (int i = 0; i < 100; i++) {
        process_heavy_task(i);   // 每次 10ms，总共 1 秒
    }
    log_info("Timer expired");
}
```

**✅ 正确示例**：
```c
static QueueHandle_t timer_queue;

void timer_callback(TimerHandle_t xTimer) {
    uint8_t event = TIMER_EVENT;
    xQueueSend(timer_queue, &event, 0);  // ✅ 只发送事件通知
}

void worker_task(void *param) {
    uint8_t event;
    while (1) {
        if (xQueueReceive(timer_queue, &event, portMAX_DELAY) == pdTRUE) {
            // ✅ 在独立任务中处理耗时操作
            for (int i = 0; i < 100; i++) {
                process_heavy_task(i);
            }
        }
    }
}
```

**💥 后果**：
- 其他定时器延迟触发
- 定时器任务栈溢出

---

### 9. 【中危】看门狗未启用导致死机无法恢复
**问题原理**：
- 生产环境未启用看门狗
- 系统死锁、死循环后无法自动恢复

**❌ 错误示例**：
```c
void init_system(void) {
    // 🔴 调试方便，生产环境也没启用看门狗
#ifdef DEBUG_MODE
    // 看门狗已禁用
#endif
}
```

**✅ 正确示例**：
```c
void init_system(void) {
#ifdef PRODUCTION_MODE
    esp_task_wdt_init(5, true);  // ✅ 生产环境强制启用
#endif
}

// ✅ 通过硬件熔丝位强制启用（STM32）
// Option Bytes: IWDG_SW = 0（硬件看门狗，无法软件禁用）
```

**💥 后果**：
- 系统死机后需要人工重启
- 设备部署在远程无法维护

---

### 10. 【中危】定时精度不足（晶振偏差）
**问题原理**：
- 内部 RC 振荡器精度 ±2%，温度影响更大
- 长时间累积误差明显

**❌ 错误示例**：
```c
// 🔴 使用内部 RC 振荡器做精确定时
void rtc_init(void) {
    __HAL_RCC_LSI_ENABLE();      // 🔴 内部低速振荡器（40kHz ±5%）
    // RTC 时钟源 = LSI
}
```

**✅ 正确示例**：
```c
// ✅ 使用外部晶振
void rtc_init(void) {
    __HAL_RCC_LSE_CONFIG(RCC_LSE_ON);  // ✅ 外部 32.768kHz 晶振（±20ppm）
    while(__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == RESET);
    // RTC 时钟源 = LSE
}

// ✅ 软件校准补偿
void calibrate_timer(void) {
    // 根据实测偏差调整预分频器
    htim.Init.Prescaler = 72 * (1.0 + measured_error);
}
```

**💥 后果**：
- RTC 时间每天误差几秒
- 定时任务触发时间不准

---

### 11. 【低危】看门狗喂狗位置不当
**问题原理**：
- 循环开头喂狗，循环体卡死无法检测

**❌ 错误示例**：
```c
void main_loop(void) {
    while (1) {
        esp_task_wdt_reset();    // 🔴 循环开头喂狗
        
        process_step1();         // 可能卡死
        process_step2();         // 可能卡死
        process_step3();         // 可能卡死
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

**✅ 正确示例**：
```c
void main_loop(void) {
    while (1) {
        process_step1();
        process_step2();
        process_step3();
        
        esp_task_wdt_reset();    // ✅ 循环末尾喂狗（所有步骤都完成）
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

---

### 12. 【低危】定时器未停止持续中断
**问题原理**：
- 不需要定时器时未停止，持续触发中断
- 浪费 CPU 资源

**❌ 错误示例**：
```c
void enter_sleep(void) {
    // 🔴 进入睡眠前未停止定时器
    esp_light_sleep_start();
}
```

**✅ 正确示例**：
```c
void enter_sleep(void) {
    HAL_TIM_Base_Stop_IT(&htim2);  // ✅ 停止定时器中断
    esp_light_sleep_start();
}
```

---

## 强制规范

### 看门狗配置原则
1. **超时时间 ≥ 最长操作时间 × 2**
2. **多任务系统必须使用任务看门狗（TWDT）**
3. **生产环境必须启用看门狗**
4. **调试模式下看门狗必须暂停**
5. **阻塞超过 1 秒的函数必须中途喂狗或临时禁用看门狗**

### 定时器使用原则
1. **硬件定时器统一分配管理**
2. **定时器中断优先级必须 ≥ `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`**
3. **软件定时器回调必须极简（< 10ms）**
4. **定时器溢出必须正确处理回绕**
5. **精确定时必须使用外部晶振**

---

## 输出格式
【风险等级】致命/高/中/低
【位置】文件名:行号:函数名
【问题类型】看门狗超时/定时器冲突/溢出未处理/优先级错误
【错误代码片段】
【问题原理】为什么会导致误复位/冲突/溢出
【修复方案 + 正确示例】
【配置建议】（看门狗超时时间、定时器分配）
