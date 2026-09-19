---
name: struct_best_practice_check
description: 审查结构体设计规范、内存优化、对齐问题、寄存器映射、函数指针架构、柔性数组、位域使用等 18 种高级用法的正确性与最佳实践。
auto_invoke: true
allowed-tools: [ReadFile, GrepSearch]
---
# 技能：结构体设计与最佳实践审查

## 触发条件
识别到以下场景自动执行：
- 结构体定义与使用
- 寄存器映射结构体
- 函数指针结构体（面向对象封装）
- 联合体（union）与结构体组合
- 柔性数组（flexible array）
- 结构体位域（bit field）
- 结构体对齐（pack/align）

## 18 种结构体用法检查清单

### 1. 寄存器映射结构体
**检查要点**：
- 【致命】缺少 `volatile` 关键字导致编译器优化失效
- 【致命】对齐方式与 MCU 不匹配（32位MCU需4字节对齐）
- 【高危】保留位未占位导致偏移错误
- 【高危】寄存器顺序与数据手册不一致

**正确示例**：
```c
#pragma pack(push, 4)
typedef struct {
    volatile uint32_t CR1;    // 控制寄存器1
    volatile uint32_t CR2;    // 控制寄存器2
    volatile uint32_t SR;     // 状态寄存器
    volatile uint32_t DR;     // 数据寄存器
} UART_TypeDef;
#pragma pack(pop)

#define USART1 ((UART_TypeDef *)0x40004400UL)
```

---

### 2. 函数指针结构体（驱动解耦）
**检查要点**：
- 【高危】函数指针表未加 `const` 浪费 RAM
- 【中危】缺少私有数据指针（priv_data）无法隔离配置
- 【中危】函数指针未判空直接调用
- 【低危】未实现统一的设备操作接口

**正确示例**：
```c
typedef struct {
    int (*dev_init)(struct device *dev);
    int (*dev_read)(struct device *dev, void *buf, size_t len);
    void (*dev_ctrl)(struct device *dev, uint8_t cmd);
} dev_ops_t;

typedef struct device {
    const char *dev_name;
    const dev_ops_t *ops;      // const 存入 Flash
    void *priv_data;           // 私有配置数据
    uint8_t dev_status;
} device_t;
```

---

### 3. 结构体状态机
**检查要点**：
- 【高危】状态迁移逻辑混乱，缺少状态表
- 【中危】状态机未记录时间戳和计数器
- 【中危】触发事件未封装
- 【低危】用 switch-case 而非表驱动

**正确示例**：
```c
typedef struct {
    state_t cur_state;
    uint32_t state_tick;       // 状态计时
    uint8_t retry_times;       // 重试计数
    uint8_t trigger_event;     // 触发事件
} fsm_t;

typedef struct {
    state_t from;
    uint8_t event;
    state_t to;
    void (*action)(fsm_t *fsm);
} state_trans_t;
```

---

### 4. 配置表结构体（表驱动）
**检查要点**：
- 【高危】配置表未加 `const` 浪费 RAM
- 【中危】配置分散在代码中未集中管理
- 【低危】新增配置需要大量修改代码

**正确示例**：
```c
typedef struct {
    GPIO_TypeDef *gpio_port;
    uint16_t gpio_pin;
    uint8_t active_level;
    const char *led_name;
} led_cfg_t;

// const 存入 Flash，节省 RAM
static const led_cfg_t led_table[] = {
    {GPIOA, GPIO_PIN_0, 1, "POWER_LED"},
    {GPIOB, GPIO_PIN_5, 0, "RUN_LED"},
};
```

---

### 5. 柔性数组（Flexible Array）
**检查要点**：
- 【致命】柔性数组未放在结构体末尾
- 【致命】直接赋值拷贝含柔性数组的结构体
- 【高危】忘记分配变长部分内存
- 【高危】释放时产生内存碎片

**正确示例**：
```c
typedef struct {
    uint8_t cmd_type;
    uint16_t data_len;
    uint32_t recv_time;
    uint8_t data[];            // 必须放最后
} uart_msg_t;

// 一次性申请头部+变长数据
uart_msg_t *msg = malloc(sizeof(uart_msg_t) + len);
memcpy(msg->data, buf, len);
free(msg);  // 一次性释放，零碎片
```

---

### 6. 柔性数组多类型消息
**检查要点**：
- 【高危】消息类型未统一封装
- 【中危】RTOS 队列传递结构体大小不一致

**正确示例**：
```c
typedef struct {
    uint8_t msg_type;
    uint16_t msg_id;
    uint16_t data_len;
    uint8_t data[];
} msg_frame_t;
```

---

### 7. 侵入式链表（Intrusive List）
**检查要点**：
- 【高危】使用传统链表导致内存重复
- 【中危】未使用 container_of 宏反推结构体首地址
- 【低危】不了解 Linux/FreeRTOS 内核常用写法

**正确示例**：
```c
typedef struct list_node {
    struct list_node *next;
} list_node_t;

typedef struct {
    uint8_t dev_id;
    uint32_t online_time;
    list_node_t node;          // 嵌入链表节点
} device_info_t;

#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
```

---

### 8. 紧凑结构体（协议解析）
**检查要点**：
- 【致命】未开启 1 字节对齐导致解析错误
- 【高危】跨设备通信未处理大小端
- 【中危】协议字段与结构体顺序不匹配

**正确示例**：
```c
#pragma pack(push, 1)
typedef struct {
    uint8_t head;
    uint8_t dev_addr;
    uint16_t cmd_code;
    uint8_t data_len;
    uint8_t data[8];
    uint8_t crc8;
} uart_frame_t;
#pragma pack(pop)

// 缓冲区直接拷贝解析
memcpy(&frame, buf, sizeof(frame));
```

---

### 9. 结构体位域（Bit Field）
**检查要点**：
- 【致命】位域变量取地址（不可取址）
- 【高危】跨平台位域排布差异未考虑
- 【中危】位域类型不当导致符号扩展
- 【低危】不同编译器行为差异

**正确示例**：
```c
typedef struct {
    uint8_t online   : 1;
    uint8_t error    : 1;
    uint8_t lock     : 1;
    uint8_t reserve  : 5;
} dev_status_bit_t;

// 注意：位域不能取地址！
// 错误：&status.online
```

---

### 10. 嵌套结构体（公共字段继承）
**检查要点**：
- 【中危】公共字段未抽离导致代码重复
- 【低危】未使用嵌套结构体模拟继承

**正确示例**：
```c
typedef struct {
    uint8_t dev_id;
    uint32_t time_stamp;
    uint8_t status;
} msg_base_t;

typedef struct {
    msg_base_t base;           // 继承公共字段
    uint16_t voltage;
    uint8_t percent;
} battery_msg_t;
```

---

### 11. UNION+STRUCT 组合（寄存器解析）
**检查要点**：
- 【高危】未使用 union 导致寄存器操作繁琐
- 【中危】位域与寄存器整体读写未统一

**正确示例**：
```c
typedef union {
    uint16_t reg_val;          // 整体读写
    struct {
        uint16_t conv_flag : 1;
        uint16_t over_flow : 1;
        uint16_t ready     : 1;
        uint16_t reserve   : 13;
    } bit;                     // 单 bit 精准判断
} adc_reg_t;

// 使用
adc_sr.reg_val = ADC1->SR;     // 整体读取
if(adc_sr.bit.ready) {...}     // 精准判断
```

---

### 12. 联合体嵌套结构体（异构外设内存复用）
**检查要点**：
- 【高危】异构外设配置未复用内存
- 【中危】配置字段冗余浪费 RAM

**正确示例**：
```c
typedef union {
    struct {
        uint32_t clock_speed;
        uint16_t slave_addr;
        uint8_t  addr_mode;
    } i2c_cfg;
    
    struct {
        uint32_t clock_speed;
        uint8_t  mode;
        uint8_t  bit_order;
    } spi_cfg;
} periph_cfg_union_t;

typedef struct {
    const char* dev_name;
    uint32_t dev_type;
    periph_cfg_union_t cfg;    // 内存复用
} device_t;
```

---

### 13. CONST 常量结构体（RAM 优化）
**检查要点**：
- 【致命】只读配置未加 `const` 浪费 RAM
- 【高危】字符串指针未指向 Flash 常量区
- 【中危】小 MCU 内存紧张未优化

**正确示例**：
```c
typedef struct {
    uint8_t i2c_addr;
    uint16_t sample_rate;
    const char *product_name;
} dev_config_t;

// const 存入 Flash，零 RAM 占用
static const dev_config_t sensor_cfg = {
    .i2c_addr = 0x38,
    .sample_rate = 10,
    .product_name = "HT_SENSOR_V1.0"
};
```

---

### 14. 结构体指针数组（多设备管理）
**检查要点**：
- 【中危】多设备未用数组统一管理
- 【低危】初始化代码重复冗余

**正确示例**：
```c
device_t *dev_list[] = {
    &bat_dev,
    &temp_dev,
    &light_dev
};

void device_all_init(void) {
    uint32_t num = sizeof(dev_list)/sizeof(device_t *);
    for(uint32_t i=0; i<num; i++) {
        device_init(dev_list[i]);
    }
}
```

---

### 15. C11 匿名结构体
**检查要点**：
- 【中危】嵌套结构体命名冗余
- 【低危】未使用 C11 简化代码

**正确示例**：
```c
typedef struct {
    uint8_t mode;
    struct {                   // 匿名结构体
        uint16_t max_val;
        uint16_t min_val;
        uint8_t alarm_en;
    };
} param_cfg_t;

// 直接访问，无需嵌套
cfg.max_val = 100;
```

---

### 16. 结构体做函数参数
**检查要点**：
- 【高危】多参数传递未封装成结构体
- 【中危】结构体传值而非传指针浪费栈

**正确示例**：
```c
typedef struct {
    uint32_t delay_ms;
    uint8_t repeat;
    void (*task_cb)(void);
    const char *task_name;
} task_cfg_t;

int task_create(const task_cfg_t *cfg);  // 传指针
```

---

### 17. 结构体做返回值（多返回值）
**检查要点**：
- 【中危】多返回值用指针传出而非结构体返回
- 【低危】返回值结构体过大影响性能

**正确示例**：
```c
typedef struct {
    int ret;
    uint16_t vol1;
    uint16_t vol2;
    uint16_t vol3;
} adc_result_t;

adc_result_t adc_get_all_vol(void) {
    adc_result_t res = {0};
    res.vol1 = adc_sample(ADC_CH1);
    res.vol2 = adc_sample(ADC_CH2);
    res.vol3 = adc_sample(ADC_CH3);
    return res;
}
```

---

### 18. 结构体回调钩子（框架解耦）
**检查要点**：
- 【致命】回调函数未判空直接调用
- 【高危】底层驱动与业务逻辑耦合
- 【中危】未使用回调实现解耦

**正确示例**：
```c
typedef struct {
    void (*recv_cb)(uint8_t *data, uint16_t len);
    void (*send_complete)(void);
    void (*error_cb)(uint8_t err_code);
} uart_callback_t;

typedef struct {
    USART_TypeDef *uart;
    uart_callback_t cb;
    uint8_t *buf;
    uint16_t buf_len;
} uart_dev_t;

// 调用前判空
if(dev->cb.recv_cb) {
    dev->cb.recv_cb(data, len);
}
```

---

## 强制规范

### 内存优化
1. 所有只读配置必须加 `const` 存入 Flash
2. 函数指针表必须加 `const`
3. 优先使用柔性数组避免内存碎片
4. 位域精准按 bit 分配节省空间

### 代码架构
1. 函数指针实现驱动解耦（面向对象）
2. 状态机替代混乱 switch-case
3. 配置表实现表驱动编程
4. 回调钩子实现框架解耦

### 硬件操作
1. 寄存器映射必须使用 `volatile`
2. UNION+STRUCT 组合处理寄存器
3. 紧凑结构体协议解析必须 `#pragma pack(1)`
4. 保留位必须占位

### 常见陷阱
1. volatile 不能省，否则编译器优化失效
2. 寄存器保留位必须占位
3. 对齐方式必须匹配 MCU
4. 位域不能取地址
5. 跨平台通信必须处理大小端
6. 柔性数组必须放最后
7. 回调函数调用前必须判空

---

## 输出格式
【风险等级】致命/高/中/低
【位置】文件名:行号
【问题类型】内存优化/架构设计/硬件操作/常见陷阱
【错误代码片段】
【问题原理】
【修复方案 + 正确示例】
【最佳实践建议】
