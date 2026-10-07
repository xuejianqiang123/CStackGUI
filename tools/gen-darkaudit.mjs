/* gen-darkaudit.mjs -- 生成「深色审计」工程：把 SDD T1 对照表里的每个部位摆进一页
 *
 * 用途：T1（内置皮肤深色化）前后各截一张图，逐部位核对——深色下不再"发亮的浅灰"，
 *       浅色下一像素不变。后来也成了角色（G1）@引用深浅两态的取证页。
 *       生成物在 build\darkaudit\（cgbuild 可随时重编）。
 * 依赖：build\modeljs（tsc 命令见 probe-merge-e2e.mjs 头部）。
 *
 * 入库位置说明：tools\ 下（build\ 整个不入库）——它是深色皮肤的**永久审计页**：
 * 深浅两态各一张截图，逐控件核对（禁用按钮 / meter / 无底色 edit / @引用取色）。
 */
import fs from "node:fs";
import path from "node:path";
import { execFileSync } from "node:child_process";
import { fileURLToPath } from "node:url";
import { generateC } from "../build/modeljs/codegen.js";
import { emptyDoc, makeNode } from "../build/modeljs/model.js";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const dir = path.join(root, "build", "darkaudit");

const doc = emptyDoc();
doc.app.title = "dark audit";
doc.app.dark = true;
doc.app.width = 580;
doc.app.height = 560;

const add = (n) => doc.widgets.push(n);
const label = (id, text, x, y, w) => {
  const n = makeNode("label", x, y);
  n.id = id;
  n.text = text;
  n.rect.w = w;
  n.rect.h = 18;
  add(n);
};

/* 按钮两态：普通（品牌紫 tonal，深色下保持）+ 禁用（T1 修：禁用灰在深底上发亮） */
const b1 = makeNode("button", 20, 40);
b1.id = "btn_normal";
b1.text = "普通按钮";
b1.rect.w = 120;
b1.rect.h = 34;
add(b1);
const b2 = makeNode("button", 20, 86);
b2.id = "btn_disabled";
b2.text = "禁用按钮";
b2.rect.w = 120;
b2.rect.h = 34;
b2.enabled = false;
add(b2);
label("lab_btn", "↑ 按钮普通/禁用", 150, 96, 140);

/* edit（无 bg：T1 修——客户区白底 + 边框带 COLOR_WINDOW） */
const e1 = makeNode("edit", 20, 136);
e1.id = "ed_plain";
e1.text = "bg-less edit";
e1.rect.w = 220;
e1.rect.h = 30;
add(e1);
label("lab_ed", "↑ 无背景色输入框", 250, 142, 150);

/* 复选 + 开关 + 进度 + 滑块（槽/框 T1 核对；选中紫保持） */
const c1 = makeNode("checkbox", 20, 184);
c1.id = "chk";
c1.text = "复选框";
c1.rect.w = 130;
c1.rect.h = 24;
c1.checked = true;
add(c1);
const sw = makeNode("switch", 170, 182);
sw.id = "sw";
sw.rect.w = 56;
sw.rect.h = 28;
sw.checked = true;
add(sw);
const sw2 = makeNode("switch", 240, 182);
sw2.id = "sw_off";
sw2.rect.w = 56;
sw2.rect.h = 28;
add(sw2);
label("lab_sw", "↑ 开/关开关", 310, 188, 120);
const pg = makeNode("progress", 20, 226);
pg.id = "pg";
pg.rect.w = 260;
pg.rect.h = 14;
pg.value = 62;
add(pg);
const sl = makeNode("slider", 20, 258);
sl.id = "sl";
sl.rect.w = 260;
sl.rect.h = 32;
sl.value = 40;
add(sl);

/* 圆环 / 仪表（T1 修：meter 指针帽深底不可见）/ 状态灯（灭点） */
const rg = makeNode("progressring", 320, 40);
rg.id = "rg";
rg.rect.w = 110;
rg.rect.h = 110;
rg.value = 40;
add(rg);
const mt = makeNode("meter", 440, 40);
mt.id = "mt";
mt.rect.w = 120;
mt.rect.h = 100;
mt.value = 70;
add(mt);
const ld = makeNode("led", 330, 165);
ld.id = "ld_off";
ld.rect.w = 20;
ld.rect.h = 20;
add(ld);
label("lab_ld", "↑ 灭态灯", 358, 168, 90);

/* 无底色标签：文字色跟主题（既有行为，回归核对） */
label("lab_plain", "无底色标签：文字色跟随主题（既有行为）", 20, 310, 380);

/* ---------------- G1/T2：色彩角色引用（深浅各取对应槽，A4） ---------------- */
doc.roles = {
  accent: { light: "#6750A4", dark: "#D0BCFF" },
  danger: { light: "#B3261E", dark: "#F2B8B5" },
};
const la = makeNode("label", 20, 344);
la.id = "lab_accent";
la.text = "fg=@accent 的标签（浅紫 / 深紫各按模式）";
la.rect.w = 380;
la.rect.h = 18;
la.style = { fg: "@accent" };
add(la);
const ldg = makeNode("label", 20, 368);
ldg.id = "lab_danger";
ldg.text = "fg=@danger 的标签（红 / 浅红各按模式）";
ldg.rect.w = 380;
ldg.rect.h = 18;
ldg.style = { fg: "@danger" };
add(ldg);
const br = makeNode("button", 420, 340);
br.id = "btn_role";
br.text = "bg=@accent";
br.rect.w = 130;
br.rect.h = 34;
br.style = { bg: "@accent" };
add(br);

/* ---------------- 生成工程 ---------------- */
const files = generateC(doc, "darkaudit");
fs.rmSync(dir, { recursive: true, force: true });
fs.mkdirSync(dir, { recursive: true });
for (const f of files) fs.writeFileSync(path.join(dir, f.name), f.content, "utf8");
console.log(`[ok] 写出 ${files.length} 个文件到 ${dir}`);
execFileSync("node", [path.join(root, "designer", "electron", "cgbuild.mjs"), dir], { stdio: "inherit" });
