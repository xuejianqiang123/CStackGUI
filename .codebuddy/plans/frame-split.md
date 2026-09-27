# 字节流切帧（`cg_frame_*`）：把"半帧 / 一帧 / 两帧粘一起"切干净

## 为什么是这个（判据，不是直觉）

- **证据 1（文档级）**：`examples/modbus/main.c` 文件头自己写着 —— "串口上的 Modbus/RTU 没有长度
  字段，得靠**帧间静默**或 CRC 反向定位 —— **那是另一个话题**，本示例只做 TCP"；
- **证据 2（代码级）**：全仓库 grep `切帧 / 分包 / 粘包 / cg_frame` **没有任何原语**；
  示例里手写了一段 `drain_frames`（按 MBAP 长度字段切）；
- **本质**：串口与 TCP 都只给**字节流**，没有消息边界；现场协议的定界办法只有四种
  （帧间静默 / 定长 / 分隔符 / 头里带长度），而"攒着 + 切出来"这段**每个上位机都要重写一遍** ——
  与 `cg_crc16` / `cg_modbus_*` 同一类：纯函数、无 I/O、该进框架。

## 交付

1. `runtime/src/kit/cg_kit_frame.c` + 公开头里的 `cg_frame_t`：
   `cg_frame_init / cg_frame_limits / cg_frame_lenfield / cg_frame_feed / cg_frame_take /
   cg_frame_tick / cg_frame_reset / cg_frame_dropped / cg_frame_frames` + `cg_now_ms()`；
   **纯状态机：时间由调用方给**（`now_ms`），不内部读时钟 ⇒ 单测不需要 sleep、不需要真串口。
2. 单测（`tests/unit/test_kit_frame.c`）：真实规范帧喂各种碎法 + 粘包 + 静默边界两侧 + 噪声丢字节计数。
3. 用在 Modbus 示例的 TCP 侧（改走 `cg_frame_*` 的长度字段模式），示例头那句"那是另一个话题"
   改成指向本 API；示例 exe 重编。
4. 文档：头文件用法片段、支持库表、README 边界表。

## 不做什么（写下来免得当成漏了）

- 不做"主站会话 / 轮询调度"（与 `cg_modbus_*` 同一立场：框架给原料，事务归业务）；
- 不做 RTU-over-TCP 网关示例：那种切法靠"对端一次写完一帧"，**不是规范保证的**，只在注释里说明。

## 验证判据（可失败，不是空跑）

- **外部不变量**：每一次切出来的帧都必须通过 `cg_crc16` 校验（用 `cg_modbus_build_*` 造帧，
  再用切帧器切回来，逐字节比）—— 不是"我说它对"；
- **静默边界两侧都断**：`gap-1 ms` 不许切、`gap ms` 必须切；
- **噪声不越界**：灌远超缓冲的字节，断言"丢字节计数在涨、缓冲没越界（用金丝雀字节验）"。
