/* probe-dialog-e2e.mjs -- F2 验收 A7 的端到端：画布模型 → generateC → cgbuild → 真窗口
 *
 * 入库位置说明：tools\ 下（build\ 整个不入库）——它守的是 F2 唯一一条"全链"验收 A7：
 * 画布造的 dialog 模型 → 生成的工程 → 编译 → 跑起来真的有那个窗口。真窗口断言走
 * tools\findwin.c（C 驱动）：PowerShell P/Invoke 的 Add-Type 委托在两轮跑法下都静默失灵，
 * 连已知有窗口的 modaltest 都"枚举不到" —— 工具不可靠时换工具，别把"工具找不到"
 * 当成"应用没窗口"。慢（要编译整个工程 + 起真窗口），**不进 check-format**，手动复跑。
 *
 * 补上此前一直没一次跑通的那条链：modaltest 证明过运行时 open/close（手写工程），
 * roundtrip 证明过画布往返（不落 exe）——但"画布模型出文档 → generateC 出工程 →
 * 编译 → 跑起来真的看到对话框窗口"没有串起来验证过。
 *
 * 链条：
 *   1. 模型 API 造一份带 dialog 的文档（center:false —— 顺带把"不居中定位"这个
 *      从未真机验证过的分支也验掉：rect.x/y 按屏幕坐标，PER_MONITOR_AWARE_V2 下就是物理 (40,40)）
 *   2. generateC 出全套工程文件（回调桩是空的 = 产品承诺的"用户代码区"起点）
 *      —— 同时核对 A8：文件清单与多窗体之前完全一致，仍然只有一份 .cgui
 *   3. 按"用户写的那一行"给 on_open 桩补 cg_dialog_open(cg_find(...))
 *      （app.events.ready 绑定它：启动即开，省去点击驱动）
 *   4. cgbuild 编译 → 起 exe → PowerShell P/Invoke 找真窗口断言：
 *      对话框存在、位置 (40,40)、owner 被禁用（A2 回归）
 *
 * 依赖：build\modeljs（tsc 命令见 probe-merge-e2e.mjs 头部）+ MinGW（cgbuild 自探测）。
 */
import fs from "node:fs";
import path from "node:path";
import { execFileSync, spawn } from "node:child_process";
import { fileURLToPath } from "node:url";
import { generateC } from "../build/modeljs/codegen.js";
import { emptyDoc, makeNode } from "../build/modeljs/model.js";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const dir = path.join(root, "build", "dlge2e");

let fails = 0;
const ok = (name, cond, extra = "") => {
  if (!cond) fails += 1;
  console.log(`${cond ? "PASS" : "FAIL"} ${name}${extra ? `：${extra}` : ""}`);
};

/* ---------------- 1. 画布模型造文档 ---------------- */
const doc = emptyDoc();
doc.app.title = "dlg e2e";
doc.app.events = { ready: "on_open" };

const dlg = makeNode("dialog", 40, 40);
dlg.id = "dlg_e2e";
dlg.text = "E2E dialog";
dlg.rect.w = 300; /* 注意是 rect.w/h：makeNode 造的节点尺寸住在 rect 里，
                     顶层 .w/.h 会被序列化忽略（探针第一版就踩了这儿） */
dlg.rect.h = 160;
dlg.center = false; /* rect.x/y 即屏幕坐标（40,40）——顺带验不居中定位 */
const lab = makeNode("label", 16, 40);
lab.id = "lab_note";
lab.text = "child inside dialog";
dlg.children = [lab];
doc.widgets.push(dlg);

/* ---------------- 2. generateC：出工程文件 + A8 证据 ---------------- */
const files = generateC(doc, "dlge2e");
ok(
  "A7 generateC 出齐工程文件（与多窗体之前同一份清单）",
  files.map((f) => f.name).join(",") ===
    "dlge2e_ui_events.h,dlge2e_ui_events.c,main.c,dlge2e.cgui,dlge2e.rc,build.bat",
  files.map((f) => f.name).join(", "),
);
ok("A8 仍然只有一份 .cgui（单文件发布面不变）", files.filter((f) => f.name.endsWith(".cgui")).length === 1);

const cgui = files.find((f) => f.name === "dlge2e.cgui").content;
ok("center:false 随序列化落进 .cgui", cgui.includes('"center": false'));

/* ---------------- 3. "用户写的那一行" ---------------- */
const STUB_HEAD = "void on_open(cg_widget_t *self, cg_event_t ev, void *user)\n{\n";
const OPEN_LINES =
  '    cg_app_t *app = cg_app_of(self);\n    cg_dialog_open(cg_find(app, "dlg_e2e"));\n';
const stubFile = files.find((f) => f.name === "dlge2e_ui_events.c");
const patched = stubFile.content.replace(STUB_HEAD, STUB_HEAD + OPEN_LINES);
ok("回调桩补上 cg_dialog_open（= 用户在画布导出后写的那一行）", patched.includes('cg_dialog_open(cg_find(app, "dlg_e2e"))'));

/* ---------------- 4. 落盘 + cgbuild ---------------- */
fs.rmSync(dir, { recursive: true, force: true });
fs.mkdirSync(dir, { recursive: true });
for (const f of files) {
  fs.writeFileSync(path.join(dir, f.name), f.name === "dlge2e_ui_events.c" ? patched : f.content, "utf8");
}
let buildOut = "";
try {
  buildOut = execFileSync("node", [path.join(root, "designer", "electron", "cgbuild.mjs"), dir], {
    encoding: "utf8",
  });
} catch (e) {
  buildOut = String(e.stdout ?? "") + String(e.stderr ?? "");
}
ok("A7 cgbuild 编译通过", !/\[fail\]|error:/i.test(buildOut), buildOut.split("\n").filter((l) => /\[fail\]|error:/i.test(l)).join(" | ") || "无错误行");
ok("exe 真的出来了", fs.existsSync(path.join(dir, "dlge2e.exe")));

/* ---------------- 5. 起真进程 + 真窗口断言（C 驱动 findwin.c，probe-modal 同款可靠手法）
 *      弃用 PowerShell P/Invoke：它的 Add-Type 委托在两轮跑法下都静默失灵
 *      （连已知有窗口的 modaltest 都枚举不到），而 C 驱动一次就对 —— 工具不可靠时换工具，
 *      别把"工具找不到"当成"应用没窗口"。 ---------------- */
const findwinSrc = path.join(root, "tools", "findwin.c");
const findwinExe = path.join(root, "build", "findwin.exe");
if (!fs.existsSync(findwinExe)) {
  const gcc = (cmd) => {
    try {
      execFileSync(cmd, [findwinSrc, "-o", findwinExe, "-luser32"], { stdio: "ignore" });
      return true;
    } catch {
      return false;
    }
  };
  if (!gcc("gcc") && !gcc("C:\\mingw64\\bin\\gcc.exe")) {
    console.error("[fail] 编译不了 findwin.c（找不到 gcc）");
    process.exit(2);
  }
}

const appProc = spawn(path.join(dir, "dlge2e.exe"), [], { cwd: dir, detached: true, stdio: "ignore" });
appProc.unref();
await new Promise((r) => setTimeout(r, 800)); /* 启动 → ready 回调 → 模态泵，留一拍 */

let out = "";
try {
  out = execFileSync(findwinExe, [], { encoding: "utf8" });
} catch (e) {
  out = String(e.stdout ?? "") + String(e.stderr ?? "");
}
const get = (k) => (out.match(new RegExp(`${k}=(\\S+)`)) || [])[1];
const mainH = get("main");
const dlgH = get("dlg");
ok("主窗口起来了", Boolean(mainH && mainH !== "0"), `hwnd=${mainH ?? out.trim()}`);
ok("A7 对话框是屏幕上的真窗口", Boolean(dlgH && dlgH !== "0"), `hwnd=${dlgH ?? "未找到"}`);
const pos = get("pos");
ok(
  "center:false 定位 = rect.x/y 屏幕坐标 (40,40)",
  !!pos && Math.abs(Number(pos.split(",")[0]) - 40) <= 2 && Math.abs(Number(pos.split(",")[1]) - 40) <= 2,
  `pos=${pos ?? "?"}`,
);
ok("A2 回归：模态时 owner 被禁用", get("owner-enabled") === "0", `owner-enabled=${get("owner-enabled") ?? "?"}`);

/* 收尾：杀掉被测进程（它还停在模态泵里） */
try {
  execFileSync("taskkill", ["/IM", "dlge2e.exe", "/F"], { stdio: "ignore" });
} catch { /* 已经退了就算了 */ }

console.log(fails === 0 ? "\nALL PASS" : `\n${fails} FAILED`);
process.exitCode = fails === 0 ? 0 : 1;
