---
name: firmware_code_style
description: 统一嵌入式C代码规范、命名、注释、函数分层、模块风格，自动整改不规范代码。
auto_invoke: true
allowed-tools: [ReadFile, GrepSearch]
---
# 技能：嵌入式 C 编码规范审查（专家级）

## 触发条件
识别到以下场景自动执行：
- 新增或修改 `.c`/`.h` 文件
- 函数定义、变量声明
- 宏定义、结构体定义
- 代码注释、文件头
- 模块划分、分层设计

---

## 完整检查清单（15 项规范）

### 1. 命名规范

#### 1.1 变量命名
**规则**：
- 局部变量：小写 + 下划线（`snake_case`）
- 全局变量：`g_` 前缀 + 小写下划线
- 静态变量：`s_` 前缀 + 小写下划线
- 常量：全大写 + 下划线
- 禁止拼音、禁止单字母（除循环变量 `i`、`j`、`k`）

**❌ 错误示例**：
```c
int SensorValue;         // 🔴 驼峰命名
int wendu;               // 🔴 拼音
int sv;                  // 🔴 随意缩写，无法理解
int a, b, c;             // 🔴 无意义命名
```

**✅ 正确示例**：
```c
int sensor_value;        // ✅ 小写下划线
int g_temperature;       // ✅ 全局变量加 g_ 前缀
static int s_retry_count;// ✅ 静态变量加 s_ 前缀
const int MAX_BUFFER_SIZE = 1024;  // ✅ 常量全大写
```

---

#### 1.2 函数命名
**规则**：
- 格式：`模块_对象_动作`（如 `uart_buffer_clear`）
- 小写 + 下划线
- 动词在最后（`init`、`start`、`stop`、`get`、`set`、`clear`）
- 静态函数可省略模块前缀

**❌ 错误示例**：
```c
void InitSensor(void);   // 🔴 驼峰命名
void sensor_csh(void);   // 🔴 缩写无法理解
void do_it(void);        // 🔴 无意义命名
void GetData(void);      // 🔴 驼峰 + 动词在前
```

**✅ 正确示例**：
```c
void sensor_init(void);          // ✅ 模块_动作
void uart_buffer_clear(void);    // ✅ 模块_对象_动作
void wifi_status_get(void);      // ✅ 模块_对象_动作
static void parse_frame(void);   // ✅ 静态函数可省略模块前缀
```

---

#### 1.3 宏定义命名
**规则**：
- 全大写 + 下划线
- 宏函数参数加括号保护
- 使用 `do { ... } while(0)` 包裹多语句宏

**❌ 错误示例**：
```c
#define max_size 1024            // 🔴 未全大写
#define ADD(a, b) a + b          // 🔴 缺少括号保护
#define LOG(msg) printf(msg); log_to_file(msg)  // 🔴 多语句未保护
```

**✅ 正确示例**：
```c
#define MAX_SIZE 1024                    // ✅ 全大写
#define ADD(a, b) ((a) + (b))            // ✅ 参数加括号
#define LOG(msg) do {                    \
    printf(msg);                         \
    log_to_file(msg);                    \
} while(0)                               // ✅ 多语句用 do-while 包裹
```

---

#### 1.4 类型定义命名
**规则**：
- 结构体：`小写_t` 后缀
- 枚举：`小写_e` 后缀
- 联合体：`小写_u` 后缀
- 避免匿名结构体（除非配合 `typedef`）

**❌ 错误示例**：
```c
struct SensorData {      // 🔴 驼峰命名
    int value;
};

typedef enum {           // 🔴 匿名枚举，无法前向声明
    STATE_IDLE,
    STATE_BUSY
} state;                 // 🔴 类型名无后缀
```

**✅ 正确示例**：
```c
typedef struct {
    int value;
    uint32_t timestamp;
} sensor_data_t;         // ✅ 结构体加 _t 后缀

typedef enum {
    STATE_IDLE,
    STATE_BUSY,
    STATE_ERROR
} state_e;               // ✅ 枚举加 _e 后缀
```

---

### 2. 代码结构规范

#### 2.1 函数长度限制
**规则**：
- 单个函数不超过 100 行（不含注释和空行）
- 超过 50 行建议拆分
- 一个函数只做一件事

**❌ 错误示例**：
```c
void process_sensor_data(void) {
    // 🔴 300 行的超长函数
    // 初始化传感器
    // 读取数据
    // 数据处理
    // 协议封装
    // 网络发送
    // 日志记录
    // 错误处理
    // ...（200 多行）
}
```

**✅ 正确示例**：
```c
void process_sensor_data(void) {
    sensor_data_t data;
    
    if (!sensor_read(&data)) {
        return;
    }
    
    data_normalize(&data);
    protocol_pack_and_send(&data);
    log_sensor_data(&data);
}

static bool sensor_read(sensor_data_t *data) {
    // 只负责读取传感器
}

static void data_normalize(sensor_data_t *data) {
    // 只负责数据处理
}

static void protocol_pack_and_send(sensor_data_t *data) {
    // 只负责封装和发送
}
```

---

#### 2.2 分层设计
**规则**：
- 硬件抽象层（HAL）：直接操作寄存器
- 驱动层（Driver）：封装设备操作
- 业务层（App）：实现业务逻辑
- 严格单向依赖：App → Driver → HAL

**❌ 错误示例**：
```c
// 🔴 业务逻辑直接操作寄存器
void send_message(const char *msg) {
    while (*msg) {
        USART1->DR = *msg++;  // 🔴 业务层直接访问寄存器
        while (!(USART1->SR & USART_SR_TXE));
    }
}
```

**✅ 正确示例**：
```c
// HAL 层：uart_hal.c
void uart_send_byte(uint8_t byte) {
    USART1->DR = byte;
    while (!(USART1->SR & USART_SR_TXE));
}

// Driver 层：uart_driver.c
void uart_send_string(const char *str) {
    while (*str) {
        uart_send_byte(*str++);
    }
}

// App 层：app_main.c
void send_message(const char *msg) {
    uart_send_string(msg);  // ✅ 业务层只调用驱动接口
}
```

---

#### 2.3 头文件保护
**规则**：
- 所有头文件必须有防重复包含保护
- 使用 `#ifndef` + `#define` + `#endif`
- 宏名格式：`__文件名_H__`（全大写）

**❌ 错误示例**：
```c
// sensor.h
// 🔴 无防重复包含保护
void sensor_init(void);
int sensor_read(void);
```

**✅ 正确示例**：
```c
// sensor.h
#ifndef __SENSOR_H__
#define __SENSOR_H__

void sensor_init(void);
int sensor_read(void);

#endif /* __SENSOR_H__ */
```

---

### 3. 注释规范

#### 3.1 文件头注释
**规则**：
- 每个文件必须有文件头注释
- 包含：文件描述、作者、日期、版权

**✅ 正确示例**：
```c
/**
 * @file    sensor_driver.c
 * @brief   温湿度传感器驱动
 * @details 支持 DHT11/DHT22 传感器，提供温度/湿度读取接口
 * @author  张三
 * @date    2026-08-23
 * @version 1.0
 * @copyright Copyright (c) 2026 XXX公司
 */
```

---

#### 3.2 函数注释
**规则**：
- 公共函数必须有注释
- 包含：功能描述、参数、返回值、注意事项

**✅ 正确示例**：
```c
/**
 * @brief  读取传感器数据
 * @param  data 输出参数，存储读取到的传感器数据
 * @retval 0 成功
 * @retval -1 失败（传感器无响应）
 * @note   调用前必须先调用 sensor_init()
 * @note   读取耗时约 20ms，期间会阻塞
 */
int sensor_read(sensor_data_t *data);
```

---

#### 3.3 关键代码注释
**规则**：
- 硬件操作必须注释（寄存器配置、时序要求）
- 复杂算法必须注释（公式、参考文档）
- 魔术数字必须注释（为什么是这个值）

**❌ 错误示例**：
```c
void delay_us(uint32_t us) {
    us *= 72;            // 🔴 魔术数字，不知道为什么是 72
    while (us--);
}
```

**✅ 正确示例**：
```c
void delay_us(uint32_t us) {
    // 系统时钟 72MHz，每个循环约 3 个时钟周期
    // 1μs = 72 个时钟 ≈ 24 次循环
    us *= 24;
    while (us--);
}

// ✅ 硬件时序注释
void i2c_start(void) {
    SCL_HIGH();
    SDA_HIGH();
    delay_us(5);         // 数据手册要求：Start 前 SCL/SDA 必须保持高电平 ≥4.7μs
    SDA_LOW();
    delay_us(5);         // Start 条件：SDA 下降沿时 SCL 保持高电平 ≥4μs
    SCL_LOW();
}
```

---

### 4. 格式规范

#### 4.1 缩进与空格
**规则**：
- 使用 4 个空格缩进（禁止 Tab）
- 运算符两侧加空格
- 逗号后加空格
- `if`/`for`/`while` 与括号之间加空格

**❌ 错误示例**：
```c
if(a==b){                // 🔴 缺少空格
    c=a+b;               // 🔴 运算符无空格
    func(a,b,c);         // 🔴 逗号后无空格
}
```

**✅ 正确示例**：
```c
if (a == b) {            // ✅ if 与括号有空格，运算符有空格
    c = a + b;           // ✅ 赋值和运算符有空格
    func(a, b, c);       // ✅ 逗号后有空格
}
```

---

#### 4.2 大括号风格
**规则**：
- K&R 风格：函数定义的 `{` 另起一行，其他 `{` 跟在语句后
- 所有 `if`/`for`/`while` 必须加大括号（即使只有一行）

**❌ 错误示例**：
```c
void func(void) {
    if (a > 0)
        a = 0;           // 🔴 单行也必须加大括号
}
```

**✅ 正确示例**：
```c
void func(void)
{                        // ✅ 函数定义的 { 另起一行
    if (a > 0) {         // ✅ 单行也加大括号
        a = 0;
    }
    
    for (int i = 0; i < 10; i++) {
        process(i);
    }
}
```

---

### 5. 安全规范

#### 5.1 指针使用
**规则**：
- 指针使用前必须判空
- 指针释放后立即置 `NULL`
- 避免野指针

**❌ 错误示例**：
```c
void process(int *ptr) {
    *ptr = 100;          // 🔴 未判空直接解引用
}

void free_buffer(char **buf) {
    free(*buf);          // 🔴 释放后未置 NULL
}
```

**✅ 正确示例**：
```c
void process(int *ptr) {
    if (ptr == NULL) {   // ✅ 先判空
        return;
    }
    *ptr = 100;
}

void free_buffer(char **buf) {
    if (*buf != NULL) {
        free(*buf);
        *buf = NULL;     // ✅ 释放后置 NULL
    }
}
```

---

#### 5.2 数组边界检查
**规则**：
- 访问数组前必须检查索引范围
- 使用 `sizeof(array)/sizeof(array[0])` 获取数组长度

**❌ 错误示例**：
```c
int values[10];
void set_value(int index, int value) {
    values[index] = value;  // 🔴 未检查边界
}
```

**✅ 正确示例**：
```c
int values[10];
void set_value(int index, int value) {
    if (index < 0 || index >= sizeof(values)/sizeof(values[0])) {
        return;          // ✅ 边界检查
    }
    values[index] = value;
}
```

---

#### 5.3 返回值检查
**规则**：
- 所有可能失败的函数必须检查返回值
- 使用 `if (ret != 0)` 而非 `if (!ret)`（更清晰）

**❌ 错误示例**：
```c
void init_system(void) {
    i2c_init();          // 🔴 未检查返回值
    sensor_init();       // 🔴 未检查返回值
}
```

**✅ 正确示例**：
```c
void init_system(void) {
    if (i2c_init() != 0) {
        log_error("I2C init failed");
        return;
    }
    
    if (sensor_init() != 0) {
        log_error("Sensor init failed");
        return;
    }
}
```

---

### 6. 模块化规范

#### 6.1 头文件包含顺序
**规则**：
1. 标准库头文件（`<stdio.h>`、`<stdint.h>`）
2. 第三方库头文件（`<FreeRTOS.h>`、`<esp_log.h>`）
3. 项目公共头文件（`"common.h"`、`"config.h"`）
4. 本模块头文件（`"sensor.h"`）

**✅ 正确示例**：
```c
// 标准库
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// 第三方库
#include <FreeRTOS.h>
#include <task.h>
#include <esp_log.h>

// 项目公共头文件
#include "common.h"
#include "config.h"

// 本模块头文件
#include "sensor.h"
```

---

#### 6.2 函数声明与定义分离
**规则**：
- `.h` 文件：声明、宏定义、类型定义
- `.c` 文件：实现、静态函数、静态变量

**❌ 错误示例**：
```c
// sensor.h
void sensor_init(void) {     // 🔴 头文件中实现函数
    // ...
}
```

**✅ 正确示例**：
```c
// sensor.h
void sensor_init(void);      // ✅ 只声明

// sensor.c
void sensor_init(void) {     // ✅ 实现放在 .c 文件
    // ...
}
```

---

### 7. 魔术数字规范

**规则**：
- 禁止直接使用数字常量（除 0、1、-1）
- 使用宏或枚举定义

**❌ 错误示例**：
```c
if (status == 2) {           // 🔴 魔术数字，不知道 2 代表什么
    retry_count = 5;         // 🔴 魔术数字，为什么是 5？
}
```

**✅ 正确示例**：
```c
#define STATUS_READY 2
#define MAX_RETRY_COUNT 5

if (status == STATUS_READY) {
    retry_count = MAX_RETRY_COUNT;
}

// 或使用枚举
typedef enum {
    STATUS_IDLE = 0,
    STATUS_BUSY = 1,
    STATUS_READY = 2,
    STATUS_ERROR = 3
} status_e;
```

---

### 8. `goto` 使用规范

**规则**：
- 仅用于统一错误处理和资源清理
- 禁止向上跳转（避免循环）
- 标签名清晰（`cleanup`、`error_exit`）

**❌ 错误示例**：
```c
retry:                       // 🔴 向上跳转，形成循环
if (connect_failed) {
    goto retry;              // 🔴 禁止！
}
```

**✅ 正确示例**：
```c
int process_file(const char *path) {
    FILE *fp = NULL;
    char *buffer = NULL;
    int ret = -1;
    
    fp = fopen(path, "r");
    if (fp == NULL) {
        goto cleanup;        // ✅ 统一清理路径
    }
    
    buffer = malloc(1024);
    if (buffer == NULL) {
        goto cleanup;
    }
    
    if (fread(buffer, 1, 1024, fp) < 0) {
        goto cleanup;
    }
    
    ret = 0;                 // 成功
    
cleanup:
    if (buffer != NULL) free(buffer);
    if (fp != NULL) fclose(fp);
    return ret;
}
```

---

### 9. 全局变量使用规范

**规则**：
- 尽量避免全局变量
- 必须使用时加 `g_` 前缀
- 模块内全局变量使用 `static` 限制作用域

**❌ 错误示例**：
```c
int sensor_value;            // 🔴 全局变量无前缀
int retry_count;             // 🔴 容易命名冲突
```

**✅ 正确示例**：
```c
int g_sensor_value;          // ✅ 全局变量加 g_ 前缀
static int s_retry_count;    // ✅ 模块内全局用 static
```

---

### 10. 错误码定义规范

**规则**：
- 使用枚举定义错误码
- 0 表示成功，负数表示错误
- 错误码有明确含义

**✅ 正确示例**：
```c
typedef enum {
    ERR_OK = 0,              // 成功
    ERR_TIMEOUT = -1,        // 超时
    ERR_NO_MEMORY = -2,      // 内存不足
    ERR_INVALID_PARAM = -3,  // 参数错误
    ERR_NOT_READY = -4,      // 设备未就绪
} error_code_e;
```

---

## 强制规范总结

### 命名规范
- 变量：`snake_case`，全局加 `g_`，静态加 `s_`
- 函数：`模块_对象_动作`
- 宏：全大写
- 类型：加 `_t`/`_e`/`_u` 后缀

### 代码结构
- 函数 < 100 行
- 分层设计：App → Driver → HAL
- 头文件必须防重复包含

### 注释规范
- 文件头注释必须有
- 公共函数必须注释
- 硬件操作必须注释
- 魔术数字必须注释

### 安全规范
- 指针使用前判空
- 数组访问检查边界
- 函数返回值必须检查
- 避免魔术数字

---

## 输出格式
【风险等级】规范/建议/低危
【位置】文件名:行号
【问题类型】命名不规范/函数过长/缺少注释/安全隐患
【错误代码片段】
【规范要求】
【修复方案 + 正确示例】
【最佳实践建议】
