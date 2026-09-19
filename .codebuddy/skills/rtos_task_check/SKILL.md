---
name: rtos_task_check
description: 审查 FreeRTOS 任务优先级、栈大小、死锁、优先级反转、任务饿死、信号量误用等问题。
auto_invoke: true
allowed-tools: [ReadFile, GrepSearch]
---
# 技能：RTOS 任务与同步机制审查

## ⚠️ 核心原则（对应 AI 评审第 4 层：RTOS 任务与同步）
- **死锁必须预防**：互斥锁获取顺序全局一致、设置超时
- **优先级反转必须解决**：使用优先级继承互斥锁
- **任务栈必须充足**：≥ 实际使用量 * 1.5 倍
- **信号量必须成对**：Take/Give 必须配对，异常分支也要释放
- **临界区必须极短**：< 50us，禁止在临界区中调用阻塞函数

## 触发条件
- xTaskCreate/xTaskCreatePinnedToCore/xTaskCreateStatic
- 互斥锁/信号量/队列/事件组操作
- 任务优先级配置
- 临界区保护（taskENTER_CRITICAL / portENTER_CRITICAL）
- 任务通知（xTaskNotify / ulTaskNotifyTake）

---

## 🔍 完整检查清单

### 🔴 第 4 层：RTOS 任务与同步（死机级）

#### 1. **死锁：互斥锁获取顺序不一致（致命）**
- **问题**：不同任务以不同顺序获取多个互斥锁？
- **后果**：系统死锁、看门狗复位

**错误示例**：
```c
// ❌ 致命：任务 A 和任务 B 获取锁的顺序不一致
// 任务 A
void task_a(void *arg) {
    xSemaphoreTake(mutex_uart, portMAX_DELAY);  // 先 UART
    xSemaphoreTake(mutex_flash, portMAX_DELAY); // 后 Flash
    // 操作...
    xSemaphoreGive(mutex_flash);
    xSemaphoreGive(mutex_uart);
}

// 任务 B
void task_b(void *arg) {
    xSemaphoreTake(mutex_flash, portMAX_DELAY); // 先 Flash
    xSemaphoreTake(mutex_uart, portMAX_DELAY);  // 后 UART （顺序反了！）
    // 操作...
    xSemaphoreGive(mutex_uart);
    xSemaphoreGive(mutex_flash);
}
// 死锁场景：A 持有 UART 等 Flash，B 持有 Flash 等 UART
```

**正确示例**：
```c
// ✅ 正确：全局统一锁顺序 - 按资源 ID 排序（如 UART < Flash）
// 任务 A
void task_a(void *arg) {
    xSemaphoreTake(mutex_uart, portMAX_DELAY);  // 先 UART
    xSemaphoreTake(mutex_flash, portMAX_DELAY); // 后 Flash
    // ...
    xSemaphoreGive(mutex_flash);
    xSemaphoreGive(mutex_uart);
}

// 任务 B - 必须保持相同顺序
void task_b(void *arg) {
    xSemaphoreTake(mutex_uart, portMAX_DELAY);  // 先 UART（顺序一致）
    xSemaphoreTake(mutex_flash, portMAX_DELAY); // 后 Flash
    // ...
    xSemaphoreGive(mutex_flash);
    xSemaphoreGive(mutex_uart);
}

// ✅ 更好：使用超时避免永久阻塞
if (xSemaphoreTake(mutex_uart, pdMS_TO_TICKS(1000)) == pdTRUE) {
    if (xSemaphoreTake(mutex_flash, pdMS_TO_TICKS(1000)) == pdTRUE) {
        // 操作...
        xSemaphoreGive(mutex_flash);
    } else {
        ESP_LOGE(TAG, "获取 Flash 锁超时，避免死锁");
    }
    xSemaphoreGive(mutex_uart);
}
```

---

#### 2. **优先级反转（致命）**
- **问题**：低优先级任务持有互斥锁，高优先级任务等待？
- **后果**：实时性失效、系统响应慢

**错误示例**：
```c
// ❌ 致命：使用二值信号量代替互斥锁，导致优先级反转
SemaphoreHandle_t sem = xSemaphoreCreateBinary();  // 错误！

// 低优先级任务 (优先级 1)
void low_task(void *arg) {
    xSemaphoreTake(sem, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(100));  // 持有锁很久
    xSemaphoreGive(sem);
}

// 高优先级任务 (优先级 5) 被低优先级任务阻塞！
void high_task(void *arg) {
    xSemaphoreTake(sem, portMAX_DELAY);  // 等待低优先级任务
    // 紧急操作...
    xSemaphoreGive(sem);
}
```

**正确示例**：
```c
// ✅ 正确：使用互斥锁（支持优先级继承）
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

// 低优先级任务持有锁时，临时继承高优先级任务的优先级
void low_task(void *arg) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(100));  // 此时优先级已提升
    xSemaphoreGive(mutex);
}

void high_task(void *arg) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    // 紧急操作...
    xSemaphoreGive(mutex);
}
```

---

#### 3. **信号量泄漏：Take 后未 Give（致命）**
- **问题**：获取信号量后，异常分支未释放？
- **后果**：其他任务永久阻塞、系统死锁

**错误示例**：
```c
// ❌ 致命：异常分支忘记释放信号量
void process_data(void) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    if (data == NULL) {
        ESP_LOGE(TAG, "数据为空");
        return;  // 忘记 Give！导致信号量泄漏
    }
    
    if (validate(data) != ESP_OK) {
        return;  // 又忘记了！
    }
    
    // 处理数据...
    xSemaphoreGive(mutex);
}
```

**正确示例**：
```c
// ✅ 正确：使用 goto 统一释放资源
esp_err_t process_data(void) {
    esp_err_t ret = ESP_FAIL;
    
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (data == NULL) {
        ESP_LOGE(TAG, "数据为空");
        ret = ESP_ERR_INVALID_ARG;
        goto cleanup;
    }
    
    if (validate(data) != ESP_OK) {
        ret = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    
    // 处理数据...
    ret = ESP_OK;
    
cleanup:
    xSemaphoreGive(mutex);  // 统一释放
    return ret;
}

// ✅ 更好：使用 RAII 风格的包装宏（C++ 或自定义宏）
#define MUTEX_LOCK_GUARD(mutex) \
    for (int _lock = (xSemaphoreTake(mutex, portMAX_DELAY), 1); \
         _lock; \
         _lock = (xSemaphoreGive(mutex), 0))

void process_data(void) {
    MUTEX_LOCK_GUARD(mutex) {
        if (data == NULL) return;  // 自动释放
        if (validate(data) != ESP_OK) return;  // 自动释放
        // 处理数据...
    } // 自动释放
}
```

---

#### 4. **任务栈过小导致栈溢出（致命）**
- **问题**：任务栈大小是否根据实际使用量配置？
- **检查方法**：使用 `uxTaskGetStackHighWaterMark()` 监控栈余量

**错误示例**：
```c
// ❌ 致命：栈大小不足
#define TASK_STACK_SIZE 1024  // 仅 1KB

void complex_task(void *arg) {
    char buffer[2048];  // 局部变量 2KB，超出栈大小！
    uint8_t temp[1024];
    // 调用深层函数...
    process(buffer, temp);  // 栈溢出！
}

xTaskCreate(complex_task, "complex", TASK_STACK_SIZE, NULL, 5, NULL);
```

**正确示例**：
```c
// ✅ 正确：充足的栈大小 + 运行时监控
#define TASK_STACK_SIZE 4096  // 4KB

// 将大缓冲区移到全局或 heap
static uint8_t buffer[2048];
static uint8_t temp[1024];

void complex_task(void *arg) {
    while (1) {
        process(buffer, temp);
        
        // 监控栈余量（开发阶段）
        UBaseType_t stack_left = uxTaskGetStackHighWaterMark(NULL);
        if (stack_left < 512) {
            ESP_LOGW(TAG, "栈余量不足: %u bytes", stack_left * sizeof(StackType_t));
        }
        
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

xTaskCreate(complex_task, "complex", TASK_STACK_SIZE, NULL, 5, NULL);

// ✅ 栈大小规则：实际使用量 * 1.5 倍（留 50% 余量）
// 例如：实际使用 2KB，配置 3KB
```

---

#### 5. **临界区持续时间过长（高危）**
- **问题**：临界区内是否有延时、循环、阻塞函数？
- **后果**：其他任务饿死、中断响应慢、看门狗复位

**错误示例**：
```c
// ❌ 高危：临界区内有延时和循环
void update_config(void) {
    taskENTER_CRITICAL(&spinlock);
    
    for (int i = 0; i < 100; i++) {
        config[i] = read_sensor();  // 读传感器很慢！
        vTaskDelay(pdMS_TO_TICKS(10));  // 延时 10ms！
    }
    
    taskEXIT_CRITICAL(&spinlock);
    // 临界区持续 1 秒以上，其他任务全部卡死
}
```

**正确示例**：
```c
// ✅ 正确：临界区只保护必要的数据访问
void update_config(void) {
    uint8_t temp[100];
    
    // 耗时操作放在临界区外
    for (int i = 0; i < 100; i++) {
        temp[i] = read_sensor();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // 临界区只保护数据拷贝（< 1us）
    taskENTER_CRITICAL(&spinlock);
    memcpy(config, temp, sizeof(temp));
    taskEXIT_CRITICAL(&spinlock);
}

// ✅ 规则：临界区持续时间 < 50us
```

---

#### 6. **任务优先级配置不合理（高危）**
- **问题**：高频任务、紧急任务是否配置了更高优先级？
- **问题**：同优先级任务过多是否导致饿死？

**错误示例**：
```c
// ❌ 错误：紧急任务优先级低于普通任务
xTaskCreate(wifi_task, "wifi", 4096, NULL, 5, NULL);      // WiFi 优先级 5
xTaskCreate(led_blink, "led", 2048, NULL, 10, NULL);      // LED 优先级 10（更高！）
// LED 任务会抢占 WiFi 任务，导致网络超时

// ❌ 错误：所有任务都是同优先级
xTaskCreate(task_a, "A", 2048, NULL, 5, NULL);
xTaskCreate(task_b, "B", 2048, NULL, 5, NULL);
xTaskCreate(task_c, "C", 2048, NULL, 5, NULL);
xTaskCreate(task_d, "D", 2048, NULL, 5, NULL);
// 时间片轮转，任务 D 可能长时间得不到执行（饿死）
```

**正确示例**：
```c
// ✅ 正确：按重要性分层
// 优先级分配原则（数字越大优先级越高）：
// - 空闲任务: 0
// - 低优先级后台任务: 1-3
// - 普通业务任务: 4-6
// - 高优先级任务: 7-10
// - 实时任务: 11-15
// - 看门狗任务: 16+

xTaskCreate(wifi_task, "wifi", 4096, NULL, 8, NULL);      // WiFi 高优先级
xTaskCreate(sensor_task, "sensor", 2048, NULL, 7, NULL);  // 传感器中等
xTaskCreate(led_blink, "led", 1024, NULL, 2, NULL);       // LED 低优先级
xTaskCreate(watchdog, "wdt", 512, NULL, 20, NULL);        // 看门狗最高

// ✅ 避免同优先级任务过多（每层 ≤ 3 个任务）
```

---

#### 7. **队列满未处理导致数据丢失（中危）**
- **问题**：队列发送失败是否有错误处理？

**错误示例**：
```c
// ❌ 中危：队列满时直接丢弃数据，无记录
void send_event(event_t *evt) {
    xQueueSend(event_queue, evt, 0);  // 发送失败时静默丢弃
}
```

**正确示例**：
```c
// ✅ 正确：队列满时记录错误 + 覆盖旧数据
void send_event(event_t *evt) {
    if (xQueueSend(event_queue, evt, 0) != pdTRUE) {
        // 策略 1：记录错误
        ESP_LOGW(TAG, "队列已满，丢弃事件 %d", evt->type);
        
        // 策略 2：覆盖最旧的数据（环形队列）
        event_t dummy;
        xQueueReceive(event_queue, &dummy, 0);  // 丢弃最旧
        xQueueSend(event_queue, evt, 0);        // 重新发送
        
        // 策略 3：阻塞等待（适用于关键数据）
        // xQueueSend(event_queue, evt, pdMS_TO_TICKS(100));
    }
}
```

---

## 强制规范

### 1. **死锁预防**
- 多个互斥锁必须按全局统一顺序获取
- 所有阻塞式 Take 必须设置超时（不用 portMAX_DELAY）
- 使用死锁检测工具（开发阶段启用 `configUSE_MUTEXES`）

### 2. **优先级管理**
- 使用互斥锁（Mutex）代替二值信号量（Binary Semaphore）
- 互斥锁自动支持优先级继承

### 3. **任务栈管理**
- 栈大小 ≥ 实际使用量 * 1.5 倍
- 开发阶段使用 `uxTaskGetStackHighWaterMark()` 监控
- 生产环境启用 `configCHECK_FOR_STACK_OVERFLOW = 2`

### 4. **信号量成对**
- Take/Give 必须配对，异常分支也要释放
- 使用 `goto cleanup` 或 RAII 风格统一释放

### 5. **临界区规则**
- 持续时间 < 50us
- 禁止在临界区内：延时、循环、阻塞函数、malloc/free

### 6. **任务优先级分层**
```
优先级 20+: 看门狗任务
优先级 11-15: 实时任务（高频传感器、关键控制）
优先级 7-10: 高优先级任务（WiFi、BLE、关键业务）
优先级 4-6: 普通业务任务
优先级 1-3: 后台任务（日志、统计）
优先级 0: 空闲任务
```

### 7. **队列满处理**
- 发送失败必须记录日志
- 关键数据设置超时重试
- 非关键数据可覆盖旧数据

---

## 输出格式
【风险等级】致命/高/中/低
【位置】文件名:行号
【问题类型】死锁/反转/饿死/栈溢出/信号量泄漏/临界区过长
【错误代码片段】
【风险说明】可能导致的后果
【修复方案 + 正确示例】
