/**
 * @file    apply-comments.mjs
 * @brief   把"注释整改补丁"批量落到源码上（供并行子代理产出的改法直接落盘）
 *
 * 为什么要有它：子代理只能读不能写，它把改法以补丁块返回；用本工具一次性落盘，
 * 比"我逐条 replace_in_file"更省（补丁文本只经过一次输出）也更稳 ——
 * **每个文件最多写一次**，不存在"同一文件分批编辑互相覆盖"的问题；有问题的块会
 * 全部列出来（而不是一条条失败），可以一次修完再重跑。
 *
 * 两种块格式：
 *
 * 【1】按函数名替换注释（推荐，短且不会错位）—— 子代理只回"函数名 + 新注释块"：
 *   ===FILE <相对仓库根的路径，用 / 分隔>
 *   ===FN <函数名>
 *   ===DOC
 *   <完整的新注释块，含首行 /** 与末行 *\/>
 *   ===END
 *
 * 【2】逐字替换（旧格式，需要原文精确匹配）：
 *   ===FILE ... / ===FN ... / ===OLD <原文> / ===NEW <新文> / ===END
 *
 * 用法：
 *   node tools\apply-comments.mjs <补丁文件>            落盘
 *   node tools\apply-comments.mjs <补丁文件> --dry      只检查能不能应用
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

/* ---------------- 块解析（===DOC 与 ===OLD/===NEW 两套都认） ---------------- */
const raw = fs.readFileSync(patchPath, 'utf8').replace(/\r\n/g, '\n');
const blocks = [];
let cur = null, section = null, lastFile = '';

const newBlock = (file, fn) => ({ file, fn, old: [], new: [], doc: [] });

/** 收块：有内容才算数（空壳块跳过） */
function flush() {
  if (!cur) return;
  cur.oldText = cur.old.join('\n').replace(/\s+$/, '');
  cur.newText = cur.new.join('\n').replace(/\s+$/, '');
  cur.docText = cur.doc.join('\n').replace(/\s+$/, '');
  if (cur.docText || cur.oldText) blocks.push(cur);
  cur = null; section = null;
}

for (const line of raw.split('\n')) {
  if (line.startsWith('===FILE')) { flush(); lastFile = line.slice(7).trim(); continue; }
  // ===FN 开一个新块：补丁里 ===FILE 常只写一次，后面连续用 ===FN（沿用同一个文件）
  if (line.startsWith('===FN')) { flush(); cur = newBlock(lastFile, line.slice(5).trim()); continue; }
  if (line === '===OLD') { if (!cur) cur = newBlock(lastFile, ''); section = 'old'; continue; }
  if (line === '===NEW') { if (!cur) cur = newBlock(lastFile, ''); section = 'new'; continue; }
  if (line === '===DOC') { if (!cur) cur = newBlock(lastFile, ''); section = 'doc'; continue; }
  if (line === '===END') { flush(); continue; }
  if (cur && section) cur[section].push(line);
}
flush();

if (blocks.length === 0) {
  console.error('没解析到任何补丁块：检查 ===FILE / ===FN / ===DOC（或 ===OLD/===NEW） / ===END 标记');
  process.exit(2);
}

/* ---------------- DOC 模式：按函数名定位它正上方的注释块 ---------------- */

/** 这个匹配行是不是"带函数体的那个"（签名收在 ')' 上，下一非空行是 '{'） */
function isDefinition(lines, i) {
  let j = i;
  while (j < lines.length && j - i < 12 && !/\)\s*$/.test(lines[j])) j++;
  if (j >= lines.length || !/\)\s*$/.test(lines[j])) return false;
  let k = j + 1;
  while (k < lines.length && lines[k].trim() === '') k++;
  return k < lines.length && /^\s*\{/.test(lines[k]);
}

/** 找函数行：优先**定义**（带函数体），没有才退回第一处声明 */
function findFunctionLine(lines, name) {
  const hits = [];
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (line === '' || /^\s/.test(line)) continue;      // 只看顶格
    if (/^[#}\/*]/.test(line)) continue;
    // 注意 `\s*`（不是 `\s+`）：`const char *cg_event_name(` 这种**星号贴着函数名**的
    // 签名，返回类型与名字之间没有空格，用 `\s+` 会一条都匹配不到（2026-09-28 踩过：
    // 一次落盘 9 块报"找不到函数"，全是这形状）
    const m = line.match(/^([A-Za-z_][\w\s*]*?)\s*([A-Za-z_]\w*)\s*\(/);
    if (m && m[2] === name) hits.push(i);
  }
  for (const i of hits) if (isDefinition(lines, i)) return i;
  return hits.length ? hits[0] : -1;
}

/** 函数上方紧邻的注释块范围 [start, end]；没有返回 null（中间的连续空行会跳过） */
function docRangeAbove(lines, fnLine) {
  let k = fnLine - 1;
  while (k >= 0 && lines[k].trim() === '') k--;
  if (k < 0 || !/\*\/\s*$/.test(lines[k])) return null;
  for (let m = k; m >= 0; m--) {
    if (/^\s*\/\*\*?/.test(lines[m])) {
      // 单行块（m === k）只认 `/**` 那种文档块，好把 `/* ---- 分节 ---- */` 这类分隔线留下
      if (m === k && !/^\s*\/\*\*/.test(lines[m])) return null;
      return [m, k];
    }
  }
  return null;
}

/* ---------------- 按文件分组，每个文件只写一次 ---------------- */
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
    /* --- DOC 模式 --- */
    if (b.docText) {
      let lines = src.split('\n');
      const fnLine = findFunctionLine(lines, b.fn);
      if (fnLine < 0) { bad++; console.log(`  [找不到函数] ${rel} : ${b.fn}`); continue; }
      const range = docRangeAbove(lines, fnLine);
      const docLines = b.docText.split('\n');
      if (range) lines = [...lines.slice(0, range[0]), ...docLines, ...lines.slice(range[1] + 1)];
      else lines = [...lines.slice(0, fnLine), ...docLines, ...lines.slice(fnLine)];
      src = lines.join('\n');
      ok++;
      continue;
    }
    /* --- 逐字替换（旧格式） --- */
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
