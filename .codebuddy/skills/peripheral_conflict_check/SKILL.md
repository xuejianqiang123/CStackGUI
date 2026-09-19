---
name: peripheral_conflict_check
description: 检测GPIO复用冲突、外设通道冲突、时钟源冲突、引脚电气特性不匹配等硬件配置问题。
auto_invoke: true
allowed-tools: [ReadFile, GrepSearch]
---
# 技能：外设冲突与硬件配置审查

## ⚠️ 核心原则（对应 AI 评审第 7 层：外设配置与寄存器操作）
- **寄存器必须 volatile**：防止编译器优化掉硬件访问
- **配置必须查数据手册**：GPIO 复用、时钟配置必须符合规格
- **初始化必须完整**：时钟使能 → 外设配置 → 中断配置
- **保留位必须占位**：位域结构体必须用保留字段对齐
- **硬件操作必须检查状态**：等待 Ready 标志、检查错误位

## 触发条件
- GPIO 初始化与复用配置
- SPI/I2C/UART/ADC/PWM 通道分配
- 引脚复用（AF功能、IOMUX）
- 时钟源配置（PLL/APB/AHB）
- 外设 DMA 通道分配
- 寄存器直接操作

---

## 🔍 完整检查清单

### 🔴 第 7 层：外设配置与寄存器操作（硬件级）

#### 1. **寄存器变量缺少 volatile（致命）**
- **问题**：硬件寄存器是否声明为 volatile？
- **后果**：编译器优化掉硬件操作，导致配置失效

**错误示例**：
```c
// ❌ 致命：寄存器未声明 volatile
typedef struct {
    uint32_t CR1;      // 控制寄存器
    uint32_t CR2;
    uint32_t SR;       // 状态寄存器
    uint32_t DR;       // 数据寄存器
} UART_TypeDef;

UART_TypeDef *uart = (UART_TypeDef *)0x40013800;

void uart_send(uint8_t byte) {
    while (!(uart->SR & 0x80));  // 等待 TXE 标志
    uart->DR = byte;
    // 编译器可能优化掉循环，导致数据发送失败
}
```

**正确示例**：
```c
// ✅ 正确：寄存器必须 volatile
typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t SR;
    volatile uint32_t DR;
} UART_TypeDef;

UART_TypeDef *uart = (UART_TypeDef *)0x40013800;

void uart_send(uint8_t byte) {
    while (!(uart->SR & 0x80));  // volatile 确保每次都读取寄存器
    uart->DR = byte;
}

// ✅ 或使用 STM32 HAL 库提供的定义
// USART1->DR = byte;  // HAL 库已正确声明为 volatile
```

---

#### 2. **同一 GPIO 被多个外设复用（致命）**
- **问题**：同一个 GPIO 引脚是否被多个功能占用？
- **后果**：外设功能冲突、通信失败

**错误示例**：
```c
// ❌ 致命：PA9 既配置为 UART TX，又配置为 GPIO 输出
// STM32 配置
void init_peripherals(void) {
    // PA9 配置为 UART1 TX
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // 同时又配置 PA9 为 GPIO 输出（冲突！）
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);  // 覆盖了 UART 配置
}

// ❌ ESP32 示例
void init_esp32(void) {
    // GPIO 17 配置为 UART TX
    uart_set_pin(UART_NUM_1, 17, 16, -1, -1);
    
    // 同时又配置为 GPIO 输出（冲突！）
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << 17),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io_conf);
}
```

**正确示例**：
```c
// ✅ 正确：每个 GPIO 只分配一个功能
void init_peripherals(void) {
    // PA9: UART1 TX
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // PA10: GPIO 输出（换一个引脚）
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

// ✅ 建议：使用引脚分配表管理
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    const char *function;
} pin_assignment_t;

const pin_assignment_t pin_map[] = {
    {GPIOA, GPIO_PIN_9,  "UART1_TX"},
    {GPIOA, GPIO_PIN_10, "UART1_RX"},
    {GPIOA, GPIO_PIN_5,  "SPI1_SCK"},
    {GPIOB, GPIO_PIN_0,  "LED"},
};
```

---

#### 3. **SPI/I2C 引脚配置错误（致命）**
- **问题**：SPI MOSI/MISO 是否互换？I2C 是否配置为开漏？
- **后果**：通信失败、数据错误

**错误示例**：
```c
// ❌ 致命：SPI 引脚配置错误
void spi_init(void) {
    // MOSI 和 MISO 配置反了！
    gpio_set_direction(GPIO_NUM_23, GPIO_MODE_INPUT);   // MOSI 应该是输出
    gpio_set_direction(GPIO_NUM_19, GPIO_MODE_OUTPUT);  // MISO 应该是输入
}

// ❌ 致命：I2C 未配置为开漏
void i2c_init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;  // SCL, SDA
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;  // 错误！应该是开漏模式
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}
```

**正确示例**：
```c
// ✅ 正确：SPI 引脚配置
// ESP32 SPI 标准配置
#define PIN_NUM_MISO 19  // Master In Slave Out（主机输入）
#define PIN_NUM_MOSI 23  // Master Out Slave In（主机输出）
#define PIN_NUM_CLK  18
#define PIN_NUM_CS   5

spi_bus_config_t buscfg = {
    .miso_io_num = PIN_NUM_MISO,  // 输入引脚
    .mosi_io_num = PIN_NUM_MOSI,  // 输出引脚
    .sclk_io_num = PIN_NUM_CLK,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
};

// ✅ 正确：I2C 必须配置为开漏 + 上拉
void i2c_init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;  // 开漏模式
    GPIO_InitStruct.Pull = GPIO_PULLUP;      // 上拉使能
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
}

// ✅ ESP32 I2C 配置
i2c_config_t conf = {
    .mode = I2C_MODE_MASTER,
    .sda_io_num = GPIO_NUM_21,
    .sda_pullup_en = GPIO_PULLUP_ENABLE,  // 上拉使能
    .scl_io_num = GPIO_NUM_22,
    .scl_pullup_en = GPIO_PULLUP_ENABLE,
    .master.clk_speed = 100000,
};
```

---

#### 4. **GPIO 速度等级不足（高危）**
- **问题**：高速 SPI 是否配置了 High Speed GPIO？
- **后果**：信号质量差、通信错误

**错误示例**：
```c
// ❌ 高危：高速 SPI（50MHz）但 GPIO 配置为低速
void spi_init_high_speed(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;  // 低速！
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // SPI 配置为 50MHz
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2; // 50MHz
}
```

**正确示例**：
```c
// ✅ 正确：高速 SPI 必须配置 GPIO 为 High Speed
void spi_init_high_speed(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;  // 高速
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
}

// ✅ GPIO 速度等级规则
// LOW: ≤ 8 MHz（按钮、LED）
// MEDIUM: ≤ 50 MHz（普通 SPI、UART）
// HIGH: ≤ 100 MHz（高速 SPI）
// VERY_HIGH: > 100 MHz（极高速外设）
```

---

#### 5. **ADC 通道与 GPIO 复用冲突（高危）**
- **问题**：ADC 使用的 GPIO 是否同时配置为数字 IO？
- **后果**：ADC 读数不准、GPIO 功能异常

**错误示例**：
```c
// ❌ 高危：PA0 既用作 ADC，又配置为 GPIO 输出
void init_adc_gpio(void) {
    // 配置 ADC 通道 0（PA0）
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = ADC_CHANNEL_0;  // PA0
    sConfig.Rank = 1;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
    
    // 同时配置 PA0 为 GPIO 输出（冲突！）
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}
```

**正确示例**：
```c
// ✅ 正确：ADC 引脚配置为模拟输入，不做其他配置
void init_adc(void) {
    // PA0 配置为模拟输入
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;  // 模拟模式
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // 配置 ADC
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = ADC_CHANNEL_0;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_15CYCLES;
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);
}

// ✅ ADC 引脚规则
// - 必须配置为 GPIO_MODE_ANALOG
// - 不可同时用作数字 IO、PWM、定时器捕获
// - 避免与高频信号引脚相邻（防止串扰）
```

---

#### 6. **外设时钟未使能（致命）**
- **问题**：使用外设前是否使能了对应时钟？
- **后果**：外设无法工作、寄存器访问触发硬件错误

**错误示例**：
```c
// ❌ 致命：直接使用 UART，但未使能时钟
void uart_init_wrong(void) {
    // 忘记使能 UART 时钟
    USART1->CR1 |= USART_CR1_UE;  // 访问未使能的外设，可能触发硬件异常
}
```

**正确示例**：
```c
// ✅ 正确：先使能时钟，再配置外设
void uart_init(void) {
    // 1. 使能 UART 时钟
    __HAL_RCC_USART1_CLK_ENABLE();
    
    // 2. 使能 GPIO 时钟
    __HAL_RCC_GPIOA_CLK_ENABLE();
    
    // 3. 配置 GPIO
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_9 | GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    // 4. 配置 UART
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    HAL_UART_Init(&huart1);
}

// ✅ 初始化顺序规则
// 1. 使能外设时钟（RCC）
// 2. 使能 GPIO 时钟
// 3. 配置 GPIO 复用功能
// 4. 配置外设寄存器
// 5. 使能外设
// 6. 配置中断（如需要）
```

---

#### 7. **时钟源配置错误（致命）**
- **问题**：外设时钟频率是否超出规格？是否选择了错误的时钟源？
- **后果**：通信速率错误、功能异常

**错误示例**：
```c
// ❌ 致命：UART 波特率计算错误
void uart_init_wrong(void) {
    // 系统时钟 80MHz，APB1 时钟 40MHz
    // 期望波特率 115200，但配置错误
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    
    // 忘记检查 APB 时钟频率，导致实际波特率不对
    // 实际波特率 = APB_CLK / (16 * USARTDIV)
    // 如果 APB_CLK 配置错误，波特率会偏差很大
}
```

**正确示例**：
```c
// ✅ 正确：检查时钟频率
void uart_init(void) {
    // 获取 APB 时钟频率
    uint32_t pclk = HAL_RCC_GetPCLK2Freq();
    ESP_LOGI(TAG, "UART APB 时钟: %u Hz", pclk);
    
    // 确认时钟频率符合预期
    assert(pclk == 80000000);  // 预期 80MHz
    
    // 配置 UART
    huart1.Instance = USART1;
    huart1.Init.BaudRate = 115200;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    HAL_UART_Init(&huart1);
    
    // 验证实际波特率
    uint32_t actual_baud = pclk / (16 * USART1->BRR);
    ESP_LOGI(TAG, "实际波特率: %u (期望 115200)", actual_baud);
}

// ✅ ESP32 时钟配置示例
void check_clock_config(void) {
    rtc_cpu_freq_config_t conf;
    rtc_clk_cpu_freq_get_config(&conf);
    ESP_LOGI(TAG, "CPU 频率: %u MHz", conf.freq_mhz);
    
    uint32_t apb_freq = rtc_clk_apb_freq_get();
    ESP_LOGI(TAG, "APB 频率: %u Hz", apb_freq);
}
```

---

## 强制规范

### 1. **寄存器操作规范**
- 所有硬件寄存器必须声明为 `volatile`
- 寄存器位操作使用位掩码，不用魔数
- 保留位必须保持不变（读-改-写）

### 2. **GPIO 复用规范**
- 使用引脚分配表管理所有 GPIO
- 同一引脚不可配置多个功能
- 复用功能必须查阅数据手册

### 3. **外设配置规范**
- I2C 必须配置为开漏 + 上拉
- SPI 高速模式必须配置 GPIO 为 High Speed
- ADC 引脚必须配置为模拟输入模式
- 外设使用前必须使能时钟

### 4. **初始化顺序**
```
1. 使能外设时钟（RCC）
2. 使能 GPIO 时钟
3. 配置 GPIO 复用功能
4. 配置外设寄存器
5. 使能外设
6. 配置中断（如需要）
```

### 5. **引脚电气特性表**

| 外设 | GPIO 模式 | 速度等级 | 上拉/下拉 |
|------|----------|---------|----------|
| I2C SDA/SCL | 开漏 | Medium | 上拉 |
| SPI MOSI/MISO/SCK | 推挽 | High/Very High | 无 |
| UART TX/RX | 推挽/输入 | Medium | 上拉 |
| ADC 输入 | 模拟 | - | 无 |
| PWM 输出 | 推挽 | Medium | 无 |
| 按钮输入 | 输入 | Low | 上拉/下拉 |

---

## 输出格式
【风险等级】致命/高/中/低
【位置】文件名:行号
【冲突类型】GPIO复用/寄存器/时钟/引脚配置/电气特性
【冲突详情】具体冲突的引脚、外设、配置项
【风险说明】可能导致的后果
【修复方案 + 正确示例】
