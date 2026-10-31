---
name: communication_protocol_check
description: 审查串口、TCP/UDP、帧协议（cg_frame_*）的稳定性、超时处理、校验机制、粘包切帧与重连。触发条件：改动涉及串口/网口收发或帧协议，或 piv-review-changes 按领域路由到本技能时自动执行。（本工程无 SPI/I2C/BLE/MQTT，那部分是上游模板残留。）
auto_invoke: true
allowed-tools: [ReadFile, GrepSearch]
---
# 技能：通信协议与数据传输审查

> **本工程落地口径**：正文示例里的 `uart_read_bytes` / `i2c_master_*` / `esp_wifi_*` /
> `pdMS_TO_TICKS` 是**上游模板残留**。本工程的通信面是 `cg_serial_*`（串口）与 `cg_net_*`
> （TCP/UDP），字节流一律按 `cg_frame_*` 切帧；"看门狗复位"这类后果在本工程对应"线程卡死 /
> UI 无响应"。判据看**缺陷模式**（超时、粘包、字节序、重连），不看 API 名。

## ⚠️ 核心原则（对应 AI 评审第 8 层：通信协议与数据完整性）
- **超时必须设置**：所有阻塞式接收必须设置超时，避免死等
- **校验必须实现**：数据包必须包含 CRC/校验和
- **边界必须检查**：接收缓冲区必须做溢出保护
- **重传必须支持**：关键数据必须有应答确认机制
- **断线必须重连**：WiFi/BLE 必须实现自动重连

## 触发条件
- UART/SPI/I2C 收发数据
- WiFi/BLE 连接与通信
- TCP/UDP Socket 操作
- MQTT/HTTP/CoAP 协议栈
- 自定义协议解析
- 数据包封装与解析

---

## 🔍 完整检查清单

### 🔴 第 8 层：通信协议与数据完整性（稳定性级）

#### 1. **无超时机制导致死等（致命）**
- **问题**：阻塞式接收是否设置了超时？
- **后果**：程序卡死、看门狗复位

**错误示例**：
```c
// ❌ 致命：UART 接收无超时，可能永久阻塞
void uart_receive_wrong(void) {
    uint8_t data[128];
    int len = 0;
    
    // 死等，直到收满 128 字节
    while (len < 128) {
        uart_read_bytes(UART_NUM_1, &data[len], 1, portMAX_DELAY);  // 永久等待！
        len++;
    }
}

// ❌ 致命：I2C 读取无超时
uint8_t read_sensor(void) {
    uint8_t data;
    i2c_master_read_from_device(I2C_NUM_0, SENSOR_ADDR, 
                                 &data, 1, portMAX_DELAY);  // 传感器离线会卡死
    return data;
}

// ❌ 致命：TCP 接收无超时
void tcp_recv_wrong(int sock) {
    char buffer[1024];
    recv(sock, buffer, sizeof(buffer), 0);  // 对端不发数据就永久阻塞
}
```

**正确示例**：
```c
// ✅ 正确：UART 接收设置超时
esp_err_t uart_receive(uint8_t *data, size_t len, uint32_t timeout_ms) {
    int received = uart_read_bytes(UART_NUM_1, data, len, 
                                    pdMS_TO_TICKS(timeout_ms));
    if (received < 0) {
        ESP_LOGE(TAG, "UART 接收错误");
        return ESP_FAIL;
    }
    
    if (received < len) {
        ESP_LOGW(TAG, "UART 接收超时: 期望 %d 字节, 实际收到 %d 字节", len, received);
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

// ✅ 正确：I2C 读取设置超时
esp_err_t read_sensor_safe(uint8_t *data) {
    esp_err_t ret = i2c_master_read_from_device(
        I2C_NUM_0, SENSOR_ADDR, data, 1, pdMS_TO_TICKS(100)  // 100ms 超时
    );
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "传感器读取失败: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

// ✅ 正确：TCP 接收设置超时
int tcp_recv_safe(int sock, char *buffer, size_t len, uint32_t timeout_ms) {
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    int received = recv(sock, buffer, len, 0);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ESP_LOGW(TAG, "TCP 接收超时");
            return -1;
        }
        ESP_LOGE(TAG, "TCP 接收错误: %d", errno);
        return -1;
    }
    
    return received;
}
```

---

#### 2. **未做 CRC/校验码验证（致命）**
- **问题**：数据包是否包含校验码？接收端是否验证？
- **后果**：数据损坏未检测、设备误动作

**错误示例**：
```c
// ❌ 致命：协议无校验码
typedef struct {
    uint8_t header;    // 帧头 0xAA
    uint8_t cmd;       // 命令
    uint8_t len;       // 数据长度
    uint8_t data[64];  // 数据
    uint8_t tail;      // 帧尾 0x55
} packet_t;  // 缺少校验码！

void process_packet(packet_t *pkt) {
    if (pkt->header == 0xAA && pkt->tail == 0x55) {
        // 直接执行命令，未验证数据完整性
        execute_command(pkt->cmd, pkt->data, pkt->len);
    }
}
```

**正确示例**：
```c
// ✅ 正确：协议包含 CRC16 校验
typedef struct {
    uint8_t header;    // 帧头 0xAA
    uint8_t cmd;       // 命令
    uint8_t len;       // 数据长度
    uint8_t data[64];  // 数据
    uint16_t crc16;    // CRC16 校验码
    uint8_t tail;      // 帧尾 0x55
} packet_t;

// CRC16 计算（标准 Modbus CRC）
uint16_t calculate_crc16(uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

esp_err_t process_packet(packet_t *pkt) {
    // 1. 验证帧头帧尾
    if (pkt->header != 0xAA || pkt->tail != 0x55) {
        ESP_LOGE(TAG, "帧头/帧尾错误");
        return ESP_ERR_INVALID_STATE;
    }
    
    // 2. 验证长度
    if (pkt->len > sizeof(pkt->data)) {
        ESP_LOGE(TAG, "长度超限: %u", pkt->len);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 3. 计算 CRC
    uint16_t calc_crc = calculate_crc16((uint8_t *)pkt, 
                                        offsetof(packet_t, crc16));
    
    // 4. 验证 CRC
    if (calc_crc != pkt->crc16) {
        ESP_LOGE(TAG, "CRC 校验失败: 期望 0x%04X, 实际 0x%04X", 
                 pkt->crc16, calc_crc);
        return ESP_ERR_INVALID_CRC;
    }
    
    // 5. CRC 正确，执行命令
    return execute_command(pkt->cmd, pkt->data, pkt->len);
}
```

---

#### 3. **接收缓冲区溢出（致命）**
- **问题**：接收数据长度是否超出缓冲区？
- **后果**：缓冲区溢出、栈破坏、系统崩溃

**错误示例**：
```c
// ❌ 致命：未校验长度就拷贝
void uart_receive_wrong(void) {
    uint8_t buffer[128];
    uint8_t header[3];
    
    // 接收头部
    uart_read_bytes(UART_NUM_1, header, 3, pdMS_TO_TICKS(1000));
    uint8_t len = header[2];  // 长度字段
    
    // 直接拷贝，未校验 len 是否超出 buffer 大小
    uart_read_bytes(UART_NUM_1, buffer, len, pdMS_TO_TICKS(1000));  // 溢出！
}
```

**正确示例**：
```c
// ✅ 正确：先校验长度，再拷贝
esp_err_t uart_receive_safe(uint8_t *buffer, size_t buffer_size, size_t *out_len) {
    uint8_t header[3];
    
    // 接收头部
    int ret = uart_read_bytes(UART_NUM_1, header, 3, pdMS_TO_TICKS(1000));
    if (ret != 3) {
        ESP_LOGE(TAG, "接收头部失败");
        return ESP_ERR_TIMEOUT;
    }
    
    uint8_t len = header[2];
    
    // 校验长度
    if (len > buffer_size) {
        ESP_LOGE(TAG, "数据长度超限: %u > %u", len, buffer_size);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 安全拷贝
    ret = uart_read_bytes(UART_NUM_1, buffer, len, pdMS_TO_TICKS(1000));
    if (ret != len) {
        ESP_LOGE(TAG, "接收数据失败: 期望 %u, 实际 %d", len, ret);
        return ESP_ERR_TIMEOUT;
    }
    
    *out_len = len;
    return ESP_OK;
}
```

---

#### 4. **数据粘包/分包未处理（高危）**
- **问题**：TCP 流式传输是否正确处理粘包/分包？
- **后果**：协议解析错误、数据丢失

**错误示例**：
```c
// ❌ 高危：假设每次 recv 都收到完整的包
void tcp_recv_wrong(int sock) {
    packet_t pkt;
    recv(sock, &pkt, sizeof(pkt), 0);  // 可能只收到一部分
    process_packet(&pkt);  // 数据不完整！
}
```

**正确示例**：
```c
// ✅ 正确：使用状态机处理粘包/分包
typedef enum {
    STATE_WAIT_HEADER,
    STATE_WAIT_LENGTH,
    STATE_WAIT_DATA,
    STATE_WAIT_CRC,
} recv_state_t;

typedef struct {
    recv_state_t state;
    uint8_t buffer[512];
    size_t buffer_pos;
    size_t expected_len;
} recv_context_t;

esp_err_t tcp_recv_statemachine(int sock, recv_context_t *ctx) {
    uint8_t byte;
    
    while (1) {
        int ret = recv(sock, &byte, 1, 0);
        if (ret <= 0) {
            return ESP_ERR_TIMEOUT;
        }
        
        switch (ctx->state) {
            case STATE_WAIT_HEADER:
                if (byte == 0xAA) {
                    ctx->buffer[0] = byte;
                    ctx->buffer_pos = 1;
                    ctx->state = STATE_WAIT_LENGTH;
                }
                break;
                
            case STATE_WAIT_LENGTH:
                ctx->buffer[ctx->buffer_pos++] = byte;
                if (ctx->buffer_pos == 3) {  // header + cmd + len
                    ctx->expected_len = byte;
                    if (ctx->expected_len > sizeof(ctx->buffer) - 10) {
                        ESP_LOGE(TAG, "长度超限");
                        ctx->state = STATE_WAIT_HEADER;
                        return ESP_ERR_INVALID_SIZE;
                    }
                    ctx->state = STATE_WAIT_DATA;
                }
                break;
                
            case STATE_WAIT_DATA:
                ctx->buffer[ctx->buffer_pos++] = byte;
                if (ctx->buffer_pos == 3 + ctx->expected_len) {
                    ctx->state = STATE_WAIT_CRC;
                }
                break;
                
            case STATE_WAIT_CRC:
                ctx->buffer[ctx->buffer_pos++] = byte;
                if (ctx->buffer_pos == 3 + ctx->expected_len + 3) {  // +crc16 +tail
                    // 完整包接收完成
                    packet_t *pkt = (packet_t *)ctx->buffer;
                    esp_err_t ret = process_packet(pkt);
                    ctx->state = STATE_WAIT_HEADER;
                    return ret;
                }
                break;
        }
    }
}
```

---

#### 5. **大小端未转换（高危）**
- **问题**：多字节数据是否做了大小端转换？
- **后果**：数值错误、协议不兼容

**错误示例**：
```c
// ❌ 高危：网络传输未转换字节序
void send_data_wrong(int sock) {
    uint32_t value = 0x12345678;
    send(sock, &value, 4, 0);  // 不同架构字节序不同
}

void recv_data_wrong(int sock) {
    uint32_t value;
    recv(sock, &value, 4, 0);
    printf("Value: 0x%08X\n", value);  // 可能是 0x78563412
}
```

**正确示例**：
```c
// ✅ 正确：使用网络字节序（大端）
#include <arpa/inet.h>  // htonl, ntohl

void send_data_safe(int sock, uint32_t value) {
    uint32_t net_value = htonl(value);  // 主机序 → 网络序
    send(sock, &net_value, 4, 0);
}

uint32_t recv_data_safe(int sock) {
    uint32_t net_value;
    recv(sock, &net_value, 4, 0);
    uint32_t value = ntohl(net_value);  // 网络序 → 主机序
    return value;
}

// ✅ 自定义协议建议
// - 统一使用大端字节序（网络字节序）
// - 使用 htons/htonl（主机 → 网络）
// - 使用 ntohs/ntohl（网络 → 主机）
```

---

#### 6. **断线重连逻辑缺失（高危）**
- **问题**：WiFi/BLE 断开后是否自动重连？
- **后果**：网络中断后无法恢复

**错误示例**：
```c
// ❌ 高危：WiFi 断开后不重连
void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data) {
    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi 断开");
        // 没有重连逻辑
    }
}
```

**正确示例**：
```c
// ✅ 正确：WiFi 断开自动重连（限制重试次数）
#define MAX_WIFI_RETRY 10
static int s_retry_num = 0;

void wifi_event_handler(void *arg, esp_event_base_t event_base,
                        int32_t event_id, void *event_data) {
    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < MAX_WIFI_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "WiFi 重连: %d/%d", s_retry_num, MAX_WIFI_RETRY);
        } else {
            ESP_LOGE(TAG, "WiFi 重连失败，进入离线模式");
            enter_offline_mode();
        }
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        s_retry_num = 0;  // 重置重试计数
        ESP_LOGI(TAG, "WiFi 连接成功");
    }
}

// ✅ 更好：指数退避重连
static uint32_t get_retry_delay_ms(int retry_count) {
    // 100ms, 200ms, 400ms, 800ms, 1600ms, ...
    uint32_t delay = 100 * (1 << retry_count);
    return (delay > 30000) ? 30000 : delay;  // 最多 30 秒
}
```

---

## 强制规范

### 1. **超时设置规则**
- 所有阻塞式接收必须设置超时
- UART/I2C: 100-1000ms
- TCP/UDP: 1000-5000ms
- HTTP 请求: 10000-30000ms

### 2. **数据包格式规范**
```
[帧头] + [命令] + [长度] + [数据] + [CRC16] + [帧尾]
```
- 帧头/帧尾：固定字节（如 0xAA/0x55）
- 长度字段：必须限制上限
- CRC16/CRC32：使用标准算法（Modbus CRC、IEEE 802.3）

### 3. **缓冲区保护规则**
- 接收前必须校验长度
- 长度必须 ≤ 缓冲区大小
- 使用 `strncpy` / `memcpy` 而非 `strcpy`

### 4. **字节序转换规则**
- 网络传输统一使用大端（网络字节序）
- 使用 `htons/htonl/ntohs/ntohl`

### 5. **重连策略**
```
断线 → 重试 3 次（指数退避）→ 降级（离线模式）→ 上报故障
```

### 6. **通信超时建议值**

| 通信类型 | 推荐超时 | 备注 |
|---------|---------|------|
| UART | 100-1000ms | 根据波特率调整 |
| I2C | 100ms | 传感器响应快 |
| SPI | 10-100ms | 高速通信 |
| WiFi 连接 | 5000ms | 等待 AP 响应 |
| TCP 接收 | 1000-5000ms | 根据业务调整 |
| HTTP 请求 | 10000-30000ms | 网络延迟较大 |

---

## 输出格式
【风险等级】致命/高/中/低
【位置】文件名:行号
【问题类型】超时/校验/溢出/粘包/字节序/重连
【错误代码片段】
【风险说明】可能导致的后果
【修复方案 + 正确示例】
