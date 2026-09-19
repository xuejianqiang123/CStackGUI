---
name: interrupt_check
description: 审查中断服务函数安全性、NVIC优先级配置、中断标志清除、共享变量保护、中断嵌套风险。
auto_invoke: true
allowed-tools: [ReadFile, GrepSearch]
---
# 技能:中断服务函数与中断安全审查

## 触发条件
识别到以下场景自动执行:
- 中断服务函数(ISR / IRAM_ATTR)
- NVIC 优先级配置
- 中断标志清除
- 中断中调用函数
- 中断嵌套
- 全局变量在中断与主循环共享

## ⚠️ 核心原则(对应 AI 评审第 2 层:中断与并发安全)
- **中断必须极简**:禁止延时、循环、阻塞、大量日志
- **共享变量必须 volatile**:中断与主循环共享的变量
- **RTOS 接口必须 FromISR**:在 FreeRTOS 中断中调用 API
- **原子操作或关中断**:多字节变量的读写

## 🔍 完整检查清单

### 🔴 第 2 层:中断与并发安全(死机级)

#### 1. **中断中死循环/长延时(致命)**
- **问题**:中断中是否有 while(1)、for 无限循环、延时函数?
- **后果**:系统卡死、看门狗复位、RTOS 调度失效

**错误示例**:
```c
// ❌ 致命:中断中死循环
void IRAM_ATTR uart_isr_handler(void *arg) {
    while (uart_ll_get_rxfifo_len(uart) == 0) {  // 死等
        // 卡在这里，系统死机
    }
    uint8_t byte = uart_ll_read_rxfifo(uart);
}

// ❌ 致命:中断中延时
void TIM2_IRQHandler(void) {
    HAL_Delay(10);  // 延时会阻塞整个系统
    __HAL_TIM_CLEAR_IT(&htim2, TIM_IT_UPDATE);
}

// ❌ 致命:中断中打印大量日志
void gpio_isr_handler(void *arg) {
    printf("GPIO triggered: %d\n", gpio_num);  // printf 很慢
    for (int i = 0; i < 100; i++) {
        printf("Data[%d] = %d\n", i, data[i]);  // 卡死
    }
}
```

**正确示例**:
```c
// ✅ 正确:中断中只做最少操作
volatile bool data_ready = false;

void IRAM_ATTR uart_isr_handler(void *arg) {
    if (uart_ll_get_rxfifo_len(uart) > 0) {
        rx_byte = uart_ll_read_rxfifo(uart);
        data_ready = true;  // 设置标志，主循环处理
    }
    uart_clear_intr_status(UART_NUM, UART_RXFIFO_FULL_INT_CLR);
}

// 主循环处理实际逻辑
void app_main(void) {
    while (1) {
        if (data_ready) {
            data_ready = false;
            process_data(rx_byte);  // 耗时操作放主循环
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

---

#### 2. **RTOS 中断中调用非 FromISR 接口(致命)**
- **问题**:在 FreeRTOS 中断中是否调用了非 FromISR 版本的 API?
- **后果**:系统崩溃、优先级错乱

**错误示例**:
```c
// ❌ 致命:中断中调用非 ISR 接口
void IRAM_ATTR timer_isr(void *arg) {
    xSemaphoreGive(sem);  // 错误!应该用 xSemaphoreGiveFromISR
    xQueueSend(queue, &data, 0);  // 错误!应该用 xQueueSendFromISR
}
```

**正确示例**:
```c
// ✅ 正确:使用 FromISR 接口
void IRAM_ATTR timer_isr(void *arg) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    xSemaphoreGiveFromISR(sem, &xHigherPriorityTaskWoken);
    xQueueSendFromISR(queue, &data, &xHigherPriorityTaskWoken);
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();  // 触发任务切换
    }
}
```

---

#### 3. **中断标志未清除导致反复触发(致命)**
- **问题**:中断服务函数退出前是否清除了中断标志?
- **后果**:中断风暴、系统卡死

**错误示例**:
```c
// ❌ 致命:忘记清除中断标志
void EXTI0_IRQHandler(void) {
    // 处理中断
    process_gpio_event();
    // 忘记清除标志，导致反复触发
}
```

**正确示例**:
```c
// ✅ 正确:及时清除中断标志
void EXTI0_IRQHandler(void) {
    if (__HAL_GPIO_EXTI_GET_IT(GPIO_PIN_0) != RESET) {
        __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0);  // 先清除标志
        process_gpio_event();
    }
}

// ✅ ESP32 示例
void IRAM_ATTR uart_isr_handler(void *arg) {
    uint32_t status = uart_ll_get_intsts_mask(uart);
    
    if (status & UART_RXFIFO_FULL_INT_ST) {
        // 处理接收
    }
    
    uart_ll_clr_intsts_mask(uart, status);  // 清除所有触发的标志
}
```

---

#### 4. **共享变量未加 volatile(高危)**
- **问题**:中断与主循环共享的变量是否声明为 volatile?
- **后果**:编译器优化导致数据不同步

**错误示例**:
```c
// ❌ 高危:缺少 volatile
bool data_ready = false;  // 编译器可能缓存到寄存器

void IRAM_ATTR isr_handler(void) {
    data_ready = true;  // 可能不会立即写入内存
}

void app_main(void) {
    while (!data_ready) {  // 可能读取缓存的旧值，永远循环
        // 等待
    }
}
```

**正确示例**:
```c
// ✅ 正确:使用 volatile
volatile bool data_ready = false;

void IRAM_ATTR isr_handler(void) {
    data_ready = true;  // 强制写入内存
}

void app_main(void) {
    while (!data_ready) {  // 每次都从内存读取
        vTaskDelay(1);
    }
}
```

---

#### 5. **多字节变量读写非原子(高危)**
- **问题**:中断与主循环读写 uint16_t/uint32_t 是否做了保护?
- **后果**:读到部分更新的值

**错误示例**:
```c
// ❌ 高危:多字节变量非原子操作
volatile uint32_t counter = 0;

void IRAM_ATTR isr_handler(void) {
    counter++;  // 非原子:读-改-写
}

void app_main(void) {
    uint32_t snapshot = counter;  // 可能读到中间值
}
```

**正确示例**:
```c
// ✅ 正确:关中断保护
volatile uint32_t counter = 0;

uint32_t get_counter_safe(void) {
    portENTER_CRITICAL(&spinlock);  // 或 taskENTER_CRITICAL()
    uint32_t val = counter;
    portEXIT_CRITICAL(&spinlock);
    return val;
}

// ✅ 或使用原子操作(ESP32/STM32)
#include <stdatomic.h>
atomic_uint_fast32_t counter = 0;

void IRAM_ATTR isr_handler(void) {
    atomic_fetch_add(&counter, 1);
}
```

---

#### 6. **中断优先级配置错误(致命)**
- **问题**:高频中断是否配置了更高优先级?
- **问题**:嵌套中断是否会导致栈溢出?

**错误示例**:
```c
// ❌ 错误:高频 UART 中断优先级低于低频定时器
HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);  // UART 优先级 5
HAL_NVIC_SetPriority(TIM2_IRQn, 2, 0);    // Timer 优先级 2(更高)
// UART 中断可能被 Timer 打断，导致数据丢失
```

**正确示例**:
```c
// ✅ 正确:高频/紧急中断配置更高优先级(数字越小优先级越高)
HAL_NVIC_SetPriority(USART1_IRQn, 2, 0);  // UART 优先级 2(高)
HAL_NVIC_SetPriority(TIM2_IRQn, 5, 0);    // Timer 优先级 5(低)

// ESP32 示例
esp_intr_alloc(ETS_UART0_INTR_SOURCE, 
               ESP_INTR_FLAG_LEVEL3 | ESP_INTR_FLAG_IRAM,  // 高优先级
               uart_isr, NULL, &uart_intr_handle);
```

---

### 🟡 第 2 层:性能与实时性

#### 7. **中断函数过长影响实时性(中危)**
- **建议**:中断执行时间 < 50us，最多不超过 100us

**检查方法**:
- 中断中是否有复杂计算、循环、多次函数调用?
- 是否可以将耗时操作移到主循环或任务中?

---

## 强制规范

1. **中断必须极简(< 50us)**
2. **禁止在中断中**:
   - 死循环、延时函数(HAL_Delay/vTaskDelay)
   - 阻塞函数(printf/scanf/malloc/free)
   - 大量日志输出
3. **共享变量必须 volatile**
4. **RTOS 中断必须用 FromISR 接口**
5. **多字节变量必须关中断保护**
6. **中断标志必须及时清除**
7. **高频中断配置更高优先级**

---

## 输出格式
【风险等级】致命/高/中/低
【位置】文件名:行号
【问题类型】中断阻塞/优先级错误/标志未清除/共享变量/RTOS接口错误
【错误代码片段】
【风险说明】
【修复方案 + 正确示例】
