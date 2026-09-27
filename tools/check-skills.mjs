/**
 * @file    check-skills.mjs
 * @brief   技能契约：技能目录 / INDEX.md / .codebuddy 的 README.md / 各 SKILL.md，四处说法必须一致
 * @details 为什么要有这个脚本：
 *
 *   本工程自己立过一条规矩 —— **"同一事实有两份的地方必须机器同步"**（根 README §3.4、
 *   INDEX.md 的维护清单），控件类型表、事件表、.cgui 字段都是靠 tools\check-format.bat 兜住的。
 *   "有哪些技能"恰恰也是这样一份事实：它同时出现在
 *     .codebuddy/skills/ 的目录名、INDEX.md 的分类表与总数、.codebuddy/README.md 的目录树、
 *     以及每个 SKILL.md 的 frontmatter 里。
 *
 *   2026-09-27 清理 13 个技能时，靠人工同步了 6 个文件、40 处引用（写了带断言的迁移脚本才敢动手）。
 *   这种事不该靠记性：漏一处就是"文档指着不存在的技能"，而**没有任何命令会因此变红**。
 *
 *   本脚本只做静态断言，不修任何东西：
 *     1) ls .codebuddy/skills/ 的目录数 == INDEX.md 顶部"技能总数：**N**"
 *     2) 每个技能目录名都出现在 INDEX.md 里（新加技能没进索引 ⇒ 红）
 *     3) 每个技能目录名都出现在 .codebuddy/README.md 里（它的目录树声称列全部 28 个）
 *     4) INDEX.md / README.md / global-rule.md 里被反引号包裹、**长得像技能名**的 token
 *        （`piv-*` / `plan-*` / `prime-*` / `skills-*` / `*-check` / `*_check` / `*-style` …）
 *        必须要么是存在的技能目录，要么出现在"清理记录"行上（含 清理/已删/删除/原来/更正/v0.1/v0.3）
 *        —— 这条专门抓"删了技能、别处还引用着"。
 *     5) 每个 SKILL.md 的 frontmatter：`name:` 等于目录名、且有 `description:`
 *        （skills-create 的规范；名字与目录不一致时，agent 按名字调用会找不到）
 *     6) settings.json 的 scopes 不许再出现固件词（embedded / esp32 / rtos / firmware）——
 *        那是"本工程是什么"的活配置，写错会把上下文带偏。
 *
 * 用法：node tools\check-skills.mjs
 * 退出码：0 = 四处一致；1 = 有漂移（逐条给出文件:行号与修法）
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const SKILLS = path.join(root, ".codebuddy", "skills");
const INDEX = path.join(root, ".codebuddy", "skills", "INDEX.md");
const AIDOC = path.join(root, ".codebuddy", "README.md");
const RULE = path.join(root, ".codebuddy", "global-rule.md");
const SETTINGS = path.join(root, ".codebuddy", "settings.json");

const bad = [];
const fail = (where, msg) => bad.push(`[fail] ${where}：${msg}`);
const lines = (p) => fs.readFileSync(p, "utf8").split(/\r?\n/);

/* ① 目录 */
const dirs = fs
  .readdirSync(SKILLS, { withFileTypes: true })
  .filter((e) => e.isDirectory())
  .map((e) => e.name)
  .sort();

/* ② INDEX 的总数 */
const indexLines = lines(INDEX);
const totalLine = indexLines.find((l) => l.includes("技能总数"));
const total = totalLine && totalLine.match(/\*\*(\d+)\*\*/);
if (!total) fail("INDEX.md", '找不到"技能总数：**N**"这一行（它是口径的唯一出处）');
else if (Number(total[1]) !== dirs.length)
  fail("INDEX.md", `技能总数写 **${total[1]}**，实际目录 ${dirs.length} 个 —— 改技能后要同步这个数`);

/* ③④ 名字必须出现在 INDEX 与 README；不存在的名字必须落在清理记录行上 */
const indexText = indexLines.join("\n");
const aidocLines = lines(AIDOC);
const aidocText = aidocLines.join("\n");
const ruleText = lines(RULE).join("\n");

for (const d of dirs) {
  if (!indexText.includes(d)) fail("INDEX.md", `技能目录 ${d}/ 没在索引里出现（新增技能必须登记）`);
  if (!aidocText.includes(d)) fail(".codebuddy/README.md", `技能目录 ${d}/ 没在导读的目录树里出现`);
}

const CLEANUP = /清理|已删|删除|原来|更正|恢复|合并|v0\.1|v0\.3|不是固件|只在/;
const NAMEISH = /^(piv-|plan-|prime-|skills-|team-|designer-|design-)|(_check|-check|-style|-taste|-verify|-ops|-delivery|-thinking|-projects|_overflow|_leak)$/;
const isSkillish = (t) => NAMEISH.test(t) && (t.includes("-") || t.includes("_"));

/** 把连续的块引用行（">" 开头）合成"一条逻辑行"：清理记录常写成多条引用行，
 *  续行本身不带"清理"字样，逐行判会把合法记录误报成断链。行号取首行。 */
function logicalLines(src) {
  const out = [];
  for (let i = 0; i < src.length; i++) {
    const l = src[i];
    const prev = out[out.length - 1];
    if (l.startsWith(">") && prev && prev.quote) {
      prev.text += " " + l;
      continue;
    }
    out.push({ line: i + 1, text: l, quote: l.startsWith(">") });
  }
  return out;
}

for (const [f, text] of [["INDEX.md", indexLines], [".codebuddy/README.md", aidocLines], ["global-rule.md", lines(RULE)]]) {
  for (const { line, text: l } of logicalLines(text)) {
    for (const m of l.matchAll(/`([a-z][a-z0-9_-]{3,40})`/g)) {
      const t = m[1];
      if (!isSkillish(t)) continue;
      if (dirs.includes(t)) continue;
      if (CLEANUP.test(l)) continue;          // 清理记录 / 更新日志里提到已删技能，是允许的
      fail(f, `第 ${line} 行引用了不存在的技能 \`${t}\` —— 要么建它，要么改成存在的，要么写进清理记录行`);
    }
  }
}

/* ⑤ frontmatter */
for (const d of dirs) {
  const p = path.join(SKILLS, d, "SKILL.md");
  if (!fs.existsSync(p)) {
    fail(`skills/${d}/`, "没有 SKILL.md（技能目录必须是 <name>/SKILL.md）");
    continue;
  }
  const head = fs.readFileSync(p, "utf8").split(/\r?\n/).slice(0, 12).join("\n");
  const name = head.match(/^name:\s*(\S+)/m);
  if (!name) fail(`skills/${d}/SKILL.md`, "frontmatter 缺 name");
  else if (name[1] !== d) fail(`skills/${d}/SKILL.md`, `name: ${name[1]} 与目录名 ${d} 不一致（按名字调用会找不到）`);
  if (!/^description:\s*\S/m.test(head)) fail(`skills/${d}/SKILL.md`, "frontmatter 缺 description（agent 靠它决定何时调用）");
}

/* ⑥ settings.json 的项目范围 */
const scopes = fs.readFileSync(SETTINGS, "utf8").match(/"scopes"\s*:\s*\[([^\]]*)\]/);
if (!scopes) fail("settings.json", "找不到 scopes");
else {
  const FW = ["embedded", "esp32", "rtos", "firmware", "stm32", "mcu", "keil"];
  const hit = FW.filter((w) => scopes[1].toLowerCase().includes(w));
  if (hit.length) fail("settings.json", `scopes 里还有固件词 ${hit.join(" / ")} —— 本工程是 Win32 桌面工具包 + 画布`);
}

if (bad.length) {
  console.log(bad.join("\n"));
  console.log(`\n[fail] 技能契约不成立：${bad.length} 处（技能目录 ${dirs.length} 个）`);
  process.exit(1);
}
console.log(`[ok] 技能契约成立：${dirs.length} 个技能 —— 目录 / INDEX.md（总数与分类）/ 导读目录树 / frontmatter / settings.json scopes 四处一致`);
