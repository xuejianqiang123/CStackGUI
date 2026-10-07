---
name: memory_leak_check
description: 检测动态内存分配泄漏、堆碎片、内存越界、野指针，以及句柄/GDI 对象未配对释放等问题。触发条件：改动涉及 malloc/free、句柄、GDI 对象、窗口生命周期，或 piv-review-changes 按领域路由到本技能时自动执行。
auto_invoke: true
allowed-tools: [ReadFile, GrepSearch]
---
# 技能：内存泄漏与堆管理审查（专家级）

> **本工程落地口径**：正文示例里的 `pvPortMalloc` / `heap_caps_malloc` / `esp_get_free_heap_size`
> 是**上游模板残留**（无 FreeRTOS / 无 ESP32）。本工程是 Win32 桌面程序，**除堆之外还要管
> 句柄与 GDI 对象**（`CreateFont`/`CreatePen`/`CreateBitmap` → `DeleteObject`，`GetDC` → `ReleaseDC`），
> 这是本工程更高频的泄漏源。"禁止在中断里分配"在本工程对应"禁止在非 UI 线程里碰控件"。

## 触发条件
识别到以下场景自动执行：
- `malloc`/`calloc`/`realloc`/`free` 调用
- `pvPortMalloc`/`vPortFree`（FreeRTOS）
- `heap_caps_malloc`/`heap_caps_free`（ESP32）
- `new`/`delete`（C++）
- 内存池操作（`osMemoryPoolAlloc`、自定义内存池）
- 字符串拼接、动态缓冲区
- 循环内的动态分配

---

## 完整检查清单（12 项致命风险）

### 1. 【致命】`malloc` 后未判空直接使用
**问题原理**：
- 内存分配可能失败（堆空间不足、碎片严重）
- 未判空直接解引用导致**空指针异常**（Null Pointer Dereference）
- 嵌入式系统无 MMU 保护，直接访问 0 地址会崩溃

**❌ 错误示例**：
```c
void process_data(int size) {
    char *buffer = malloc(size);
    memcpy(buffer, data, size);     // 🔴 malloc 失败时 buffer = NULL，崩溃！
    process(buffer);
    free(buffer);
}

// 🔴 FreeRTOS 版本
void wifi_task(void *param) {
    uint8_t *rx_buf = pvPortMalloc(1024);
    wifi_receive(rx_buf, 1024);     // 🔴 未判空，分配失败时崩溃
    vPortFree(rx_buf);
}
```

**✅ 正确示例**：
```c
void process_data(int size) {
    char *buffer = malloc(size);
    if (buffer == NULL) {
        log_error("Memory allocation failed");
        return;                     // ✅ 立即返回
    }
    memcpy(buffer, data, size);
    process(buffer);
    free(buffer);
}

// ✅ FreeRTOS 版本
void wifi_task(void *param) {
    uint8_t *rx_buf = pvPortMalloc(1024);
    if (rx_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        vTaskDelay(pdMS_TO_TICKS(100));
        return;
    }
    wifi_receive(rx_buf, 1024);
    vPortFree(rx_buf);
}
```

**💥 后果**：
- 系统崩溃、复位
- 难以排查的间歇性故障（内存不足时才触发）

---

### 2. 【致命】分配的内存未释放（经典内存泄漏）
**问题原理**：
- 每次泄漏都会永久占用一块内存
- 长时间运行后堆空间耗尽，系统无法分配新内存

**❌ 错误示例**：
```c
// 🔴 每次调用泄漏 1024 字节
void send_message(const char *msg) {
    char *buffer = malloc(1024);
    if (buffer == NULL) return;
    sprintf(buffer, "MSG: %s", msg);
    uart_send(buffer, strlen(buffer));
    // 🔴 忘记 free(buffer)！
}

// 🔴 异常分支泄漏
int read_file(const char *path) {
    FILE *fp = fopen(path, "r");
    char *buffer = malloc(1024);
    if (buffer == NULL) {
        return -1;              // 🔴 忘记 fclose(fp)！
    }
    fread(buffer, 1, 1024, fp);
    free(buffer);
    fclose(fp);
    return 0;
}

// 🔴 RTOS 任务泄漏
void worker_task(void *param) {
    while (1) {
        uint8_t *data = pvPortMalloc(512);
        if (data == NULL) continue;
        process_data(data);
        // 🔴 每次循环泄漏 512 字节！
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

**✅ 正确示例**：
```c
void send_message(const char *msg) {
    char *buffer = malloc(1024);
    if (buffer == NULL) return;
    sprintf(buffer, "MSG: %s", msg);
    uart_send(buffer, strlen(buffer));
    free(buffer);               // ✅ 成对释放
}

// ✅ 异常分支必须释放所有资源
int read_file(const char *path) {
    FILE *fp = fopen(path, "r");
    char *buffer = malloc(1024);
    if (buffer == NULL) {
        fclose(fp);             // ✅ 先释放文件句柄
        return -1;
    }
    fread(buffer, 1, 1024, fp);
    free(buffer);
    fclose(fp);
    return 0;
}

// ✅ RTOS 任务必须释放
void worker_task(void *param) {
    while (1) {
        uint8_t *data = pvPortMalloc(512);
        if (data == NULL) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        process_data(data);
        vPortFree(data);        // ✅ 每次循环都释放
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

**💥 后果**：
- 系统运行几小时/几天后崩溃
- 堆空间耗尽，`malloc` 返回 `NULL`

---

### 3. 【致命】重复释放同一内存（Double Free）
**问题原理**：
- 释放已经释放过的指针会破坏堆管理结构
- 导致堆损坏（Heap Corruption），下次分配时崩溃

**❌ 错误示例**：
```c
void process_data(void) {
    char *buf1 = malloc(128);
    char *buf2 = buf1;          // buf2 和 buf1 指向同一内存
    
    process(buf1);
    free(buf1);                 // 第一次释放
    
    process(buf2);
    free(buf2);                 // 🔴 重复释放同一内存！
}

// 🔴 条件分支导致重复释放
void handle_error(char *buffer, int error) {
    if (error == ERR_TIMEOUT) {
        free(buffer);           // 分支1释放
        return;
    }
    if (error == ERR_INVALID) {
        free(buffer);           // 分支2也释放
        return;
    }
    free(buffer);               // 🔴 正常流程又释放！
}
```

**✅ 正确示例**：
```c
void process_data(void) {
    char *buf1 = malloc(128);
    char *buf2 = buf1;
    
    process(buf1);
    free(buf1);
    buf1 = NULL;                // ✅ 释放后立即置 NULL
    buf2 = NULL;
    
    // 后续代码不会重复释放
}

// ✅ 统一释放路径
void handle_error(char *buffer, int error) {
    if (error == ERR_TIMEOUT) {
        log_error("Timeout");
        goto cleanup;           // ✅ 统一跳转到释放
    }
    if (error == ERR_INVALID) {
        log_error("Invalid");
        goto cleanup;
    }
    
cleanup:
    if (buffer != NULL) {
        free(buffer);
        buffer = NULL;
    }
}
```

**💥 后果**：
- 堆损坏，后续分配时崩溃
- 难以排查的内存错误

---

### 4. 【致命】释放后继续使用（Use After Free）
**问题原理**：
- 释放后的内存可能被重新分配给其他对象
- 继续使用会读到错误数据或写入破坏其他对象

**❌ 错误示例**：
```c
void process_message(void) {
    message_t *msg = malloc(sizeof(message_t));
    if (msg == NULL) return;
    
    msg->type = MSG_DATA;
    send_message(msg);
    free(msg);                  // 释放内存
    
    log_info("Message type: %d", msg->type);  // 🔴 释放后继续使用！
}

// 🔴 多线程场景
void task1(void) {
    shared_buffer = malloc(1024);
    // ...
    free(shared_buffer);        // 任务1释放
}

void task2(void) {
    memcpy(shared_buffer, data, 512);  // 🔴 任务2还在使用！
}
```

**✅ 正确示例**：
```c
void process_message(void) {
    message_t *msg = malloc(sizeof(message_t));
    if (msg == NULL) return;
    
    msg->type = MSG_DATA;
    log_info("Message type: %d", msg->type);  // ✅ 使用后再释放
    send_message(msg);
    free(msg);
    msg = NULL;                 // ✅ 释放后置 NULL，防止误用
}

// ✅ 多线程场景需要引用计数
typedef struct {
    uint8_t *data;
    int ref_count;
    SemaphoreHandle_t mutex;
} shared_buffer_t;

void release_buffer(shared_buffer_t *buf) {
    xSemaphoreTake(buf->mutex, portMAX_DELAY);
    buf->ref_count--;
    if (buf->ref_count == 0) {
        free(buf->data);        // ✅ 引用计数为0才释放
        buf->data = NULL;
    }
    xSemaphoreGive(buf->mutex);
}
```

**💥 后果**：
- 读到随机数据
- 写入破坏其他对象（内存踩踏）

---

### 5. 【高危】循环内频繁 `malloc`/`free` 导致堆碎片
**问题原理**：
- 频繁分配/释放不同大小的内存导致碎片化
- 虽然总空闲内存足够，但无连续大块，分配失败

**❌ 错误示例**：
```c
void process_packets(void) {
    for (int i = 0; i < 1000; i++) {
        int size = get_packet_size(i);  // 每次大小不同
        char *buf = malloc(size);       // 🔴 频繁分配不同大小
        if (buf == NULL) break;
        process_packet(buf, size);
        free(buf);                      // 🔴 立即释放，产生碎片
    }
}
```

**✅ 正确示例**：
```c
// 方案1：预分配固定大小
#define MAX_PACKET_SIZE 1024
void process_packets(void) {
    char *buf = malloc(MAX_PACKET_SIZE);  // ✅ 只分配一次
    if (buf == NULL) return;
    
    for (int i = 0; i < 1000; i++) {
        int size = get_packet_size(i);
        if (size > MAX_PACKET_SIZE) continue;
        process_packet(buf, size);
    }
    free(buf);                            // ✅ 循环结束后释放
}

// 方案2：使用内存池
static memory_pool_t g_packet_pool;

void init_system(void) {
    memory_pool_init(&g_packet_pool, 1024, 10);  // 10个1024字节块
}

void process_packets(void) {
    for (int i = 0; i < 1000; i++) {
        char *buf = memory_pool_alloc(&g_packet_pool);
        if (buf == NULL) break;
        process_packet(buf, 1024);
        memory_pool_free(&g_packet_pool, buf);
    }
}
```

**💥 后果**：
- 堆碎片化，大块分配失败
- 系统长时间运行后性能下降

---

### 6. 【高危】指针赋值后原内存未释放
**问题原理**：
- 指针重新赋值后，原来指向的内存失去引用
- 再也无法释放，永久泄漏

**❌ 错误示例**：
```c
char *global_buffer = NULL;

void update_buffer(int size) {
    global_buffer = malloc(size);   // 🔴 第一次分配
    // ...
    global_buffer = malloc(size*2); // 🔴 重新分配，原内存泄漏！
}

// 🔴 函数参数传递
void allocate_buffer(char **buf) {
    *buf = malloc(1024);            // 第一次分配
    // ...
    *buf = malloc(2048);            // 🔴 重新分配，原内存泄漏！
}
```

**✅ 正确示例**：
```c
char *global_buffer = NULL;

void update_buffer(int size) {
    if (global_buffer != NULL) {
        free(global_buffer);        // ✅ 先释放旧内存
    }
    global_buffer = malloc(size);
}

// ✅ 使用 realloc
void update_buffer(int new_size) {
    char *new_buf = realloc(global_buffer, new_size);
    if (new_buf == NULL) {
        // realloc 失败时原内存仍然有效
        return;
    }
    global_buffer = new_buf;        // ✅ realloc 自动管理旧内存
}
```

**💥 后果**：
- 永久泄漏，无法追踪

---

### 7. 【高危】异常分支未释放内存
**问题原理**：
- 函数有多个返回路径（`return`、`goto`、`break`）
- 某些异常分支忘记释放已分配的内存

**❌ 错误示例**：
```c
int process_file(const char *path) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) return -1;
    
    char *buffer = malloc(1024);
    if (buffer == NULL) {
        return -1;              // 🔴 忘记 fclose(fp)！
    }
    
    int size = fread(buffer, 1, 1024, fp);
    if (size < 0) {
        return -1;              // 🔴 忘记 free(buffer) 和 fclose(fp)！
    }
    
    free(buffer);
    fclose(fp);
    return 0;
}
```

**✅ 正确示例**：
```c
// 方案1：goto 统一清理
int process_file(const char *path) {
    FILE *fp = NULL;
    char *buffer = NULL;
    int ret = 0;
    
    fp = fopen(path, "r");
    if (fp == NULL) {
        ret = -1;
        goto cleanup;
    }
    
    buffer = malloc(1024);
    if (buffer == NULL) {
        ret = -1;
        goto cleanup;
    }
    
    int size = fread(buffer, 1, 1024, fp);
    if (size < 0) {
        ret = -1;
        goto cleanup;
    }
    
cleanup:
    if (buffer != NULL) free(buffer);
    if (fp != NULL) fclose(fp);
    return ret;
}

// 方案2：提前分配，延迟判断
int process_file(const char *path) {
    FILE *fp = fopen(path, "r");
    char *buffer = malloc(1024);
    int size = -1;
    
    if (fp != NULL && buffer != NULL) {
        size = fread(buffer, 1, 1024, fp);
    }
    
    // ✅ 统一释放
    if (buffer != NULL) free(buffer);
    if (fp != NULL) fclose(fp);
    
    return (size > 0) ? 0 : -1;
}
```

**💥 后果**：
- 异常情况下泄漏
- 难以排查（正常流程不泄漏）

---

### 8. 【高危】全局指针未初始化为 `NULL`
**问题原理**：
- 未初始化的全局指针可能是野指针
- 释放时判断 `if (ptr != NULL)` 会失效

**❌ 错误示例**：
```c
char *g_rx_buffer;              // 🔴 未初始化，可能是随机值

void init_system(void) {
    if (g_rx_buffer != NULL) {  // 🔴 野指针，判断无效！
        free(g_rx_buffer);
    }
    g_rx_buffer = malloc(1024);
}
```

**✅ 正确示例**：
```c
char *g_rx_buffer = NULL;       // ✅ 显式初始化为 NULL

void init_system(void) {
    if (g_rx_buffer != NULL) {
        free(g_rx_buffer);
    }
    g_rx_buffer = malloc(1024);
}
```

---

### 9. 【中危】嵌套分配未按逆序释放
**问题原理**：
- 嵌套结构体内部可能包含指针成员
- 必须先释放内部指针，再释放外部结构

**❌ 错误示例**：
```c
typedef struct {
    char *name;
    uint8_t *data;
} packet_t;

void process(void) {
    packet_t *pkt = malloc(sizeof(packet_t));
    pkt->name = malloc(64);
    pkt->data = malloc(1024);
    
    // ...
    free(pkt);                  // 🔴 先释放外层，内层泄漏！
}
```

**✅ 正确示例**：
```c
void process(void) {
    packet_t *pkt = malloc(sizeof(packet_t));
    pkt->name = malloc(64);
    pkt->data = malloc(1024);
    
    // ✅ 逆序释放
    free(pkt->data);
    free(pkt->name);
    free(pkt);
}

// ✅ 封装释放函数
void packet_free(packet_t *pkt) {
    if (pkt == NULL) return;
    if (pkt->data != NULL) free(pkt->data);
    if (pkt->name != NULL) free(pkt->name);
    free(pkt);
}
```

---

### 10. 【中危】`realloc` 使用不当
**问题原理**：
- `realloc` 失败时返回 `NULL`，但原内存仍然有效
- 直接赋值会丢失原指针

**❌ 错误示例**：
```c
char *buffer = malloc(512);
// ...
buffer = realloc(buffer, 1024);  // 🔴 失败时 buffer = NULL，原内存泄漏！
```

**✅ 正确示例**：
```c
char *buffer = malloc(512);
// ...
char *new_buf = realloc(buffer, 1024);
if (new_buf == NULL) {
    // realloc 失败，原 buffer 仍然有效
    free(buffer);               // ✅ 手动释放
    return;
}
buffer = new_buf;               // ✅ 成功后才更新
```

---

### 11. 【低危】内存分配失败未记录日志
**问题原理**：
- 分配失败可能是系统即将崩溃的征兆
- 不记录日志无法排查

**✅ 正确示例**：
```c
char *buffer = malloc(1024);
if (buffer == NULL) {
    ESP_LOGE(TAG, "Failed to allocate 1024 bytes, free heap: %d", 
             esp_get_free_heap_size());
    return;
}
```

---

### 12. 【低危】未检查堆剩余空间
**问题原理**：
- 嵌入式系统堆空间有限
- 大量分配前应检查剩余空间

**✅ 正确示例**：
```c
// ESP32
size_t free_heap = esp_get_free_heap_size();
if (free_heap < 10240) {
    ESP_LOGW(TAG, "Low memory: %d bytes", free_heap);
}

// FreeRTOS
size_t free_heap = xPortGetFreeHeapSize();
if (free_heap < 10240) {
    log_warning("Low memory");
}
```

---

## 强制规范

### 内存分配原则
1. **分配后立即判空**，未判空禁止合入
2. **成对出现**：一次分配对应一次释放
3. **释放后置 `NULL`**：`free(ptr); ptr = NULL;`
4. **优先级**：栈内存 > 静态内存 > 动态内存
5. **禁止场景**：非 UI 线程里碰控件、高频函数（每秒调用 >100 次）、窗口过程里每帧分配

### 内存释放原则
1. **统一释放路径**：使用 `goto cleanup` 模式
2. **逆序释放**：嵌套结构先释放内部，再释放外部
3. **异常分支必须释放**：所有 `return` 前都要检查
4. **全局指针显式初始化**：`char *ptr = NULL;`

### 内存池使用场景
- 循环内频繁分配/释放
- 固定大小的对象（如数据包、消息）
- 实时性要求高的场合

---

## 输出格式
【风险等级】致命/高/中/低
【位置】文件名:行号:函数名
【问题类型】泄漏/野指针/碎片/Double Free/Use After Free
【代码片段】
【问题原理】为什么会导致内存泄漏/损坏
【修复方案 + 正确示例】
【替代方案】（如使用内存池、静态内存）
