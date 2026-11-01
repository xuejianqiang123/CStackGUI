/**
 * @file    verify-on-stop.cjs
 * @brief   Stop 收工验证：跑快速静态门禁，**只对回归（上次绿这次红）回注**
 * @details 为什么只报回归：
 *   仓库里常有别人留下的红灯（未提交的例外表债务之类）。拿既有红灯卡住 AI 是错的，
 *   它会把"不是这轮造成的"当成"这轮弄坏的"，然后越修越乱。
 *   ⇒ 首次运行只记基线不报警；此后只有 "上次 ok → 这次 fail" 才顶回去。
 *
 * 为什么是"有界的一轮"：
 *   Stop 会带 `stop_hook_active`，为真时立刻放行（否则可能无限循环）。
 *   同一次回归只提一次 —— 修不好也不会卡死。
 *
 * 为什么跳过空工作区：
 *   `git status --porcelain` 为空（或只有 tools/hooks 自己的状态文件）⇒ 这轮没改东西，不跑。
 */
const { execFileSync } = require("child_process");
const fs = require("fs");
const path = require("path");
const { readStdin, pass, feedback } = require("./_lib.cjs");

const ROOT = process.cwd();
const STATE = path.join(ROOT, "tools", "hooks", ".last-gate.json");

const GATES = [
  { name: "check-skills", bin: "node", args: ["tools/check-skills.mjs"] },
  { name: "check-routing", bin: "node", args: ["tools/check-routing.mjs"] },
  { name: "check-roster", bin: "python", args: ["tools/check_roster.py"] },
];

function readState() {
  try {
    return JSON.parse(fs.readFileSync(STATE, "utf8"));
  } catch (e) {
    return null;
  }
}

function writeState(s) {
  try {
    fs.writeFileSync(STATE, JSON.stringify(s, null, 2), "utf8");
  } catch (e) {
    /* 静默 */
  }
}

function changedFiles() {
  try {
    const out = execFileSync("git", ["status", "--porcelain"], {
      cwd: ROOT,
      timeout: 8000,
      windowsHide: true,
      encoding: "utf8",
    });
    return out
      .split(/\r?\n/)
      .filter(Boolean)
      .filter((l) => !l.includes("tools/hooks/.last-gate.json"));
  } catch (e) {
    return [];
  }
}

/**
 * 三态结果：true = 确证绿 ｜ false = 确证红 ｜ null = **判不出来**（超时 / 命令缺失 / python 没装）
 *
 * 为什么必须有第三种：旧实现把「跑不起来」也记成 false。于是某次 40s 超时会给该门禁写下
 * 一个假红灯，而回归判据是 `prev === true && now === false` —— 下次真回归时 prev 是 false，
 * **一次超时吃掉一次真回归**（2026-11-01 修）。判据不看 e.status 是区分不出来的：
 * 进程真跑完并返回非 0 时 status 是数字；超时是 null、命令不存在是 undefined。
 */
function runGate(g) {
  try {
    const out = execFileSync(g.bin, g.args, {
      cwd: ROOT,
      timeout: 40000,
      windowsHide: true,
      encoding: "utf8",
    });
    return { ok: true, tail: (out || "").trim().split(/\r?\n/).slice(-1)[0] || "" };
  } catch (e) {
    const tail = String((e.stdout || "") + (e.stderr || ""))
      .trim()
      .split(/\r?\n/)
      .filter(Boolean)
      .slice(-4)
      .join("\n");
    if (typeof e.status === "number") return { ok: false, tail }; /* 真跑完了，确证红 */
    return { ok: null, tail: `跑不起来（${e.code || e.signal || "unknown"}）：${tail}` };
  }
}

/** 判不出来时**不覆盖**上次基线：一次超时不该抹掉「这条本来是绿的」这个已知事实 */
function mergeGateState(prevGates, name, result) {
  const out = Object.assign({}, prevGates || {});
  if (result.ok !== null) out[name] = result.ok;
  return out;
}

/** 只有「上次确证绿 → 这次确证红」才是本次引入的回归；判不出来不算回归 */
function isRegression(prevGates, name, result) {
  return !!(prevGates && prevGates[name] === true && result.ok === false);
}

async function main() {
  const ev = await readStdin();
  if (ev.stop_hook_active) return pass(); /* 已经在纠正循环里，放行 */

  const changed = changedFiles();
  if (changed.length === 0) return pass(); /* 这轮没改东西 */

  const prev = readState();
  const prevGates = (prev && prev.gates) || {};
  const now = { at: new Date().toISOString(), scope: "meta", gates: Object.assign({}, prevGates) };
  const regressions = [];

  for (const g of GATES) {
    const r = runGate(g);
    now.gates = mergeGateState(now.gates, g.name, r);
    if (isRegression(prevGates, g.name, r)) regressions.push(`${g.name}：\n${r.tail}`);
  }

  writeState(now);

  if (regressions.length === 0) return pass();

  return feedback(
    `收工验证发现**本次新引入**的门禁红灯（上次是绿的，这次红了）：\n\n` +
      regressions.join("\n\n") +
      `\n\n请先修好再收工；若确认与本轮改动无关，说明情况即可（基线已更新，不会反复打断）。`
  );
}

module.exports = { runGate, mergeGateState, isRegression, GATES };

if (require.main === module) {
  main().catch(() => pass()); /* hook 自身出错必须静默放行 */
}
