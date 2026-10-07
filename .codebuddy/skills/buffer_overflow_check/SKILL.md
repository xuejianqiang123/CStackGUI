---
name: buffer_overflow_check
description: 检测缓冲区溢出、数组越界、字符串操作越界、memcpy/strcpy/sprintf 等危险函数的边界安全。触发条件：改动涉及缓冲区/字符串/memcpy/格式化输出，或 piv-review-changes 按领域路由到本技能时自动执行。
auto_invoke: true
allowed-tools: [ReadFile, GrepSearch]
---
# 技能：缓冲区溢出与边界安全审查

> **本工程落地口径**：正文示例里出现的 `ESP_LOGE` / `esp_err_t` 是**上游模板残留**（本工程是
> Win32/GDI+ 桌面工具包，没有 ESP-IDF）。判据看**缺陷模式**（越界、整数溢出、返回值不判长度），
> 不看示例里的 API 名；本工程对应的是 `cg_*` 接口、`cg_frame_*` 切帧、Win32 缓冲区与 `snprintf`。

## 触发条件
- memcpy/memmove/memset 操作
- strcpy/strncpy/strcat/sprintf/snprintf
- 数组写入操作
- 字符串拼接与格式化
- 循环中的数组索引访问

## ⚠️ 核心原则：交叉验证 + 追踪传播
- 看到 memcpy 使用了一个长度变量，但前面没有对该变量的边界检查 → 这就是问题，必须报告
- 长度变量是否先校验再使用？检查和使用之间是否可能被篡改(TOCTOU)？
- 循环索引 `i < len` 中的 len 是否可能超出数组容量？循环内是否对索引做了边界保护？

## 🔍 完整检查清单（对应 AI 评审第 1 层）

### 🔴 第 1 层：内存安全（死机级）

#### 1. **缓冲区溢出：memcpy/memmove/memset**
- **问题**：目标容量是否 ≥ 源长度？
- **检查**：长度变量是否先校验再使用？
- **TOCTOU**：检查和使用之间是否可能被篡改？

**错误示例**：
```c
// ❌ 错误：长度未校验直接使用
void process_data(uint8_t *dst, uint8_t *src, uint16_t len) {
    memcpy(dst, src, len);  // len 可能超出 dst 容量！
}

// ❌ 错误：先用后检查（时序错误）
memcpy(dst, src, len);
if (len > MAX_SIZE) return -1;  // 已经溢出了！

// ❌ 错误：TOCTOU 竞态
if (msg->len <= MAX_SIZE) {      // 检查
    // 中间可能被中断修改 msg->len
    memcpy(dst, msg->data, msg->len);  // 使用
}
```

**正确示例**：
```c
// ✅ 正确：先校验后使用
if (len > sizeof(dst)) {
    ESP_LOGE(TAG, "长度超限: %u > %u", len, sizeof(dst));
    return -1;
}
memcpy(dst, src, len);

// ✅ 正确：TOCTOU 防护 - 缓存到局部变量
uint16_t safe_len = msg->len;  // 缓存
if (safe_len > MAX_SIZE) {
    return -1;
}
memcpy(dst, msg->data, safe_len);
```

---

#### 2. **strcpy/strcat 无边界保护**
- **问题**：是否使用 strncpy/strncat 替代？
- **问题**：目标缓冲区是否足够大？
- **问题**：是否确保字符串以 '\0' 结尾？

**错误示例**：
```c
// ❌ 致命：strcpy 无边界保护
char buf[32];
strcpy(buf, user_input);  // user_input 可能超过 32 字节

// ❌ 错误：strncpy 后未强制结尾
strncpy(buf, src, sizeof(buf));  // 如果 src 长度 >= sizeof(buf)，不会自动加 '\0'
```

**正确示例**：
```c
// ✅ 正确：strncpy + 强制结尾
strncpy(buf, src, sizeof(buf) - 1);
buf[sizeof(buf) - 1] = '\0';

// ✅ 更好：使用 snprintf
snprintf(buf, sizeof(buf), "%s", src);
```

---

#### 3. **sprintf 缓冲区溢出**
- **问题**：是否改用 snprintf？
- **问题**：格式化后的长度是否可能超出缓冲区？

**错误示例**：
```c
// ❌ 致命：sprintf 无边界保护
char buf[64];
sprintf(buf, "Device: %s, ID: %d, Status: %s", 
        device_name, device_id, status_str);  // 可能溢出
```

**正确示例**：
```c
// ✅ 正确：使用 snprintf
int ret = snprintf(buf, sizeof(buf), "Device: %s, ID: %d", name, id);
if (ret >= sizeof(buf)) {
    ESP_LOGW(TAG, "格式化字符串被截断");
}
```

---

#### 4. **数组越界写入**
- **问题**：循环索引是否可能超出数组容量？
- **问题**：循环内是否对索引做了边界保护？

**错误示例**：
```c
// ❌ 错误：len 可能超出数组大小
uint8_t buffer[128];
for (int i = 0; i < len; i++) {  // len 可能 > 128
    buffer[i] = data[i];
}

// ❌ 错误：直接使用外部索引
void set_value(uint8_t index, uint8_t value) {
    array[index] = value;  // index 未校验
}
```

**正确示例**：
```c
// ✅ 正确：先限制 len 上限
len = (len > sizeof(buffer)) ? sizeof(buffer) : len;
for (int i = 0; i < len; i++) {
    buffer[i] = data[i];
}

// ✅ 正确：参数边界校验
void set_value(uint8_t index, uint8_t value) {
    if (index >= ARRAY_SIZE) {
        ESP_LOGE(TAG, "索引越界: %u >= %u", index, ARRAY_SIZE);
        return;
    }
    array[index] = value;
}
```

---

### 🟠 第 5 层：数值与边界安全

#### 5. **长度校验时序错误**
- **问题**：len 字段是否先做边界检查，再用于 memcpy/循环边界？

**错误示例**：
```c
// ❌ 错误：使用在检查之后
memcpy(dst, src, packet->len);  // 先使用
if (packet->len > MAX_PKT_SIZE) {  // 后检查
    return -1;
}
```

**正确示例**：
```c
// ✅ 正确：先检查后使用
if (packet->len > MAX_PKT_SIZE) {
    return -1;
}
memcpy(dst, src, packet->len);
```

---

#### 6. **整数溢出导致边界失效**
- **问题**：uint8_t 累加是否可能溢出回绕？
- **问题**：len + offset 是否可能溢出？
- **问题**：有符号/无符号混用是否安全？

**错误示例**：
```c
// ❌ 错误：整数溢出导致检查失效
uint8_t len = 200;
uint8_t offset = 100;
uint8_t total = len + offset;  // 溢出：300 回绕为 44
if (total > 255) {  // 永远不会触发
    return -1;
}

// ❌ 错误：有符号/无符号混用
int index = -1;
uint8_t array[10];
if (index < sizeof(array)) {  // -1 转为无符号后变成极大值
    array[index] = 0;  // 越界！
}
```

**正确示例**：
```c
// ✅ 正确：使用更宽的类型避免溢出
uint16_t total = (uint16_t)len + (uint16_t)offset;
if (total > MAX_SIZE) {
    return -1;
}

// ✅ 正确：先检查负数
if (index < 0 || index >= (int)sizeof(array)) {
    return -1;
}
```

---

#### 7. **TOCTOU 竞态条件**
- **问题**：校验和使用之间，len 变量是否可能被另一上下文修改？
- **解决**：应缓存到局部变量

**错误示例**：
```c
// ❌ 错误：共享变量 TOCTOU
volatile uint16_t g_data_len;  // ISR 可能修改

void process(void) {
    if (g_data_len <= MAX_SIZE) {      // T1: 检查时合法
        // ISR 触发，g_data_len 被改为超大值
        memcpy(buf, data, g_data_len); // T2: 使用时已非法
    }
}
```

**正确示例**：
```c
// ✅ 正确：缓存到局部变量
void process(void) {
    uint16_t len = g_data_len;  // 原子读取并缓存
    if (len <= MAX_SIZE) {
        memcpy(buf, data, len);
    }
}
```

---

## 危险函数对照表

| 危险函数 | 替代方案 | 注意事项 |
|---------|---------|---------|
| `strcpy` | `strncpy` + 强制结尾 '\0' | 目标缓冲区必须足够大 |
| `strcat` | `strncat` + 强制结尾 '\0' | 计算剩余空间 |
| `sprintf` | `snprintf` | 检查返回值是否截断 |
| `gets` | `fgets` | **禁止使用 gets** |
| `scanf("%s")` | `scanf("%31s", buf)` | 限制最大读取长度 |
| `memcpy` | 先校验 len | len ≤ sizeof(dst) |

---

## 强制规范

1. **所有 memcpy/memmove 必须先校验长度**
2. **禁止使用 strcpy/strcat/sprintf/gets**
3. **数组索引必须先做边界检查**
4. **循环边界必须先限制上限**
5. **共享变量必须缓存到局部变量（TOCTOU 防护）**
6. **整数运算必须防止溢出**

---

## 输出格式
【风险等级】致命/高/中/低
【位置】文件名:行号
【问题类型】缓冲区溢出/数组越界/长度未校验/TOCTOU/整数溢出
【错误代码片段】
【风险说明】可能导致的后果
【修复方案 + 正确示例】
