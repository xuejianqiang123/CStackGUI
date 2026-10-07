/* skeleton-lang.mjs -- 「这个工程是 C 还是 C++」的唯一判水龙头
 *
 * 病：同一个工程里一旦落下两套源文件（`main.c` 与 `main.cpp`、
 * `<base>_ui_events.c` 与 `.cpp`），链接就是**重复定义**；而用户的业务代码
 * 往往正写在另一份里 —— 画布只消漏挡一次，就是"导出之后工程编不过了"。
 *
 * 治法：语言只在**首次导出**（目录里还没有骨架）时由用户选；此后一律以磁盘现状为准，
 * 并且"不属于当前语言的那两个源文件"一个字节都不写。这里把这套判据收成纯函数：
 * 纯函数才能在 tools\check-skeleton.mjs 里跑真断言（electron 起不来也能测），
 * 且 main.mjs 不会再有第二处写着 `main.c` 字面量的分支。
 *
 * 纯函数、零 electron 依赖：tools\check-electron.mjs（按目录 glob）会自动做语法门禁。
 */

/** 语言 -> 源文件后缀 */
const EXT = { c: "c", cpp: "cpp" };

/** 回调实现文件名：`<base>_ui_events.c` / `.cpp` */
export const implFileName = (prefix, lang) => `${prefix}_ui_events.${EXT[lang] ?? "c"}`;
/** 入口文件名：`main.c` / `main.cpp` */
export const entryFileName = (lang) => (lang === "cpp" ? "main.cpp" : "main.c");

/**
 * 目录里现在用的是哪种语言？
 * @param {string} prefix `<base>_ui_events.*` 的前缀
 * @param {(p: string) => boolean} exists 文件是否存在（主进程传 existsSync）
 * @returns {"" | "c" | "cpp"} "" = 一份骨架都还没有（首次导出）
 * @note 判据是**回调实现文件**而不是入口：入口可以被用户删掉重写，
 *       而 `<base>_ui_events.*` 才是"业务已经写在里面了"的那一份。
 *       两份都有时按 cpp —— 那种局面本身就不该出现，宁可保住更晚建的那套。
 */
export function skeletonLangOf(prefix, exists) {
  if (exists(`${prefix}_ui_events.${EXT.cpp}`)) return "cpp";
  if (exists(`${prefix}_ui_events.${EXT.c}`)) return "c";
  return "";
}

/**
 * 这次到底按哪种语言落文件。
 * @param {{ requested?: string, existing?: string }} o requested = 用户这次选的；existing = 磁盘现状（可能为 ""）
 * @returns {"c" | "cpp"} 磁盘现状优先 —— 已有骨架时用户的选项被忽略
 */
export function resolveLang({ requested, existing }) {
  if (existing === "c" || existing === "cpp") return existing;
  return requested === "cpp" ? "cpp" : "c";
}

/**
 * 这次哪些源文件该写、哪些整个跳过。
 * @param {string} prefix `<base>_ui_events.*` 的前缀
 * @param {"c"|"cpp"} lang 本次生效的语言
 * @param {string[]} names 生成器给出的全部文件名
 * @returns {{ write: string[], skip: string[] }}
 * @note 语言专属的四个文件（两套 `<base>_ui_events.*` / `main.*`）里，
 *       不属于 lang 的一律进 skip —— 这是"不破坏既有骨架"的最后一道闸：
 *       哪怕渲染端按默认语言生成了一版文件送过来，也落不到磁盘上。
 */
export function langWriteList(prefix, lang, names) {
  const mine = new Set([implFileName(prefix, lang), entryFileName(lang)]);
  const owned = (n) => n === `${prefix}_ui_events.c` || n === `${prefix}_ui_events.cpp`
                    || n === "main.c" || n === "main.cpp";
  const write = [];
  const skip = [];
  for (const n of names) {
    (owned(n) ? mine.has(n) : true) ? write.push(n) : skip.push(n);
  }
  return { write, skip };
}
