/**
 * @file    test-guard.cjs
 * @brief   guard-command 的回归判据（能变红的那种）
 * @details 为什么需要它：
 *   `guard-command.cjs` 是**硬拦截层**，它出错的两种方式都很贵：
 *     ① 漏判 —— 危险命令直接执行，不可恢复；
 *     ② 误伤 —— 正常命令被拦，使用者只能关掉整条规则，防线跟着失效。
 *   两种都只能靠用例钉住，靠"看起来对"不行。本文件就是那组用例。
 *
 *   2026-10-31 加它的直接原因：`/^(format|diskpart|mkfs)\b/i` 把 `| Format-Table`
 *   误判成"格式化磁盘"——`\b` 在 `format` 与 `-` 之间成立。误伤的是 PowerShell 所有
 *   Format-* cmdlet（管道里极常见），当时只靠人肉发现。这里把它固定成用例。
 *
 * 用法：node tools/hooks/test-guard.cjs      （由 tools\check-format.bat 调起）
 * 契约：全部通过退出 0；任何一条不符合预期退出 1，并打印是哪条。
 */
const path = require("path");
const { spawnSync } = require("child_process");

const GUARD = path.join(__dirname, "guard-command.cjs");

/** 走一遍真实的 hook 进程：stdin 喂事件，读 stdout 判定 */
function run(cmd) {
  const r = spawnSync(process.execPath, [GUARD], {
    input: JSON.stringify({ hook_event_name: "PreToolUse", tool_input: { command: cmd } }),
    encoding: "utf8",
  });
  const out = String(r.stdout || "").trim();
  if (!out) return { decision: "allow", reason: "" };
  try {
    const j = JSON.parse(out).hookSpecificOutput || {};
    return { decision: j.permissionDecision || "allow", reason: String(j.permissionDecisionReason || "") };
  } catch (e) {
    return { decision: "unparsable", reason: out };
  }
}

/**
 * deny 用例：命令 + 期望命中的规则 id。
 * 断言**具体 id**，不只断言"被拦了"——否则规则串味（disk-format 拦住了 rmdir）也能过。
 */
const DENY_CASES = [
  ["rmdir-refuse-dir", "rmdir /s /q build", "rmdir-recursive"],
  ["rd-recursive", "rd /s /q build", "rmdir-recursive"],
  ["del-recursive", "del /s *.obj", "del-recursive"],
  ["ps-remove-item-recurse", "Remove-Item -Recurse -Force build", "ps-remove-recurse"],
  ["git-clean-force", "git clean -fdx", "git-clean"],
  ["git-reset-hard", "git reset --hard HEAD~3", "git-reset-hard"],
  ["git-push-force-long", "git push --force origin main", "git-push-force"],
  ["git-push-force-plus", "git push origin +main", "git-push-force"],
  ["cmd-format-drive", "format c: /fs:ntfs", "disk-format"],
  ["cmd-format-drive-fq", "format.com d: /q", "disk-format"],
  ["ps-format-volume", "Format-Volume -DriveLetter D", "ps-format-volume"],
  ["ps-clear-disk", "Clear-Disk -Number 1 -RemoveData", "ps-format-volume"],
  ["diskpart", "diskpart", "diskpart-mkfs"],
  ["mkfs", "mkfs.ext4 /dev/sdb1", "diskpart-mkfs"],
  ["reg-delete", "reg delete HKCU\\Software\\Foo /f", "reg-delete"],
  /* 段尾也要能拦：`cd build && rmdir /s /q .` 这种，危险在第二段 */
  ["second-segment", "git status && rmdir /s /q build", "rmdir-recursive"],
  ["pipe-segment", "Get-ChildItem | Remove-Item -Recurse", "ps-remove-recurse"],
];

/**
 * allow 用例：必须放行。
 * 这批是**误伤回归**，每一条都对应一个真实会被管道/参数带出来的写法。
 */
const ALLOW_CASES = [
  ["ps-format-table", "Get-ChildItem | Format-Table -AutoSize"],
  ["ps-format-list", "Get-Process | Format-List Name,Id"],
  ["ps-format-hex", "Get-Content a.bin -AsByteStream | Format-Hex"],
  ["ps-format-wide", "Get-Command | Format-Wide -Column 3"],
  ["git-log-format", "git log --format=\"%an <%ae>\" -5"],
  ["git-status", "git status --porcelain"],
  ["git-push-normal", "git push origin main"],
  ["git-commit-message", "git commit -m \"fix rmdir /s bug\""],
  ["echo-dangerous-string", "echo \"rm -rf / --no-preserve-root\""],
  ["grep-dangerous-string", "Select-String -Path README.md -Pattern \"git reset --hard\""],
  ["node-gate", "node tools\\check-format.bat"],
  ["dir-list", "Get-ChildItem -Directory -Name"],
];

/** 放行开关：disable 里列出的 id 不应该再拦（用环境变量把 HOME 指到临时目录会破坏其他用例，这里只测配置解析） */
const disabled = (() => {
  try {
    const fs = require("fs");
    const os = require("os");
    const p = path.join(os.homedir(), ".codebuddy", "hooks.json");
    const cfg = JSON.parse(fs.readFileSync(p, "utf8"));
    return new Set(((cfg && cfg.guard && cfg.guard.disable) || []).filter(Boolean));
  } catch (e) {
    return new Set();
  }
})();

let failed = 0;
let skipped = 0;

for (const [name, cmd, wantId] of DENY_CASES) {
  if (disabled.has(wantId)) {
    console.log(`  [skip] ${name} —— 该规则已在 ~/.codebuddy/hooks.json 被禁用`);
    skipped += 1;
    continue;
  }
  const got = run(cmd);
  const reasonOk = got.reason.includes(`团队红线：${wantId}`);
  if (got.decision !== "deny" || !reasonOk) {
    failed += 1;
    console.error(
      `[fail] ${name}\n        命令: ${cmd}\n        期望: deny(${wantId})\n        实际: ${got.decision} ${got.reason.split("\n")[0] || ""}`,
    );
  }
}

for (const [name, cmd] of ALLOW_CASES) {
  const got = run(cmd);
  if (got.decision !== "allow") {
    failed += 1;
    console.error(
      `[fail] ${name}\n        命令: ${cmd}\n        期望: allow\n        实际: ${got.decision} ${got.reason.split("\n")[0] || ""}`,
    );
  }
}

const total = DENY_CASES.length + ALLOW_CASES.length - skipped;
if (failed) {
  console.error(`[fail] guard 用例 ${failed}/${total} 条不符合预期`);
  process.exit(1);
}
console.log(
  `[ok] guard 规则用例全过：${DENY_CASES.length - skipped} 条拦截 + ${ALLOW_CASES.length} 条放行（含 Format-* 误伤回归）`,
);
