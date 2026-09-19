/* gen-cc.mjs -- 生成 compile_commands.json（clangd / IDE 语言服务的输入）
 *
 * 背景：include 路径写在 build-app.bat / build-runtime.bat 里，编译没问题，
 * 但 clangd 拿不到编译命令，会把 `#include "cstackgui.h"` 标成找不到。
 * 这个脚本把「真正编译时用的那套参数」同步成 compile_commands.json，
 * 参数（-I、-std、宏）以这里为准，改了构建脚本要同步改这里。
 *
 * 运行：node tools\gen-cc.mjs（在项目根；build-runtime.bat / build-app.bat 成功后也会自动刷）
 * 产物：compile_commands.json（相对路径，内容机器无关；是否入库随项目定，当前 gitignore）
 */
import { readdirSync, writeFileSync, existsSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
/** 项目内相对路径 + 正斜杠：compile_commands.json 内容机器无关，换机/换盘不变。
 *  clangd 会把相对的 directory/file 解析到本 json 所在目录。 */
const rel = (p) => {
  const withSlashes = p.split(path.sep).join("/");
  const rootPrefix = root.split(path.sep).join("/") + "/";
  return withSlashes.startsWith(rootPrefix) ? withSlashes.slice(rootPrefix.length) : withSlashes;
};

/** 与 build-runtime.bat / build-app.bat 保持一致的公共参数 */
const commonFlags = [
  "-std=c11",
  "-Wall",
  "-Wextra",
  "-Wno-unused-parameter",
  "-Iruntime/include",
  "-D_CRT_SECURE_NO_WARNINGS",
];

/** 各翻译单元额外需要的参数（和对应构建脚本一致） */
function extraFlags(file) {
  const flags = [];
  if (file.includes(`${path.sep}runtime${path.sep}`)) {
    flags.push("-Iruntime/src");
  }
  if (path.basename(file) === "cg_win32.c") {
    /* cg_win32.c 明确走 W 系列 API */
    flags.push("-DUNICODE", "-D_UNICODE");
  }
  return flags;
}

/** 枚举目录下所有子目录；发布包里 runtime\src、apps\ 可能不存在，缺了就跳过 */
const list = (base) => {
  const dir = path.join(root, base);
  if (!existsSync(dir)) return [];
  return readdirSync(dir, { withFileTypes: true })
    .filter((d) => d.isDirectory())
    .map((d) => path.join(dir, d.name));
};
/* apps\ 也要：用户自己的工程同样享受 clangd；
 * runtime\src 发布包里没有（只有预编译库），缺了跳过 */
const runtimeSrc = path.join(root, "runtime", "src");
const dirs = [...(existsSync(runtimeSrc) ? [runtimeSrc] : []), ...list("apps"), ...list("examples")];

const entries = [];
for (const dir of dirs) {
  for (const f of readdirSync(dir)) {
    if (!f.endsWith(".c")) continue;
    const abs = path.join(dir, f);
    const obj = path.join(root, "build", "obj", f.replace(/\.c$/, ".o"));
    entries.push({
      directory: ".",
      file: rel(abs),
      command: `gcc ${[...commonFlags, ...extraFlags(abs)].join(" ")} -c ${rel(abs)} -o ${rel(obj)}`,
    });
  }
}

const out = path.join(root, "compile_commands.json");
writeFileSync(out, JSON.stringify(entries, null, 2), "utf8");
console.log(`[ok] ${out}（${entries.length} 个翻译单元）`);
