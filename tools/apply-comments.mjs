/**
 * @file    apply-comments.mjs
 * @brief   把"逐字 OLD/NEW 补丁"批量落到源码上（供并行子代理产出的改法直接落盘）
 *
 * 为什么要有它：子代理只能读不能写，它把改法以补丁块返回；用本工具一次性落盘，
 * 比"我逐条 replace_in_file"更省（补丁文本只经过一次输出）也更稳 ——
 * **每个文件最多写一次**，不存在"同一文件分批编辑互相覆盖"的问题；不匹配的块会
 * 全部列出来（而不是一条条失败），可以一次修完再重跑。
 *
 * 补丁格式（多次重复）：
 *   ===FILE <相对仓库根的路径，用 / 分隔>
 *   ===FN <函数名>        （可选，只用于打印）
 *   ===OLD
 *   <逐字原文：注释块 + 紧随其后的函数签名行；要与文件当前内容完全一致>
 *   ===NEW
 *   <改好的注释块 + 原样的签名行>
 *   ===END
 *
 * 用法：
 *   node tools\apply-comments.mjs <补丁文件>            落盘
 *   node tools\apply-comments.mjs <补丁文件> --dry      只检查能不能匹配
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const patchPath = process.argv[2];
const dry = process.argv.includes('--dry');

if (!patchPath) {
  console.error('用法：node tools\\apply-comments.mjs <补丁文件> [--dry]');
  process.exit(2);
}

/* ---- 解析补丁块 ---- */
const raw = fs.readFileSync(patchPath, 'utf8').replace(/\r\n/g, '\n');
const lines = raw.split('\n');
const blocks = [];
let cur = null, section = null;

for (const line of lines) {
  if (line.startsWith('===FILE')) {
    cur = { file: line.slice(7).trim(), fn: '', old: [], new: [] };
    section = null;
    continue;
  }
  if (line.startsWith('===FN')) { if (cur) cur.fn = line.slice(5).trim(); continue; }
  if (line === '===OLD') { section = 'old'; continue; }
  if (line === '===NEW') { section = 'new'; continue; }
  if (line === '===END') {
    if (cur && (cur.old.length || cur.new.length)) {
      cur.oldText = cur.old.join('\n');
      cur.newText = cur.new.join('\n');
      blocks.push(cur);
    }
    cur = null; section = null;
    continue;
  }
  if (cur && section) cur[section].push(line);
}

if (blocks.length === 0) {
  console.error('没解析到任何补丁块：检查 ===FILE / ===OLD / ===NEW / ===END 标记');
  process.exit(2);
}

/* ---- 按文件分组，每个文件只写一次 ---- */
const byFile = new Map();
for (const b of blocks) {
  if (!byFile.has(b.file)) byFile.set(b.file, []);
  byFile.get(b.file).push(b);
}

let ok = 0, bad = 0;
for (const [rel, list] of byFile) {
  const abs = path.join(ROOT, rel);
  if (!fs.existsSync(abs)) {
    for (const b of list) { bad++; console.log(`  [缺文件] ${rel} : ${b.fn}`); }
    continue;
  }
  const orig = fs.readFileSync(abs, 'utf8');
  const crlf = orig.includes('\r\n');
  let src = orig.replace(/\r\n/g, '\n');

  for (const b of list) {
    const first = src.indexOf(b.oldText);
    if (first < 0) { bad++; console.log(`  [匹配不上] ${rel} : ${b.fn}`); continue; }
    if (src.indexOf(b.oldText, first + 1) >= 0) { bad++; console.log(`  [不唯一] ${rel} : ${b.fn}`); continue; }
    src = src.slice(0, first) + b.newText + src.slice(first + b.oldText.length);
    ok++;
  }

  if (!dry && src !== orig.replace(/\r\n/g, '\n')) {
    fs.writeFileSync(abs, crlf ? src.replace(/\n/g, '\r\n') : src, 'utf8');
    console.log(`  [已写] ${rel}（${list.length} 块）`);
  }
}

console.log(`\n== apply-comments：${ok} 块就绪、${bad} 块有问题（${byFile.size} 个文件）`);
process.exit(bad ? 1 : 0);
