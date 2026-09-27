/* 一个够用的模拟 Modbus/TCP 从站 —— 给 examples/modbus 当对手，不追求协议完整。
 *
 * 它存在的理由：**Modbus 上位机的第一件事是"能跟一个从站说上话"**，而现场不一定有板子。
 * 这个脚本在本机起一个从站，于是"拼帧 → 发送 → 收 → 按长度切帧 → 解帧 → 填表"整条链
 * 都能在这台机器上真跑一遍（不需要任何硬件）。
 *
 * 实现了：01 读线圈 / 03 读保持寄存器 / 04 读输入寄存器；地址越界回异常码 02（非法数据地址）、
 * 功能码不认识回 01（非法功能）。其余的照规范该回异常，这里没做（示例用不上）。
 *
 * 保持寄存器里放的是**可预期的值**（i*7+100），另外让第 0 个寄存器当"运行秒数"——
 * 这样自动轮询时能看见表格在变，一眼就知道轮询真的在跑。
 *
 * 用法：node examples/modbus/mock-slave.mjs [端口]     默认 1502
 *      （502 是标准端口，但要管理员权限；示例默认用 1502，界面里的端口默认也是它）
 */
import net from "node:net";

const PORT = Number(process.argv[2] ?? 1502);
const REGS = 100;

const start = Date.now();
const holding = Array.from({ length: REGS }, (_, i) => (i * 7 + 100) & 0xffff);
const input = Array.from({ length: REGS }, (_, i) => (i * 3 + 7) & 0xffff);
const coils = Array.from({ length: REGS }, (_, i) => i % 3 === 0);

/** 异常响应：功能码 | 0x80 + 异常码 */
function exception(frame, code) {
  const pdu = Buffer.from([frame[7] | 0x80, code]);
  return Buffer.concat([mbap(frame, pdu.length), pdu]);
}

/** MBAP 头：事务号沿用请求、协议 0、长度 = 1(单元) + PDU、单元沿用请求 */
function mbap(req, pduLen) {
  const h = Buffer.alloc(7);
  h.writeUInt16BE(req.readUInt16BE(0), 0);
  h.writeUInt16BE(0, 2);
  h.writeUInt16BE(pduLen + 1, 4);
  h[6] = req[6];
  return h;
}

function handle(frame) {
  const unit = frame[6];
  const func = frame[7];
  const addr = frame.readUInt16BE(8);

  if (func === 1 || func === 3 || func === 4) {
    const count = frame.readUInt16BE(10);
    if (addr + count > REGS || count < 1 || count > 125) return exception(frame, 2);

    let pdu;
    if (func === 3 || func === 4) {
      const src = func === 3 ? holding : input;
      pdu = Buffer.alloc(2 + count * 2);
      pdu[0] = func;
      pdu[1] = count * 2;
      for (let i = 0; i < count; i++) pdu.writeUInt16BE(src[addr + i] & 0xffff, 2 + i * 2);
    } else {
      const bytes = Math.ceil(count / 8);
      pdu = Buffer.alloc(2 + bytes);
      pdu[0] = func;
      pdu[1] = bytes;
      for (let i = 0; i < count; i++) {
        if (coils[addr + i]) pdu[2 + (i >> 3)] |= 1 << (i & 7);
      }
    }
    return Buffer.concat([mbap(frame, pdu.length), pdu]);
  }

  return exception(frame, 1);   /* 非法功能 */
}

const server = net.createServer((sock) => {
  let buf = Buffer.alloc(0);
  console.log(`[mock] 客户端 ${sock.remoteAddress}:${sock.remotePort} 连上了`);

  sock.on("data", (chunk) => {
    buf = Buffer.concat([buf, chunk]);
    for (;;) {
      if (buf.length < 7) return;
      const total = 6 + buf.readUInt16BE(4);          /* 头里的长度字段含单元标识 */
      if (total < 8 || total > 260) {
        console.log(`[mock] 长度字段可疑（${total}），丢掉缓冲`);
        buf = Buffer.alloc(0);
        return;
      }
      if (buf.length < total) return;                 /* 还没收齐，等下一批 */
      const frame = buf.subarray(0, total);
      buf = buf.subarray(total);

      const head = frame.subarray(0, Math.min(14, frame.length)).toString("hex").match(/../g).join(" ").toUpperCase();
      console.log(`[mock] RX ${head}${frame.length > 14 ? " …" : ""}`);

      const rsp = handle(frame);
      if (rsp) {
        const rhead = rsp.subarray(0, Math.min(20, rsp.length)).toString("hex").match(/../g).join(" ").toUpperCase();
        console.log(`[mock] TX ${rhead}${rsp.length > 20 ? " …" : ""}`);
        sock.write(rsp);
      }
    }
  });

  sock.on("error", (e) => console.log(`[mock] 客户端出错：${e.message}`));
  sock.on("close", () => console.log("[mock] 客户端断开"));
});

/* 让第 0 个保持寄存器当"运行秒数"：自动轮询时表格里的值会变，一眼看出轮询在跑。
 * （上位机轮询读到的就是它 —— 现场常见做法：拿一个寄存器当心跳/计数器） */
setInterval(() => {
  holding[0] = Math.floor((Date.now() - start) / 1000) & 0xffff;
}, 250).unref?.();

server.listen(PORT, "127.0.0.1", () => {
  console.log(`[mock] Modbus/TCP 从站已监听 127.0.0.1:${PORT}（${REGS} 个寄存器）`);
  console.log("[mock] 保持寄存器 0..9 = " + holding.slice(0, 10).join(", "));
});
