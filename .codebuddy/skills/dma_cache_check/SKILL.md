---
name: dma_cache_check
description: 专门审查 STM32/ESP32 固件 DMA 代码。只要出现 DMA、BDMA、外设传输、内存搬运、缓冲区配置，自动触发。重点检查：缓存未刷新、缓冲区未对齐、栈内存做DMA、传输溢出、中断冲突。
auto_invoke: true
allowed-tools: [ReadFile, GrepSearch]
---
# 技能：DMA 与 Cache 一致性审查

## ⚠️ 核心原则（对应 AI 评审第 3 层：DMA 与 Cache）
- **DMA 缓冲区不能在栈上**：必须用全局/静态变量或 heap_caps_malloc
- **Cache 必须刷新**：DMA 前 Clean，DMA 后 Invalidate
- **地址必须对齐**：根据 MCU 要求（通常 4/32 字节）
- **长度必须对齐**：DMA 传输长度需符合外设要求
- **内存区域必须匹配**：ESP32 DMA 只能访问特定 RAM 区域
# 技能：DMA 安全审查专家

## 触发条件
识别到以下任意场景自动执行：
- DMA初始化、DMA启动、HAL_DMA_Start
- 串口/ADC/SPI/I2C 外设DMA传输
- 内存缓冲区作为DMA目标
- Cache 缓存操作相关代码

## 完整检查清单
1. 【致命】DMA缓冲区是否全局/静态，禁止栈变量做DMA
2. 【致命】开启Cache后是否做 Clean/Invalidate
3. 【高危】缓冲区是否4字节对齐
4. 【高危】DMA传输长度是否存在越界
5. 【中危】多次DMA启动未做状态判断
6. 【中危】DMA中断未清标志、未做容错
7. 【低危】超时无处理、无异常重试

## 输出格式
【风险等级】致命/高/中/低
【位置】文件名:行号
【问题原理】
【错误代码片段】
【修复方案+正确示例】
