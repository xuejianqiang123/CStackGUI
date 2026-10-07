---
name: error_handling_check
description: 审查错误码处理、异常捕获、断言使用、日志记录、容错机制、故障恢复策略。触发条件：改动涉及错误码/断言/日志/回调失败路径，或 piv-review-changes 按领域路由到本技能时自动执行。
auto_invoke: true
allowed-tools: [ReadFile, GrepSearch]
---
# 技能：错误处理与容错机制审查

> **本工程落地口径**：正文示例里的 `ESP_ERROR_CHECK` / `HAL_StatusTypeDef` / `HAL_UART_Init`
> 是**上游模板残留**。本工程的错误面是 `cg_*` 的返回码、回调失败路径、句柄与 GDI 对象释放、
> 以及"复位外设/重启系统"在本工程对应"关窗重开 / 重建控件"。判据看**缺陷模式**
> （返回值不判、失败路径漏释放、日志缺失），不看 API 名。

## ⚠️ 核心原则（对应 AI 评审第 6 层：错误处理与资源泄漏）
- **返回值必须检查**：malloc/API 调用的返回值不能忽略
- **资源必须释放**：异常分支也要释放 malloc/文件句柄/互斥锁
- **错误必须上报**：关键失败必须记录日志 + 上报
- **恢复必须实现**：硬件错误重试 3 次 → 降级 → 复位 → 报警
- **生产环境禁用 assert**：改用日志 + 恢复逻辑

## 触发条件
- 函数返回值检查（malloc/API/HAL）
- assert/configASSERT 断言
- ESP_ERROR_CHECK/HAL_StatusTypeDef
- 异常处理逻辑
- 资源分配与释放（malloc/free、文件、互斥锁）
- 错误日志记录

---

## 🔍 完整检查清单

### 🔴 第 6 层：错误处理与资源泄漏（稳定性级）

#### 1. **函数返回值未检查（致命）**
- **问题**：malloc/API 调用是否检查了返回值？
- **后果**：野指针解引用、系统崩溃

**错误示例**：
```c
// ❌ 致命：malloc 返回值未检查
void process_data(void) {
    uint8_t *buffer = (uint8_t *)malloc(1024);
    buffer[0] = 0x01;  // 如果 malloc 失败，这里会崩溃！
    
    // ESP-IDF API 返回值未检查
    esp_wifi_connect();  // 如果失败呢？
    nvs_set_u32(handle, "key", value);  // 如果失败呢？
}

// ❌ 致命：HAL 返回值未检查
void stm32_init(void) {
    HAL_StatusTypeDef ret;
    ret = HAL_UART_Init(&huart1);  // 返回值未使用
    HAL_UART_Transmit(&huart1, data, len, 1000);  // 继续执行，但 UART 可能未初始化
}
```

**正确示例**：
```c
// ✅ 正确：检查所有可能失败的调用
esp_err_t process_data(void) {
    uint8_t *buffer = (uint8_t *)malloc(1024);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "内存分配失败");
        return ESP_ERR_NO_MEM;
    }
    
    buffer[0] = 0x01;
    
    esp_err_t ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi 连接失败: %s", esp_err_to_name(ret));
        free(buffer);
        return ret;
    }
    
    ret = nvs_set_u32(handle, "key", value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS 写入失败: %s", esp_err_to_name(ret));
        free(buffer);
        return ret;
    }
    
    free(buffer);
    return ESP_OK;
}

// ✅ STM32 示例
esp_err_t stm32_init(void) {
    HAL_StatusTypeDef ret = HAL_UART_Init(&huart1);
    if (ret != HAL_OK) {
        Error_Handler();  // 初始化失败，进入错误处理
        return ESP_FAIL;
    }
    
    ret = HAL_UART_Transmit(&huart1, data, len, 1000);
    if (ret != HAL_OK) {
        ESP_LOGE(TAG, "UART 发送失败: %d", ret);
        return ESP_FAIL;
    }
    
    return ESP_OK;
}
```

---

#### 2. **异常分支未释放资源（致命）**
- **问题**：提前 return 时是否释放了已分配的资源？
- **后果**：内存泄漏、文件句柄耗尽、互斥锁泄漏

**错误示例**：
```c
// ❌ 致命：异常分支忘记释放资源
esp_err_t read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return ESP_FAIL;
    }
    
    char *buffer = malloc(1024);
    if (buffer == NULL) {
        return ESP_ERR_NO_MEM;  // 忘记 fclose(f)！
    }
    
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) {
        return ESP_ERR_NO_MEM;  // 忘记 free(buffer) 和 fclose(f)！
    }
    
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    if (validate(buffer) != ESP_OK) {
        return ESP_FAIL;  // 忘记 Give、free、fclose！
    }
    
    // ... 正常流程
    xSemaphoreGive(mutex);
    vSemaphoreDelete(mutex);
    free(buffer);
    fclose(f);
    return ESP_OK;
}
```

**正确示例**：
```c
// ✅ 正确：使用 goto cleanup 统一释放资源
esp_err_t read_file(const char *path) {
    esp_err_t ret = ESP_FAIL;
    FILE *f = NULL;
    char *buffer = NULL;
    SemaphoreHandle_t mutex = NULL;
    
    f = fopen(path, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "打开文件失败");
        ret = ESP_FAIL;
        goto cleanup;
    }
    
    buffer = malloc(1024);
    if (buffer == NULL) {
        ESP_LOGE(TAG, "内存分配失败");
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    
    mutex = xSemaphoreCreateMutex();
    if (mutex == NULL) {
        ESP_LOGE(TAG, "创建互斥锁失败");
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    
    if (xSemaphoreTake(mutex, portMAX_DELAY) != pdTRUE) {
        ret = ESP_ERR_TIMEOUT;
        goto cleanup;
    }
    
    if (validate(buffer) != ESP_OK) {
        ret = ESP_ERR_INVALID_STATE;
        goto cleanup_mutex;
    }
    
    // ... 正常流程
    ret = ESP_OK;
    
cleanup_mutex:
    xSemaphoreGive(mutex);
    
cleanup:
    if (mutex) vSemaphoreDelete(mutex);
    if (buffer) free(buffer);
    if (f) fclose(f);
    
    return ret;
}

// ✅ 更好：使用资源管理宏（RAII 风格）
#define AUTO_FREE __attribute__((cleanup(free_ptr)))
static inline void free_ptr(void **ptr) {
    if (*ptr) free(*ptr);
}

esp_err_t read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) return ESP_FAIL;
    
    AUTO_FREE char *buffer = malloc(1024);  // 作用域结束自动释放
    if (buffer == NULL) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    
    // ... 正常流程，return 时自动释放
    fclose(f);
    return ESP_OK;
}
```

---

#### 3. **assert 在生产环境未禁用（致命）**
- **问题**：生产环境是否还在使用 assert？
- **后果**：触发 assert 导致系统复位，丢失运行数据

**错误示例**：
```c
// ❌ 致命：生产环境使用 assert
void process_packet(packet_t *pkt) {
    assert(pkt != NULL);  // 生产环境触发 assert 会复位！
    assert(pkt->len <= MAX_SIZE);
    
    memcpy(buffer, pkt->data, pkt->len);
}
```

**正确示例**：
```c
// ✅ 正确：生产环境改用日志 + 恢复逻辑
esp_err_t process_packet(packet_t *pkt) {
    if (pkt == NULL) {
        ESP_LOGE(TAG, "空指针：packet 为 NULL");
        return ESP_ERR_INVALID_ARG;  // 返回错误，不复位
    }
    
    if (pkt->len > MAX_SIZE) {
        ESP_LOGE(TAG, "长度超限: %u > %u", pkt->len, MAX_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }
    
    memcpy(buffer, pkt->data, pkt->len);
    return ESP_OK;
}

// ✅ 开发环境：使用 assert 快速定位问题
// 生产环境：使用 #define NDEBUG 禁用 assert，改用日志
#ifdef DEBUG
    #define CHECK_PTR(ptr) assert(ptr != NULL)
#else
    #define CHECK_PTR(ptr) \
        do { \
            if ((ptr) == NULL) { \
                ESP_LOGE(TAG, "空指针: %s", #ptr); \
                return ESP_ERR_INVALID_ARG; \
            } \
        } while(0)
#endif
```

---

#### 4. **硬件错误未做恢复（致命）**
- **问题**：传感器离线、通信失败是否有恢复机制？
- **后果**：功能失效、数据丢失

**错误示例**：
```c
// ❌ 致命：硬件错误后直接放弃
float read_temperature(void) {
    esp_err_t ret = sensor_read(&temp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "传感器读取失败");
        return -1.0;  // 直接返回，不重试
    }
    return temp;
}
```

**正确示例**：
```c
// ✅ 正确：硬件错误重试 3 次 → 降级 → 上报
float read_temperature(void) {
    float temp = 0.0;
    esp_err_t ret;
    
    // 重试 3 次
    for (int retry = 0; retry < 3; retry++) {
        ret = sensor_read(&temp);
        if (ret == ESP_OK) {
            return temp;  // 成功
        }
        
        ESP_LOGW(TAG, "传感器读取失败，重试 %d/3", retry + 1);
        vTaskDelay(pdMS_TO_TICKS(100));  // 延时 100ms 后重试
    }
    
    // 重试 3 次后仍失败 → 降级处理
    ESP_LOGE(TAG, "传感器持续失败，使用上次值");
    
    // 策略 1：使用上次有效值
    static float last_valid_temp = 25.0;
    temp = last_valid_temp;
    
    // 策略 2：上报故障
    report_sensor_fault(SENSOR_ID_TEMP);
    
    // 策略 3：触发系统自检
    // system_self_check();
    
    return temp;
}

// ✅ 更完整的恢复策略
typedef enum {
    RECOVER_RETRY,      // 重试
    RECOVER_DEGRADE,    // 降级（使用备用方案）
    RECOVER_RESET,      // 复位外设
    RECOVER_REBOOT,     // 重启系统
    RECOVER_ALERT       // 报警
} recover_strategy_t;

esp_err_t handle_sensor_error(esp_err_t err, int retry_count) {
    if (retry_count < 3) {
        return RECOVER_RETRY;
    }
    
    if (retry_count < 5) {
        ESP_LOGW(TAG, "尝试复位传感器");
        sensor_reset();
        return RECOVER_RESET;
    }
    
    ESP_LOGE(TAG, "传感器无法恢复，进入降级模式");
    return RECOVER_DEGRADE;
}
```

---

#### 5. **错误日志不完整（高危）**
- **问题**：错误日志是否包含：时间戳、错误码、现场数据？
- **后果**：问题难以复现、无法定位

**错误示例**：
```c
// ❌ 高危：日志信息不足
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "失败");  // 什么失败？为什么失败？
}

// ❌ 高危：关键错误无日志
if (flash_write(addr, data, len) != ESP_OK) {
    return;  // 静默失败
}
```

**正确示例**：
```c
// ✅ 正确：日志包含完整上下文
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "UART 发送失败: 错误码=%s, 数据长度=%u, 重试次数=%d, 时间戳=%lld",
             esp_err_to_name(ret), len, retry, esp_timer_get_time());
}

// ✅ 正确：关键错误必须记录
esp_err_t ret = flash_write(addr, data, len);
if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Flash 写入失败: 地址=0x%08X, 长度=%u, 错误=%s",
             addr, len, esp_err_to_name(ret));
    
    // 可选：保存错误到 NVS，用于故障分析
    save_error_log(ret, addr, len);
    
    return ret;
}

// ✅ 日志分级规则
// ESP_LOGE: 致命错误、数据丢失、功能失效
// ESP_LOGW: 可恢复错误、降级运行、资源不足
// ESP_LOGI: 关键操作、状态变更、用户行为
// ESP_LOGD: 调试信息（生产环境禁用）
```

---

#### 6. **重试次数无上限（中危）**
- **问题**：重试循环是否有次数限制？
- **后果**：死循环、看门狗复位

**错误示例**：
```c
// ❌ 中危：无限重试
while (connect_wifi() != ESP_OK) {
    ESP_LOGW(TAG, "WiFi 连接失败，重试...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    // 如果 WiFi 硬件损坏，会永远卡在这里
}
```

**正确示例**：
```c
// ✅ 正确：限制重试次数
#define MAX_RETRY 10

int retry = 0;
while (retry < MAX_RETRY) {
    esp_err_t ret = connect_wifi();
    if (ret == ESP_OK) {
        break;
    }
    
    retry++;
    ESP_LOGW(TAG, "WiFi 连接失败，重试 %d/%d", retry, MAX_RETRY);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

if (retry >= MAX_RETRY) {
    ESP_LOGE(TAG, "WiFi 连接失败，进入离线模式");
    enter_offline_mode();  // 降级处理
}
```

---

## 强制规范

### 1. **返回值检查规则**
- 所有可能失败的函数必须检查返回值
- 包括：malloc、API 调用、HAL 函数、文件操作、互斥锁获取

### 2. **资源释放规则**
- 所有 malloc 必须配对 free
- 所有 fopen 必须配对 fclose
- 所有 Take 必须配对 Give
- 异常分支必须释放已分配资源
- 推荐使用 `goto cleanup` 或 RAII 风格

### 3. **assert 使用规则**
- 开发环境：使用 assert 快速定位问题
- 生产环境：使用 `#define NDEBUG` 禁用 assert，改用日志 + 恢复

### 4. **错误恢复策略**
```
硬件错误 → 重试 3 次（间隔 100ms）→ 降级（备用方案）→ 复位外设 → 重启系统 → 报警
```

### 5. **日志记录规则**
- **必须记录**：错误码、现场数据、时间戳、重试次数
- **日志分级**：
  - `ESP_LOGE`: 致命错误、功能失效
  - `ESP_LOGW`: 可恢复错误、降级运行
  - `ESP_LOGI`: 关键操作、状态变更
  - `ESP_LOGD`: 调试信息（生产禁用）

### 6. **重试限制**
- 重试次数 ≤ 10 次
- 重试间隔：指数退避（100ms → 200ms → 400ms）
- 超过上限后进入降级模式

---

## 输出格式
【风险等级】致命/高/中/低
【位置】文件名:行号
【问题类型】未检查返回值/资源泄漏/assert滥用/无恢复/日志缺失
【错误代码片段】
【风险说明】可能导致的后果
【修复方案 + 正确示例】
