/**
 * @file    check-routing.mjs
 * @brief   技能路由用例校验：触发词必须真在 description 里 / 不许第二个技能同时命中 / 每个技能至少被点名
 * @details 为什么要有它：
 *
 *   本工程 38 个技能全靠 `auto_invoke` + `description` 触发，但**没有任何回归基线**。
 *   后果是两件没人发现的事：
 *     ① description 里没写某个触发词 ⇒ 用户说那句话，模型永远选不中这个技能（静默失效）；
 *     ② 两个技能的 description 同时命中同一串触发词 ⇒ 路由歧义，选谁看运气。
 *   两者都不会让任何命令变红，只会在某天表现为"它怎么没按规矩来"。
 *
 *   判据是**可证伪的静态代理**（不跑 LLM：CI 里没有模型，真跑既贵又不稳定）：
 *     A. 每条用例的每个 keyword，必须出现在 expect 技能的 description 里；
 *     B. 除 expect 与显式 allow 外，**不允许**第二个技能的 description 同时命中全部 keyword；
 *     C. 每个技能都必须被至少一条用例点名（新增技能不补用例 ⇒ 红）。
 *
 *   真值文件：`.codebuddy/skills/_registry/routing_cases.json`
 *     （用 JSON 而不是上游的 TOML：本工程是 Node，JSON 是原生能力，零依赖 ——
 *       选格式的理由是"运行时原生支持"，不是"好看"。）
 *
 * 用法：
 *   node tools\check-routing.mjs          校验
 *   node tools\check-routing.mjs --dump   打印每个技能的 name + description（写用例时查词用）
 * 退出码：0 = 通过；1 = 有漂移
 */
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const SKILLS = path.join(root, ".codebuddy", "skills");
const CASES = path.join(SKILLS, "_registry", "routing_cases.json");

const bad = [];
const fail = (where, msg) => bad.push(`[fail] ${where}：${msg}`);

/** 读一个技能目录的 frontmatter（name / description，description 只取首行） */
function readSkillAt(file, dir) {
  if (!fs.existsSync(file)) return null;
  const head = fs.readFileSync(file, "utf8").split(/\r?\n/).slice(0, 15).join("\n");
  const name = (head.match(/^name:\s*(\S+)/m) || [])[1];
  const desc = (head.match(/^description:\s*(.+)$/m) || [])[1] || "";
  return { dir, name: name || dir, desc };
}

/* 物理分层后：`skills/` 顶层是「入口 + 团队目录」，成员在 `skills/<team>/<skill>/` */
const skills = [];
for (const e of fs.readdirSync(SKILLS, { withFileTypes: true })) {
  if (!e.isDirectory() || e.name.startsWith("_")) continue;   /* _registry 不是技能 */
  const top = path.join(SKILLS, e.name);
  const own = path.join(top, "SKILL.md");
  if (fs.existsSync(own)) {
    const s = readSkillAt(own, e.name);
    if (s) skills.push(s);
    continue;
  }
  for (const sub of fs.readdirSync(top, { withFileTypes: true })) {
    if (!sub.isDirectory()) continue;
    const s = readSkillAt(path.join(top, sub.name, "SKILL.md"), sub.name);
    if (s) skills.push(s);
  }
}
skills.sort((a, b) => a.name.localeCompare(b.name));

const byName = new Map(skills.map((s) => [s.name, s]));

/* 写用例时查词：把 38 条 description 打出来，别凭记忆猜哪个词在不在 */
if (process.argv.includes("--dump")) {
  for (const s of skills) console.log(`\n=== ${s.name} ===\n${s.desc}`);
  console.log(`\n[total] ${skills.length} 个技能`);
  process.exit(0);
}

if (!fs.existsSync(CASES)) {
  console.log(`[fail] 缺路由用例文件：${path.relative(root, CASES)}`);
  process.exit(1);
}

const data = JSON.parse(fs.readFileSync(CASES, "utf8"));
const cases = Array.isArray(data.cases) ? data.cases : [];
const hit = (desc, kw) => desc.toLowerCase().includes(String(kw).toLowerCase());

const covered = new Set();

for (const c of cases) {
  const id = c.id || "(缺 id)";
  const exp = byName.get(c.expect);
  if (!exp) {
    fail(id, `expect 指向不存在的技能 \`${c.expect}\``);
    continue;
  }
  covered.add(c.expect);

  if (!Array.isArray(c.keywords) || c.keywords.length === 0) {
    fail(id, "缺 keywords（至少要一个触发词）");
    continue;
  }

  /* A. 触发词必须真在 description 里 —— 没写，模型就看不见 */
  for (const kw of c.keywords) {
    if (!hit(exp.desc, kw)) {
      fail(id, `关键词「${kw}」不在 ${c.expect} 的 description 里 —— 用户这样说时模型永远选不中它`);
    }
  }

  /* B. 不允许第二个技能同时命中全部关键词 */
  const allow = new Set([c.expect, ...(c.allow || [])]);
  for (const s of skills) {
    if (allow.has(s.name)) continue;
    if (c.keywords.every((kw) => hit(s.desc, kw))) {
      fail(id, `路由歧义：\`${s.name}\` 的 description 也同时命中 ${JSON.stringify(c.keywords)}`);
    }
  }
}

/* C. 覆盖：每个技能至少被一条用例点名 */
for (const s of skills) {
  if (!covered.has(s.name)) {
    fail("coverage", `技能 \`${s.name}\` 没有任何用例点名（新增技能必须补一条，否则它可能永远不被触发）`);
  }
}

if (bad.length) {
  console.log(bad.join("\n"));
  console.log(`\n[fail] 路由用例不成立：${bad.length} 处（${cases.length} 条用例 / ${skills.length} 个技能）`);
  process.exit(1);
}
console.log(`[ok] 路由用例成立：${cases.length} 条覆盖 ${skills.length} 个技能，触发词在位、无路由歧义`);
