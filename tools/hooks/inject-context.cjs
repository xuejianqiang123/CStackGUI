/**
 * @file    inject-context.cjs
 * @brief   SessionStart 注入环境简报：只放"每次都要重查、且文档不会记"的动态事实
 * @details 为什么只在 SessionStart 注入：
 *   每轮都注入等于把同一段话反复塞进上下文（纯 token 消耗 + 注意力污染）。
 *   这份简报的价值在"开新会话时不用重新问一遍现状"，不在"每轮提醒"。
 *
 *   只放三类：当前分支 / 未提交改动规模 / 团队与技能规模 + 上次门禁结果。
 *   限长 500 字 —— 超了就会挤掉真正该保留的内容。
 */
const { execFileSync } = require("child_process");
const path = require("path");
const { readStdin, pass, context } = require("./_lib.cjs");

const ROOT = process.cwd();

function sh(bin, args, ms = 6000) {
  try {
    return execFileSync(bin, args, {
      cwd: ROOT,
      timeout: ms,
      windowsHide: true,
      encoding: "utf8",
    }).trim();
  } catch (e) {
    return "";
  }
}

function readLastGate() {
  try {
    const fs = require("fs");
    const p = path.join(ROOT, "tools", "hooks", ".last-gate.json");
    const s = JSON.parse(fs.readFileSync(p, "utf8"));
    const bad = Object.entries(s.gates || {})
      .filter(([, ok]) => !ok)
      .map(([k]) => k);
    return bad.length ? `红 ${bad.join(" / ")}` : "全绿";
  } catch (e) {
    return "";
  }
}

(async () => {
  try {
    await readStdin();

    const branch = sh("git", ["rev-parse", "--abbrev-ref", "HEAD"]) || "?";
    const dirty = sh("git", ["status", "--porcelain"])
      .split(/\r?\n/)
      .filter(Boolean).length;
    const head = sh("git", ["log", "-1", "--format=%h %s"]).slice(0, 60);
    const roster = sh("python", ["tools/check_roster.py"], 15000);
    const teamCount = (roster.match(/：(\d+) 个团/) || [])[1] || "?";
    const gate = readLastGate();

    const lines = [
      "【环境简报】CStackGUI（Win32/GDI+ 桌面工具包 + Electron 画布）",
      `分支 ${branch}${head ? " · 最近 " + head : ""} ｜ 未提交改动 ${dirty} 个文件`,
      `技能编制 ${teamCount} 个团${gate ? ` ｜ 上次收工门禁：${gate}` : ""}`,
      "开工顺序：`expert-teams` 选团 → `sop` 定流程；改技能后跑 check-skills / check-routing / check-roster。",
    ];

    let text = lines.join("\n");
    if (text.length > 500) text = text.slice(0, 500) + "…";
    return context(text);
  } catch (e) {
    pass();
  }
})();
