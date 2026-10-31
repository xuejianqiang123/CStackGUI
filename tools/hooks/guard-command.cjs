/**
 * @file    guard-command.cjs
 * @brief   PreToolUse 硬拦截：破坏性命令（`safe-file-ops` 的强制执行体）
 * @details 为什么需要它：
 *   `safe-file-ops` 是**软约束**（写在 SKILL.md 里，靠 AI 自觉）。而 `rmdir /s`、`git reset --hard`
 *   这类命令一次手滑就不可恢复。软约束 + 硬拦截才是完整的两层。
 *
 *   判定原则（学自上游踩坑）：**只按命令段首匹配**，不做"命令里出现就拦"。
 *   后者会误拦 `grep -rn "git push --force" .` 和 `echo "rm -rf /"`，同时漏判 `git push origin +main`——
 *   误伤与漏判同源：都是匹配位置错了。
 *
 *   正常 `git commit` / `git push` **不拦**（那是"需当次授权"，不是"永不合法"）。
 *   命中即 deny，reason 里给两条合法出路。
 *
 *   放行开关：`~/.codebuddy/hooks.json` 的 `guard.disable` 里加规则 id。
 */
const fs = require("fs");
const os = require("os");
const path = require("path");
const { readStdin, pass, deny } = require("./_lib.cjs");

const RULES = [
  { id: "rmdir-recursive", re: /^(rmdir|rd)\b(?=.*\s\/s\b)/i, why: "递归删除整个目录" },
  { id: "del-recursive", re: /^del\b(?=.*\s\/s\b)/i, why: "递归删除文件" },
  { id: "ps-remove-recurse", re: /^remove-item\b(?=.*-recurse)/i, why: "递归删除（PowerShell）" },
  { id: "git-clean", re: /^git\s+clean\b(?=.*-[a-z]*f)/i, why: "删掉未跟踪文件，删了就找不回" },
  { id: "git-reset-hard", re: /^git\s+reset\s+--hard/i, why: "丢弃全部未提交改动" },
  { id: "git-push-force", re: /^git\s+push\b(?=.*(\s-f\b|\s--force|\s\+\S+))/i, why: "强推覆盖远端历史" },
  { id: "disk-format", re: /^(format|diskpart|mkfs)\b/i, why: "格式化磁盘" },
  { id: "reg-delete", re: /^reg\s+delete\b/i, why: "删除注册表项" },
];

/** 读用户级放行开关；没有该文件 = 用默认值（全部启用） */
function disabledIds() {
  try {
    const p = path.join(os.homedir(), ".codebuddy", "hooks.json");
    const cfg = JSON.parse(fs.readFileSync(p, "utf8"));
    return new Set((cfg && cfg.guard && cfg.guard.disable) || []);
  } catch (e) {
    return new Set();
  }
}

(async () => {
  try {
    const ev = await readStdin();
    const ti = ev.tool_input || {};
    const cmd = String(ti.command || ti.cmd || "");
    if (!cmd) return pass();

    /* 按 `&& || ; | 换行` 切段，只看每段开头 */
    const segs = cmd
      .split(/&&|\|\||[;|\n]/)
      .map((s) => s.trim())
      .filter(Boolean);

    const off = disabledIds();
    for (const seg of segs) {
      for (const r of RULES) {
        if (off.has(r.id)) continue;
        if (!r.re.test(seg)) continue;
        return deny(
          `[团队红线：${r.id}] 这条命令会${r.why}，已拦截。\n` +
            `命令片段：${seg.slice(0, 140)}\n` +
            `出路：① 确需执行请由人手动跑；② 认为该规则误伤，在 %USERPROFILE%\\.codebuddy\\hooks.json 的 "guard.disable" 里加 "${r.id}"。`
        );
      }
    }
    pass();
  } catch (e) {
    pass(); /* hook 自身出错必须静默放行 */
  }
})();
