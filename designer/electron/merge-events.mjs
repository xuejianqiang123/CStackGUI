/* merge-events.mjs -- 重新导出时怎么对待已有的 <工程>_ui_events.c
 *
 * 病：画布上**新绑一个事件**后重新导出，`<工程>_ui_events.h` 会被重新生成（新原型 + 新的
 * `CG_BIND` 条目）、`main.c` 的 k_bindings[] 直接展开那个宏引用了它，而 `_ui_events.c`
 * 原先被整份跳过 —— 于是**新回调没有实现，链接直接 undefined reference**。
 * 用户唯一的出路是手抄一遍函数签名（抄错就是下一个坑，而运行时不会为此报错）。
 *
 * 治法：**只补不改**。
 *   - 老文件里缺哪个回调，就在末尾补上那个桩；
 *   - 老文件里已有的回调**原样留着**（哪怕它已经没有事件绑定了）——**永不删用户的代码**；
 *   - 一个都不缺时**逐字节不变**，而且调用方不写文件（mtime 不动，不会触发无谓的重新编译）。
 *
 * 为什么不是"整份重生成 + 区域标记保留"（py880 那种做法）：
 *   用户会在这份文件里加**标记之外**的东西 —— #include、全局变量、辅助函数、甚至整个 #if 0 块。
 *   整份重写的合并器认不出这些，**丢掉就是丢用户的代码**，而这个功能存在的全部意义恰恰是
 *   "不弄坏用户的代码"。要安全重写就得写个 C 小词法器（字符串/注释里的花括号、#if 分支），
 *   收益只是"刷新注释"（回调签名本来就固定不许改），不划算。
 *
 * 纯函数、零 electron 依赖：这样 tools\check-electron.mjs（按目录 glob）会自动做语法门禁，
 * Node 探针也能直接 import 它跑真断言。
 */

/** 行首的函数定义：`void on_ok_click(`。
 *  容错地允许前面的 `static` 与缩进 —— 认不出来的代价是**重复补一个桩**，
 *  那是 redefinition（比漏桩更难查），所以这里宁可松一点。
 *  `^\s*` 只锚在行首（不是"行内任意位置"），所以 printf 里出现 `void on_x(` 不会被误认。 */
const FN_HEAD = /^\s*(?:static\s+)?void\s+([A-Za-z_]\w*)\s*\(/;

/** 文件里出现过哪些回调名（按出现顺序，可能重复） */
function namesIn(text) {
  const out = [];
  for (const line of text.split("\n")) {
    const m = FN_HEAD.exec(line);
    if (m) out.push(m[1]);
  }
  return out;
}

/** 把一份生成内容切成「回调名 -> 整块源码」（含它紧邻的 Doxygen 注释）。
 *  只认生成器自己的形状：注释块紧贴签名、函数体以列 0 的 `}` 结束。 */
function splitBlocks(text) {
  const lines = text.split("\n");
  const blocks = new Map();
  for (let i = 0; i < lines.length; i++) {
    const m = FN_HEAD.exec(lines[i]);
    if (!m) continue;
    /* 往上吃掉紧贴着的 Doxygen 注释（生成器那份注释的收尾行紧贴着签名）——
       注意这里**不能**把注释收尾那两个字符写出来：它们会把这个块注释提前闭合，
       剩下的半句就变成代码（check-electron.mjs 这个门禁就是被同一种写法咬出来的）。 */
    let start = i;
    if (i > 0 && lines[i - 1].trim() === "*/") {
      let j = i - 1;
      while (j >= 0 && !lines[j].trim().startsWith("/**")) j -= 1;
      if (j >= 0) start = j;
    }
    /* 往下找到列 0 的 `}` —— 生成器写函数体时收尾就是这一行。
       找不到（内容被截断/形状不对）就**不认这个块**：宁可报出来不补，也不能把
       "只有签名没有函数体"的残块追加进用户的文件（那是编译错误，还可能插在文件中间）。 */
    let end = -1;
    for (let k = i; k < lines.length; k++) {
      if (lines[k] === "}") {
        end = k;
        break;
      }
    }
    if (end < 0) continue;
    if (!blocks.has(m[1])) blocks.set(m[1], lines.slice(start, end + 1).join("\n"));
  }
  return blocks;
}

/** 从 `<工程>_ui_events.h` 里读出它声明了哪些回调（`CG_BIND("名字", 名字), \`）。
 *  .h 是每次导出都重写的纯生成物，所以它 = "画布上次导出时绑定了哪些回调"的权威快照。 */
export function declaredInHeader(headerText) {
  return [...(headerText ?? "").matchAll(/CG_BIND\("([^"]+)"/g)].map((m) => m[1]);
}

/** 画布上**已不再绑定**、但 .c 里还留着的回调（用来提醒用户去处理，**不自动删**）。
 *  判据只能是"老 .h 里声明过、新 .h 里没有了" —— 拿 .c 里的函数名去比会把手写辅助函数
 *  也算进来（那些从来不是回调）。 */
export function callbackDiff(oldHeaderText, newHeaderText) {
  const after = new Set(declaredInHeader(newHeaderText));
  return declaredInHeader(oldHeaderText).filter((n) => !after.has(n));
}

const ADDED_BANNER = `/* ============================ 以下回调由画布自动补上 ============================
 * 画布上新绑了事件，而这份文件里原来没有它们的实现 —— 缺了它们链接会报 undefined reference。
 * 你自己写的内容**一个字节都没动**；下面这些函数就是你的，随便改（画布不会再覆盖这个文件）。
 */`;

/**
 * 把「新生成的骨架」并进「已有的 _ui_events.c」。
 *
 * @param {string} oldText 磁盘上已有的内容（空串/空白 = 首次导出）
 * @param {string} newText 本次生成的内容
 * @returns {{ content: string, added: string[], failed: string[], mode: "fresh" | "same" | "insert" }}
 *   content 要写入的文本；added 这次真的补上的回调名（顺序与生成器一致）；
 *   failed 想补却切不出整块的名字（**非空就说明出了意外，调用方要报出来**，别静默）；
 *   mode: fresh=首次写出 / same=不改（调用方**别写文件**）/ insert=补了桩
 */
export function mergeEvents(oldText, newText) {
  const old = oldText ?? "";
  const fresh = newText ?? "";

  if (!old.trim()) return { content: fresh, added: namesIn(fresh), failed: [], mode: "fresh" };

  const have = new Set(namesIn(old));
  const missing = namesIn(fresh).filter((n) => !have.has(n));
  /* 一个都不缺：把老文本原样交回去（**不是**空改动 —— 逐字节相同的承诺靠这里） */
  if (!missing.length) return { content: old, added: [], failed: [], mode: "same" };

  const blocks = splitBlocks(fresh);
  const add = missing.filter((n) => blocks.has(n));
  /* 同一份文本里"名字找得到、整块却切不出来"，只可能发生在调用方传了非生成器产物的时候。
     这时**宁可不动文件**（第一承诺），但必须**把名字报出去** —— 静默不补桩会让用户又回到
     "新绑的事件没有实现、链接 undefined reference"那个老坑，而且一点线索都没有。 */
  const failed = missing.filter((n) => !blocks.has(n));
  if (!add.length) return { content: old, added: [], failed, mode: "same" };

  const base = old.endsWith("\n") ? old : `${old}\n`;
  const content = `${base}\n${ADDED_BANNER}\n\n${add.map((n) => blocks.get(n)).join("\n\n")}\n`;
  return { content, added: add, failed, mode: "insert" };
}
