/**
 * @file    check-comments.mjs
 * @brief   C 注释规范门禁：runtime/ tests/ examples/ 下每个 C 函数的注释必须合 skill
 *          firmware_code_style §1 的 Doxygen 块格式
 *
 * 判定口径（§1，逐条机器可判）：
 *   1) 文件顶部 30 行内有 @file + @brief
 *   2) 每个函数定义 / 声明 / 函数指针 typedef 的**紧邻上方**有 /** 注释块
 *   3) 块内**第一个 tag 是 @brief**（一句话说清干什么）
 *   4) 每个具名参数各有一行 @param <名字>（未命名参数与可变参数 ... 不算）
 *   5) 有 @return
 *
 * 用法：
 *   node tools\check-comments.mjs             列出不合规的函数（按待整改数排序）
 *   node tools\check-comments.mjs gate        全部合规 exit 0，否则 exit 1
 *   node tools\check-comments.mjs codegate    比对 HEAD 与工作区"去掉注释后的 token"，
 *                                             证明本批改动只碰了注释（有代码改动 exit 1）
 *   node tools\check-comments.mjs normalize <文件>   打印归一化结果（codegate 的内部口径）
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const DIRS = ['runtime', 'tests', 'examples'];
const EXCLUDE_DIRS = new Set(['node_modules', 'build', 'dist', '.git']);

function walk(dir, out) {
  let ents;
  try { ents = fs.readdirSync(dir, { withFileTypes: true }); } catch { return; }
  for (const e of ents) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) { if (!EXCLUDE_DIRS.has(e.name)) walk(p, out); }
    else if (/\.(c|h)$/.test(e.name)) out.push(p);
  }
}

function allFiles() {
  const out = [];
  for (const d of DIRS) walk(path.join(ROOT, d), out);
  return out.sort();
}

/* ---------------- 注释剥离（保留换行，供比对） ---------------- */
function stripComments(src) {
  let out = '';
  let i = 0;
  const n = src.length;
  let state = 'code';
  while (i < n) {
    const c = src[i], c2 = src[i + 1];
    if (state === 'code') {
      if (c === '/' && c2 === '*') { state = 'block'; i += 2; continue; }
      if (c === '/' && c2 === '/') { state = 'line'; i += 2; continue; }
      if (c === '"' || c === "'") { state = c === '"' ? 'str' : 'chr'; out += c; i++; continue; }
      out += c; i++; continue;
    }
    if (state === 'block') {
      if (c === '*' && c2 === '/') { state = 'code'; i += 2; continue; }
      if (c === '\n') out += '\n';
      i++; continue;
    }
    if (state === 'line') {
      if (c === '\n') { state = 'code'; out += c; }
      i++; continue;
    }
    const q = state === 'str' ? '"' : "'";
    if (c === '\\') { out += c + (c2 || ''); i += 2; continue; }
    out += c;
    if (c === q) state = 'code';
    i++;
  }
  return out;
}

function normalizeCode(src) {
  return stripComments(src)
    .replace(/[ \t]+/g, ' ')
    .split('\n').map(l => l.trim()).filter(l => l !== '').join('\n');
}

/* ---------------- 函数索引 ---------------- */

/** 切出"函数名后面那个参数括号组"；typedef void (*name)(A, B) 取第二组 */
function paramGroup(sig) {
  const first = sig.indexOf('(');
  if (first < 0) return null;
  let depth = 0, close = -1;
  for (let i = first; i < sig.length; i++) {
    if (sig[i] === '(') depth++;
    else if (sig[i] === ')') { depth--; if (depth === 0) { close = i; break; } }
  }
  if (close < 0) return null;
  if (/^\s*\(/.test(sig.slice(close + 1))) {
    const start = sig.indexOf('(', close);
    depth = 0;
    for (let i = start; i < sig.length; i++) {
      if (sig[i] === '(') depth++;
      else if (sig[i] === ')') { depth--; if (depth === 0) return { text: sig.slice(start + 1, i), nameEnd: sig.slice(0, first) }; }
    }
    return null;
  }
  return { text: sig.slice(first + 1, close), nameEnd: sig.slice(0, first) };
}

function splitTop(text) {
  const parts = [];
  let depth = 0, cur = '';
  for (const c of text) {
    if (c === '(' || c === '[') depth++;
    if (c === ')' || c === ']') depth--;
    if (c === ',' && depth === 0) { parts.push(cur); cur = ''; continue; }
    cur += c;
  }
  if (cur.trim() !== '') parts.push(cur);
  return parts.map(s => s.trim()).filter(s => s !== '');
}

/** 参数名：函数指针取 (*name)，其余取最后一个标识符 */
function paramName(p) {
  if (/\(\s*\*/.test(p)) {
    const m = p.match(/\(\s*\*\s*([A-Za-z_]\w*)\s*\)/);
    return m ? m[1] : null;
  }
  // 允许"一个或多个"数组后缀：`char cells[][N]`、`int a[2][3]` 这类二维数组形参，
  // 旧写法只认一对 []，于是取不到参数名、被当成未命名参数漏数（2026-09-28 实测：
  // 一次落盘报 "@param 多出（签名 3 个）"，其实签名有 4 个参数）。
  const m = p.match(/([A-Za-z_]\w*)\s*((?:\[\s*\w*\s*\])*)\s*$/);
  return m ? m[1] : null;
}

/** 参数是否"只有类型、没有名字"：抹掉类型关键字与标点后什么都不剩就是未命名 */
function isTypeOnly(p) {
  let s = p.replace(/\b(const|volatile|register|static|unsigned|signed|struct|union|enum|long|short)\b/g, ' ');
  s = s.replace(/\b(void|char|int|float|double|bool|_Bool|size_t|wchar_t|int8_t|int16_t|int32_t|int64_t|uint8_t|uint16_t|uint32_t|uint64_t|[A-Za-z_]\w*_t)\b/g, ' ');
  s = s.replace(/[[\]*&,.()]/g, ' ');
  return s.trim() === '';
}

const NOT_A_FUNCTION = ['CG_TEST_SUITE', 'CG_TEST', 'CG_TEST_EQ', 'CG_API', 'CG_KIT_API',
  'if', 'sizeof', 'return', 'switch', 'while', 'for', 'defined', 'assert'];

function indexFunctions(src) {
  const lines = src.split('\n');
  const fns = [];
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i];
    if (line === '' || /^\s/.test(line)) continue;      // 只看顶格行
    if (/^[#}\/*]/.test(line)) continue;
    // 第三方 ABI 声明不是本工程的函数，§1 的注释口径管不到它们：
    //   ① __declspec(dllimport) 的 GDI+ 系列；
    //   ② 运行期 GetProcAddress 取的 DLL 函数指针 typedef（typedef DWORD (WINAPI *Fn)(...)）
    if (/^__declspec|^extern\s+"C"/.test(line)) continue;
    if (/\bWINAPI\s+\*/.test(line)) continue;
    if (!/\(/.test(line)) continue;
    if (/\b(if|for|while|switch|return|else)\b/.test(line)) continue;

    let sig = line, j = i, kind = null;
    while (j < lines.length) {
      const cur = lines[j].trim();
      if (/\{\s*$/.test(cur) || /(\)|\])\s*\{\s*$/.test(cur)) { kind = 'def'; break; }
      if (/;\s*$/.test(cur)) { kind = 'decl'; break; }
      j++;
      if (j >= lines.length || j - i > 20) break;
      sig += ' ' + lines[j].trim();
    }
    if (!kind) continue;
    if (!/\)\s*(\{|;)\s*$/.test(sig.trim())) continue;

    const pg = paramGroup(sig);
    if (!pg) continue;
    const nameEnd = pg.nameEnd.trim();
    if (/^(struct|union|enum)\s+\w+$/.test(nameEnd)) continue;
    if (!/[A-Za-z_]\w*\s*$/.test(nameEnd)) continue;
    // 函数指针 typedef（typedef void (*cg_event_fn)(...)）的名字在括号里，别用返回类型当名字
    const fp = sig.match(/\(\s*\*\s*([A-Za-z_]\w*)\s*\)/);
    const name = (fp && sig.indexOf('(') === sig.indexOf(fp[0])) ? fp[1]
               : nameEnd.match(/([A-Za-z_]\w*)$/)[1];
    if (NOT_A_FUNCTION.includes(name)) continue;

    const named = [];
    for (const p of splitTop(pg.text)) {
      if (p === 'void' || p === '...') continue;
      const pn = paramName(p);
      if (pn && !isTypeOnly(p)) named.push(pn);
    }

    // 向上找紧邻的 /** 文档块
    let k = i - 1;
    while (k >= 0 && lines[k].trim() === '') k--;
    let docStart = -1;
    if (k >= 0 && /\*\/\s*$/.test(lines[k])) {
      let m = k;
      while (m >= 0) {
        if (/^\s*\/\*\*/.test(lines[m])) { docStart = m; break; }
        const t = lines[m];
        if (t.trim() !== '' && !/^\s*\*/.test(t) && !/\*\/\s*$/.test(t)) break;
        if (/^\s*\/\*[^*]/.test(t)) break;
        m--;
      }
    }
    const doc = docStart >= 0 ? lines.slice(docStart, k + 1).join('\n') : null;
    const paramTags = doc ? [...doc.matchAll(/@param\s+([A-Za-z_]\w*)/g)].map(x => x[1]) : [];
    const problem = [];
    if (!doc) problem.push('无注释');
    else {
      const tags = [...doc.matchAll(/@(\w+)/g)].map(x => x[1]).filter(t => t !== 'file' && t !== 'details');
      if (tags[0] !== 'brief') problem.push('首个 tag 不是 @brief');
      if (!/@brief/.test(doc)) problem.push('缺 @brief');
      for (const p of named) if (!paramTags.includes(p)) problem.push(`缺 @param ${p}`);
      if (paramTags.length > named.length) problem.push(`@param 多出（签名 ${named.length} 个）`);
      if (!/@return/.test(doc)) problem.push('缺 @return');
    }
    fns.push({ line: i + 1, name, kind, problem });
  }
  return fns;
}

/* ---------------- 子命令 ---------------- */
const cmd = process.argv[2] || 'check';

if (cmd === 'normalize') {
  process.stdout.write(normalizeCode(fs.readFileSync(process.argv[3], 'utf8')) + '\n');
  process.exit(0);
}

if (cmd === 'codegate') {
  const { execSync } = await import('node:child_process');
  const out = execSync('git status --porcelain', { cwd: ROOT }).toString();
  const changed = out.split('\n').filter(l => l.trim() !== '')
    .map(l => l.slice(3).replace(/^"|"$/g, '').trim())
    .filter(p => /\.(c|h)$/.test(p));
  let bad = 0;
  for (const rel of changed) {
    let head;
    try { head = execSync(`git show HEAD:${rel}`, { cwd: ROOT, maxBuffer: 64 * 1024 * 1024 }).toString(); }
    catch { console.log(`  [新文件] ${rel}`); continue; }
    const cur = fs.readFileSync(path.join(ROOT, rel), 'utf8');
    if (normalizeCode(head) === normalizeCode(cur)) console.log(`  ok    ${rel}`);
    else { bad++; console.log(`  CODE-DIFF! ${rel}  ← 有代码被改动（去注释后 token 不一致）`); }
  }
  console.log(`\n== codegate：${changed.length} 个改动文件，代码被动过 ${bad} 个`);
  process.exit(bad ? 1 : 0);
}

const rows = [];
let totFn = 0, badFn = 0, badFile = 0;
for (const f of allFiles()) {
  const src = fs.readFileSync(f, 'utf8');
  const rel = path.relative(ROOT, f).replace(/\\/g, '/');
  const headerOk = /@file/.test(src.split('\n').slice(0, 30).join('\n')) &&
                   /@brief/.test(src.split('\n').slice(0, 30).join('\n'));
  const fns = indexFunctions(src);
  const bad = fns.filter(x => x.problem.length);
  totFn += fns.length; badFn += bad.length;
  if (!headerOk) badFile++;
  if (bad.length || !headerOk) rows.push({ rel, headerOk, fns, bad });
}

rows.sort((a, b) => b.bad.length - a.bad.length);
for (const r of rows) {
  if (cmd === 'check') {
    console.log(`--- ${r.rel}  ${r.fns.length - r.bad.length}/${r.fns.length} 合规${r.headerOk ? '' : '   [文件头缺 @file/@brief]'}`);
    for (const fn of r.bad) console.log(`      L${fn.line} ${fn.name} [${fn.kind}] : ${fn.problem.join('; ')}`);
  }
}
console.log(`\n== 函数 ${totFn - badFn}/${totFn} 合规；待整改 ${badFn} 处；文件头不合规 ${badFile} 个`);
process.exit(cmd === 'gate' && (badFn || badFile) ? 1 : 0);
