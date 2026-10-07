/**
 * @file    check-skeleton.mjs
 * @brief   「工程是 C 还是 C++」判据的行为门禁（designer\electron\skeleton-lang.mjs）
 * @details 为什么需要它：首次导出选语言这条新规则，最坏的失败模式是**破坏既有骨架** ——
 *   往 C 工程里落一份 `main.cpp`（或反过来）＝同一个工程两套入口，链接重复定义，
 *   而用户的业务代码往往正写在另一份里。这套判据必须被机器锁住：
 *
 *   - `npx tsc --noEmit` 看不见它（electron\*.mjs 不在 tsconfig 里）；
 *   - `node --check`（check-electron.mjs）只保语法不保行为；
 *   - 起真画布点一遍是人工动作，回归不起。
 *
 *   所以这里用真文件系统（临时目录，跑完即删）把判据逐条钉死。
 * 用法：node tools\check-skeleton.mjs        退出码 0 = 全过；1 = 有断言失败
 */
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { skeletonLangOf, resolveLang, implFileName, entryFileName, langWriteList } from "../designer/electron/skeleton-lang.mjs";

let failed = 0;
function ok(cond, label) {
  if (cond) {
    console.log(`  [ok] ${label}`);
  } else {
    failed += 1;
    console.log(`[fail] ${label}`);
  }
}

/* 临时目录当"工程"，existsSync 走的是磁盘不是打桩 —— 判据与主进程用的是同一个 */
const dir = fs.mkdtempSync(path.join(os.tmpdir(), "cg-skeleton-"));
const exists = (p) => fs.existsSync(path.join(dir, p));

try {
  console.log("== skeletonLangOf：判据是回调实现文件在不在 ==");
  ok(skeletonLangOf("demo", exists) === "", "空目录 → \"\"（首次导出，该问用户选语言）");

  fs.writeFileSync(path.join(dir, "demo_ui_events.c"), "/* C 版业务 */");
  ok(skeletonLangOf("demo", exists) === "c", "只有 _ui_events.c → c（哪怕 main.c 还没生成）");

  fs.writeFileSync(path.join(dir, "main.c"), "int main(void){return 0;}");
  ok(skeletonLangOf("demo", exists) === "c", "main.c 在不在不影响判定（入口可删可重写）");

  fs.writeFileSync(path.join(dir, "demo_ui_events.cpp"), "/* C++ 版业务 */");
  ok(skeletonLangOf("demo", exists) === "cpp", "两份都在 → cpp（这种局面不该出现，保更晚建的那套）");

  console.log("== resolveLang：磁盘现状优先 ==");
  ok(resolveLang({ requested: "cpp", existing: "c" }) === "c", "已是 C 骨架，用户选 C++ 也按 c 落");
  ok(resolveLang({ requested: "c", existing: "cpp" }) === "cpp", "已是 C++ 骨架，用户选 C 也按 cpp 落");
  ok(resolveLang({ requested: "cpp", existing: "" }) === "cpp", "首次导出：用户选 C++ 就是 cpp");
  ok(resolveLang({ requested: undefined, existing: "" }) === "c", "首次导出又没选：默认 c");

  console.log("== langWriteList：另一套语言的源文件一个字节都不写 ==");
  const names = [
    "demo.cgui", "demo_ui_events.h", "demo_ui_events.c", "demo_ui_events.cpp",
    "main.c", "main.cpp", "demo.rc", "build.bat",
  ];
  const cPlan = langWriteList("demo", "c", names);
  ok(cPlan.write.includes("main.c") && cPlan.write.includes("demo_ui_events.c"), "C 工程：两份 C 源都写");
  ok(!cPlan.write.includes("main.cpp") && cPlan.skip.includes("main.cpp"), "C 工程：main.cpp 被挡下");
  ok(!cPlan.write.includes("demo_ui_events.cpp") && cPlan.skip.includes("demo_ui_events.cpp"), "C 工程：_ui_events.cpp 被挡下");
  ok(cPlan.write.includes("demo.cgui") && cPlan.write.includes("build.bat"), "非语言专属的文件照写（.cgui/.rc/build.bat）");

  const cppPlan = langWriteList("demo", "cpp", names);
  ok(cppPlan.write.includes("main.cpp") && cppPlan.write.includes("demo_ui_events.cpp"), "C++ 工程：两份 C++ 源都写");
  ok(cppPlan.skip.includes("main.c") && cppPlan.skip.includes("demo_ui_events.c"), "C++ 工程：两份 C 源都被挡下");

  console.log("== 文件名口径与 codegen 一致 ==");
  ok(implFileName("my_app", "cpp") === "my_app_ui_events.cpp", "implFileName(cpp) = <base>_ui_events.cpp");
  ok(entryFileName("c") === "main.c", "entryFileName(c) = main.c");

  if (failed) {
    console.log(`\n[fail] skeleton-lang 行为门禁：${failed} 条断言失败`);
    process.exit(1);
  }
  console.log("\n[ok] skeleton-lang 行为门禁全过（判据没被改坏）");
} finally {
  fs.rmSync(dir, { recursive: true, force: true });
}
