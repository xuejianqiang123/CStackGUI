---
name: stack_overflow_check
description: 自动扫描固件代码栈溢出风险。识别局部大数组、结构体、递归、阻塞函数，自动评估栈用量，输出栈风险报告。
auto_invoke: true
allowed-tools: [ReadFile, GrepSearch]
---
# 技能：栈溢出安全审查（专家级）

## 触发条件
识别到以下场景自动执行：
- 函数内部定义局部数组、结构体（`char buf[...]`、`uint8_t data[...]`）
- 递归调用或深层函数嵌套
- RTOS 任务函数定义（`xTaskCreate`、`osThreadNew`）
- 中断服务函数（ISR）内的局部变量
- 深层循环嵌套、复杂局部变量
- `alloca()` 动态栈分配
- 可变参数函数（`va_list`）

---

## 完整检查清单（10 项致命风险）

### 1. 【致命】局部大数组导致栈溢出
**问题原理**：
- 函数局部变量分配在**栈**上，而栈空间极其有限
- ESP32 默认任务栈仅 4KB，STM32 主栈通常 1-4KB
- 定义 `char buf[1024]` 在 4KB 栈中直接消耗 25% 空间

**检测规则**：
- 局部数组 ≥ 512 字节：🔴 **致命**（立即栈溢出）
- 局部数组 128-512 字节：🟠 **高危**（多函数嵌套时溢出）
- 局部数组 64-128 字节：🟡 **中危**（需评估调用链深度）

**❌ 错误示例**：
```c
void wifi_parse_packet(void) {
    char rx_buf[2048];          // 🔴 2KB 局部数组！ESP32 默认栈只有 4KB
    wifi_receive(rx_buf, 2048);
    parse_data(rx_buf);
}

void uart_handler(void) {
    uint8_t log[512];           // 🟠 512B，占用 STM32 1KB 栈的一半
    sprintf(log, "Data: %d", get_sensor_val());
    uart_send(log, 512);
}
```

**✅ 正确示例**：
```c
// 方案1：改为 static（存储在全局区，零栈消耗）
void wifi_parse_packet(void) {
    static char rx_buf[2048];   // ✅ 存储在 .bss 段，不占用栈
    wifi_receive(rx_buf, 2048);
    parse_data(rx_buf);
}

// 方案2：改为动态分配（堆空间更大，需注意释放）
void wifi_parse_packet(void) {
    char *rx_buf = malloc(2048);
    if (rx_buf == NULL) return;
    wifi_receive(rx_buf, 2048);
    parse_data(rx_buf);
    free(rx_buf);               // ✅ 必须释放
}

// 方案3：使用全局缓冲区
static char g_rx_buf[2048];
void wifi_parse_packet(void) {
    wifi_receive(g_rx_buf, 2048);
    parse_data(g_rx_buf);
}
```

**💥 后果**：
- 任务崩溃、系统重启
- 栈溢出覆盖相邻内存（破坏其他任务数据）
- 看门狗超时复位（函数卡死）

---

### 2. 【致命】递归调用无终止条件
**问题原理**：
- 每次递归调用都会压栈（保存返回地址、局部变量）
- 递归 100 层 = 栈消耗 100 倍
- 嵌入式系统栈空间极小，递归深度受限

**❌ 错误示例**：
```c
// 🔴 解析 JSON 嵌套层级无限制，恶意数据可栈溢出
void parse_json(json_node_t *node) {
    if (node->type == JSON_OBJECT) {
        for (int i = 0; i < node->child_count; i++) {
            parse_json(&node->children[i]);  // 🔴 递归无深度限制
        }
    }
}

// 🔴 链表遍历用递归，链表过长直接崩溃
void free_list(node_t *head) {
    if (head == NULL) return;
    free_list(head->next);      // 🔴 链表1000个节点 = 1000层递归
    free(head);
}
```

**✅ 正确示例**：
```c
// 方案1：递归深度限制
#define MAX_JSON_DEPTH 8
void parse_json(json_node_t *node, int depth) {
    if (depth > MAX_JSON_DEPTH) {
        log_error("JSON nested too deep");
        return;  // ✅ 强制终止
    }
    if (node->type == JSON_OBJECT) {
        for (int i = 0; i < node->child_count; i++) {
            parse_json(&node->children[i], depth + 1);
        }
    }
}

// 方案2：改为循环（零递归，零栈风险）
void free_list(node_t *head) {
    node_t *current = head;
    while (current != NULL) {
        node_t *next = current->next;
        free(current);
        current = next;
    }
}
```

**💥 后果**：
- 深度递归直接栈溢出崩溃
- 攻击者通过构造深层嵌套数据触发 DoS

---

### 3. 【致命】RTOS 任务栈配置过小
**问题原理**：
- FreeRTOS 任务栈在创建时固定分配，运行时无法扩展
- 栈溢出会破坏 TCB（Task Control Block）导致系统崩溃
- 默认栈大小通常不够用（ESP-IDF 默认 3072 字节）

**❌ 错误示例**：
```c
// 🔴 任务栈只有 512 字节，但函数内有 256B 局部变量
xTaskCreate(wifi_task, "wifi", 512, NULL, 5, NULL);

void wifi_task(void *param) {
    char ssid[32];              // 32B
    char password[64];          // 64B
    uint8_t rx_buffer[256];     // 256B - 总共 352B
    wifi_scan_results_t results[10];  // 🔴 又增加了几百字节！
    // ...
}
```

**✅ 正确示例**：
```c
// 方案1：增加栈大小
#define WIFI_TASK_STACK_SIZE 4096
xTaskCreate(wifi_task, "wifi", WIFI_TASK_STACK_SIZE, NULL, 5, NULL);

// 方案2：任务函数内的大变量改为 static
void wifi_task(void *param) {
    char ssid[32];
    char password[64];
    static uint8_t rx_buffer[256];           // ✅ 不占用栈
    static wifi_scan_results_t results[10];  // ✅ 不占用栈
    // ...
}

// 方案3：启用栈溢出检测
#define configCHECK_FOR_STACK_OVERFLOW 2  // FreeRTOS 配置
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName) {
    log_fatal("Task %s stack overflow!", pcTaskName);
    while(1);  // 死循环保留现场
}
```

**💥 后果**：
- 任务崩溃、系统频繁重启
- 难以排查的内存破坏问题

---

### 4. 【致命】中断服务函数内定义局部变量
**问题原理**：
- ISR 使用**主栈**（MSP），而非任务栈
- 中断嵌套会累积栈消耗
- ISR 栈溢出直接导致 HardFault

**❌ 错误示例**：
```c
void USART1_IRQHandler(void) {
    uint8_t rx_buffer[128];     // 🔴 中断栈极小，定义大数组致命
    uint8_t parse_buffer[64];
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)) {
        HAL_UART_Receive(&huart1, rx_buffer, 128, 100);
        protocol_parse(rx_buffer, parse_buffer);
    }
}
```

**✅ 正确示例**：
```c
// 方案1：全局缓冲区
static uint8_t g_isr_rx_buffer[128];
static uint8_t g_isr_parse_buffer[64];

void USART1_IRQHandler(void) {
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)) {
        HAL_UART_Receive(&huart1, g_isr_rx_buffer, 128, 100);
        protocol_parse(g_isr_rx_buffer, g_isr_parse_buffer);
    }
}

// 方案2：ISR 只接收数据，解析移到任务中
static QueueHandle_t uart_queue;
void USART1_IRQHandler(void) {
    uint8_t byte;
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)) {
        byte = USART1->DR;
        xQueueSendFromISR(uart_queue, &byte, NULL);  // ✅ 只传递1字节
    }
}
```

**💥 后果**：
- HardFault 异常
- 中断嵌套时栈溢出覆盖其他中断数据

---

### 5. 【高危】循环内定义大数组
**问题原理**：
- 编译器可能每次循环都重新分配栈空间
- 某些编译器优化不当会导致栈累积

**❌ 错误示例**：
```c
void process_sensors(void) {
    for (int i = 0; i < 10; i++) {
        uint8_t temp_buf[256];  // 🔴 每次循环都分配？
        read_sensor(i, temp_buf);
        process_data(temp_buf);
    }
}
```

**✅ 正确示例**：
```c
void process_sensors(void) {
    uint8_t temp_buf[256];      // ✅ 循环外定义，只分配一次
    for (int i = 0; i < 10; i++) {
        read_sensor(i, temp_buf);
        process_data(temp_buf);
    }
}
```

---

### 6. 【高危】深层函数调用链累积栈消耗
**问题原理**：
- 调用链：A → B → C → D，每层函数的局部变量都累积在栈上
- 栈消耗 = 每层局部变量之和

**❌ 错误示例**：
```c
void task_main(void) {
    char buf1[128];             // 128B
    process_step1(buf1);
}

void process_step1(char *data) {
    char buf2[256];             // 256B（累积 384B）
    process_step2(buf2);
}

void process_step2(char *data) {
    char buf3[512];             // 512B（累积 896B！）
    memcpy(buf3, data, 512);
}
```

**✅ 正确示例**：
```c
// 方案1：大缓冲区改为 static
static char g_shared_buffer[512];

void task_main(void) {
    char buf1[128];
    process_step1(buf1);
}

void process_step1(char *data) {
    process_step2(g_shared_buffer);  // ✅ 共享缓冲区
}

void process_step2(char *data) {
    memcpy(data, g_shared_buffer, 512);
}
```

---

### 7. 【高危】可变参数函数（`va_list`）栈消耗不可控
**问题原理**：
- `printf`、`sprintf` 等可变参数函数内部会分配较大栈空间
- 嵌入式环境应避免频繁使用

**❌ 错误示例**：
```c
void log_handler(const char *fmt, ...) {
    char buffer[512];           // 🔴 512B 栈消耗
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, 512, fmt, args);
    va_end(args);
    uart_send(buffer, strlen(buffer));
}
```

**✅ 正确示例**：
```c
// 方案1：限制缓冲区大小
void log_handler(const char *fmt, ...) {
    char buffer[128];           // ✅ 缩小到 128B
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, 128, fmt, args);
    va_end(args);
    uart_send(buffer, strlen(buffer));
}

// 方案2：使用静态缓冲区
static char g_log_buffer[512];
void log_handler(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(g_log_buffer, 512, fmt, args);
    va_end(args);
    uart_send(g_log_buffer, strlen(g_log_buffer));
}
```

---

### 8. 【中危】局部结构体过大
**问题原理**：
- 结构体可能包含多个数组、嵌套结构体
- 实际占用空间可能超预期

**❌ 错误示例**：
```c
typedef struct {
    char name[64];
    uint8_t data[256];
    uint32_t timestamp;
} sensor_packet_t;               // 324 字节

void process_sensor(void) {
    sensor_packet_t packet;      // 🔴 324B 局部变量
    read_sensor(&packet);
}
```

**✅ 正确示例**：
```c
void process_sensor(void) {
    static sensor_packet_t packet;  // ✅ 改为 static
    read_sensor(&packet);
}
```

---

### 9. 【中危】`alloca()` 动态栈分配
**问题原理**：
- `alloca()` 在栈上动态分配内存，大小不可控
- 嵌入式系统**禁止使用** `alloca()`

**❌ 错误示例**：
```c
void process_data(int size) {
    char *buf = alloca(size);   // 🔴 size 可能为 10KB！
    read_data(buf, size);
}
```

**✅ 正确示例**：
```c
void process_data(int size) {
    if (size > 1024) return;    // ✅ 限制大小
    char *buf = malloc(size);
    if (buf == NULL) return;
    read_data(buf, size);
    free(buf);
}
```

---

### 10. 【低危】调试打印未控制栈消耗
**问题原理**：
- `printf` 内部会分配较大栈空间（通常 128-512B）

**❌ 错误示例**：
```c
void sensor_task(void) {
    char buf[256];
    read_sensor(buf);
    printf("Sensor data: %s\n", buf);  // 🔴 printf 额外消耗栈
}
```

**✅ 正确示例**：
```c
void sensor_task(void) {
    char buf[256];
    read_sensor(buf);
    ESP_LOGI(TAG, "Data: %s", buf);  // ✅ 使用轻量级日志
}
```

---

## 强制规范

### 栈使用原则
1. **局部变量限制**：
   - 单个函数栈消耗 < 256B
   - 大于 128B 的数组必须改为 `static` 或动态分配
2. **递归限制**：
   - 必须有明确的深度限制（通常 ≤ 8 层）
   - 优先使用循环替代递归
3. **RTOS 任务栈**：
   - 默认任务栈 ≥ 4096 字节
   - 启用栈溢出检测（`configCHECK_FOR_STACK_OVERFLOW`）
4. **中断函数**：
   - ISR 内禁止定义局部数组
   - ISR 栈消耗必须 < 64B

### 栈估算方法
```c
// 估算公式：任务栈大小 = 基础开销 + 最深调用链栈消耗 + 安全余量
// - 基础开销：200-300B（RTOS 上下文切换）
// - 调用链消耗：逐层累加每个函数的局部变量
// - 安全余量：50%（防止编译器优化差异）

// 示例
void task_main(void) {           // 128B 局部变量
    char buf1[128];
    process_data(buf1);
}

void process_data(char *data) {  // 256B 局部变量
    char buf2[256];
    parse_protocol(buf2);
}

void parse_protocol(char *data) { // 64B 局部变量
    uint32_t temp[16];
}

// 栈消耗 = 300（基础） + 128 + 256 + 64 = 748B
// 实际分配 = 748 * 1.5 = 1122B → 建议 2048B
```

---

## 输出格式
【风险等级】致命/高/中/低
【位置】文件名:行号:函数名
【问题类型】局部大数组/递归无限制/RTOS栈过小/ISR栈风险
【栈消耗估算】当前函数 XXX 字节，调用链累积 XXX 字节
【错误代码片段】
【问题原理】为什么会导致栈溢出
【修复方案 + 正确示例】
【栈配置建议】（针对 RTOS 任务）
