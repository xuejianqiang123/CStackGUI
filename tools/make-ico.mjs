// make-ico.mjs -- 生成 CStackGUI 程序图标（多尺寸 .ico，纯 Node 无依赖）
// 用法: node tools\make-ico.mjs
// 输出: runtime\res\cstackgui.ico（业务 exe，windres 用）
//       designer\build\icon.ico（electron-builder 用）
//       build\icon-preview.png（256px 预览，肉眼检查用）
// 图形为代码绘制：M3 紫渐变圆角底 + 三层堆叠窗口（"CStack"主题）。
// 先在 2048 超采样画布上硬边绘制，再按面积加权缩小到各尺寸，天然抗锯齿。
import { deflateSync, crc32 as _crc32 } from "node:zlib";
import { writeFileSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const S = 2048, G = 256, K = S / G;

let crc32 = _crc32;
if (typeof crc32 !== "function") {
  const T = new Int32Array(256);
  for (let n = 0; n < 256; n++) { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; T[n] = c; }
  crc32 = (b) => { let c = -1; for (let i = 0; i < b.length; i++) c = T[(c ^ b[i]) & 0xff] ^ (c >>> 8); return (c ^ -1) >>> 0; };
}

function pngChunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length, 0);
  const body = Buffer.concat([Buffer.from(type, "ascii"), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(body) >>> 0, 0);
  return Buffer.concat([len, body, crc]);
}
function encodePng(w, h, rgba) {
  const st = w * 4 + 1, raw = Buffer.alloc(st * h);
  for (let y = 0; y < h; y++) { raw[y * st] = 0; for (let x = 0; x < w * 4; x++) raw[y * st + 1 + x] = rgba[y * w * 4 + x]; }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 6;
  return Buffer.concat([Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    pngChunk("IHDR", ihdr), pngChunk("IDAT", deflateSync(raw, { level: 9 })), pngChunk("IEND", Buffer.alloc(0))]);
}

const px = new Float32Array(S * S * 4);
function over(i, r, g, b, a) { // straight-alpha over 合成
  const da = px[i + 3] / 255, oa = a + da * (1 - a);
  if (oa <= 0) return;
  px[i] = (r * a + px[i] * da * (1 - a)) / oa;
  px[i + 1] = (g * a + px[i + 1] * da * (1 - a)) / oa;
  px[i + 2] = (b * a + px[i + 2] * da * (1 - a)) / oa;
  px[i + 3] = oa * 255;
}
function sdRoundRect(x, y, cx, cy, hw, hh, r) {
  const qx = Math.abs(x - cx) - (hw - r), qy = Math.abs(y - cy) - (hh - r);
  return Math.hypot(Math.max(qx, 0), Math.max(qy, 0)) + Math.min(Math.max(qx, qy), 0) - r;
}
// col: [r,g,b] 0-255；alpha 0-1；坐标按 256 网格
function fillRoundRect(x, y, w, h, r, col, alpha) {
  const cx = (x + w / 2) * K, cy = (y + h / 2) * K, hw = w * K / 2, hh = h * K / 2, rr = r * K;
  for (let py = Math.max(0, y * K | 0); py < Math.min(S, Math.ceil((y + h) * K)); py++)
    for (let qx = Math.max(0, x * K | 0); qx < Math.min(S, Math.ceil((x + w) * K)); qx++)
      if (sdRoundRect(qx + 0.5, py + 0.5, cx, cy, hw, hh, rr) < 0) over((py * S + qx) * 4, col[0], col[1], col[2], alpha);
}
// 底板：圆角方块内做垂直渐变
function fillTile(x, y, w, h, r, top, bottom) {
  const cx = (x + w / 2) * K, cy = (y + h / 2) * K, hw = w * K / 2, hh = h * K / 2, rr = r * K;
  for (let py = 0; py < S; py++) for (let qx = 0; qx < S; qx++) {
    if (sdRoundRect(qx + 0.5, py + 0.5, cx, cy, hw, hh, rr) >= 0) continue;
    const t = py / S, i = (py * S + qx) * 4;
    over(i, top[0] + (bottom[0] - top[0]) * t, top[1] + (bottom[1] - top[1]) * t, top[2] + (bottom[2] - top[2]) * t, 1);
  }
}

/* ---------- 图形：紫渐变圆角底 + 三层堆叠窗口 ---------- */
fillTile(0, 0, 256, 256, 57, [118, 92, 195], [77, 46, 133]);
const W = 150, H = 100, X = 53, R = 18;
fillRoundRect(X, 114, W, H, R, [255, 255, 255], 0.22);
fillRoundRect(X, 89, W, H, R, [255, 255, 255], 0.45);
fillRoundRect(X, 64, W, H, R, [255, 255, 255], 0.97);
fillRoundRect(67, 78, 72, 9, 4.5, [103, 80, 164], 1);   // 标题条
fillRoundRect(67, 98, 122, 9, 4.5, [203, 195, 230], 1); // 内容行 1
fillRoundRect(67, 116, 94, 9, 4.5, [224, 218, 244], 1); // 内容行 2

/* ---------- 缩放（alpha 加权面积平均，避免白边） ---------- */
function resize(target) {
  const out = Buffer.alloc(target * target * 4), sc = S / target;
  for (let ty = 0; ty < target; ty++) for (let tx = 0; tx < target; tx++) {
    let r = 0, g = 0, b = 0, a = 0, wsum = 0;
    for (let sy = Math.floor(ty * sc); sy < Math.ceil((ty + 1) * sc); sy++)
      for (let sx = Math.floor(tx * sc); sx < Math.ceil((tx + 1) * sc); sx++) {
        const wx = Math.min((tx + 1) * sc, sx + 1) - Math.max(tx * sc, sx);
        const wy = Math.min((ty + 1) * sc, sy + 1) - Math.max(ty * sc, sy);
        const wt = wx * wy, i = (sy * S + sx) * 4, al = px[i + 3];
        r += px[i] * al * wt; g += px[i + 1] * al * wt; b += px[i + 2] * al * wt; a += al * wt; wsum += wt;
      }
    const o = (ty * target + tx) * 4;
    out[o + 3] = Math.round(a / wsum);
    const a2 = a || 1;
    out[o] = Math.round(r / a2); out[o + 1] = Math.round(g / a2); out[o + 2] = Math.round(b / a2);
  }
  return out;
}
function bmpEntry(size, rgba) { // ICO 里的 BMP 帧（32bpp + AND 掩码）
  const st = size * 4, am = ((size + 31) >> 5) << 2, img = Buffer.alloc(40 + st * size + am * size);
  img.writeUInt32LE(40, 0); img.writeInt32LE(size, 4); img.writeInt32LE(size * 2, 8);
  img.writeUInt16LE(1, 12); img.writeUInt16LE(32, 14); img.writeUInt32LE(st * size + am * size, 20);
  for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
    const si = ((size - 1 - y) * size + x) * 4, di = 40 + y * st + x * 4;
    img[di] = rgba[si + 2]; img[di + 1] = rgba[si + 1]; img[di + 2] = rgba[si]; img[di + 3] = rgba[si + 3];
  }
  return img;
}
function buildIco(images) {
  const head = Buffer.alloc(6); head.writeUInt16LE(1, 2); head.writeUInt16LE(images.length, 4);
  const dirs = [], frames = []; let off = 6 + 16 * images.length;
  for (const im of images) {
    const e = Buffer.alloc(16), d = im.size === 256 ? 0 : im.size;
    e[0] = d; e[1] = d; e.writeUInt16LE(1, 4); e.writeUInt16LE(32, 6);
    e.writeUInt32LE(im.buf.length, 8); e.writeUInt32LE(off, 12);
    dirs.push(e); frames.push(im.buf); off += im.buf.length;
  }
  return Buffer.concat([head, ...dirs, ...frames]);
}

const sizes = [16, 24, 32, 48, 64, 128, 256];
const frames = sizes.map((sz) => {
  const rgba = resize(sz);
  return { size: sz, buf: sz === 256 ? encodePng(sz, sz, rgba) : bmpEntry(sz, rgba) };
});
const ico = buildIco(frames);
mkdirSync(join(ROOT, "runtime", "res"), { recursive: true });
mkdirSync(join(ROOT, "designer", "build"), { recursive: true });
mkdirSync(join(ROOT, "build"), { recursive: true });
writeFileSync(join(ROOT, "runtime", "res", "cstackgui.ico"), ico);
writeFileSync(join(ROOT, "designer", "build", "icon.ico"), ico);
writeFileSync(join(ROOT, "build", "icon-preview.png"), encodePng(256, 256, resize(256)));
console.log("[ok] cstackgui.ico / designer\\build\\icon.ico / build\\icon-preview.png 已生成");
