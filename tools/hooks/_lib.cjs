/**
 * @file    _lib.cjs
 * @brief   hook 公共垫片：stdin 契约、静默失败、四种输出形态
 * @details 三条铁律（学自上游 .codebuddy/hooks 的实测结论）：
 *   1) **hook 自身出任何问题都必须静默放行** —— 判据脚本崩了不能卡住主流程；
 *   2) 命令一律写成 `node tools/hooks/<脚本>.cjs`，不出现绝对路径（换机器就失效）；
 *   3) 不要用 bash —— 本 IDE 在 Windows 下用 cmd.exe 执行 hook，`$VAR` / `$()` / `;` 全部失效，
 *      且失败会被吞成 `{allowed:true}`，表现为"什么都不发生"。
 *
 * 放在 `tools/hooks/` 而不是 `.codebuddy/hooks/`：
 *   本 IDE 把 `.codebuddy` 下紧随其后的 `hooks` 视为**受保护配置路径**，每次写入都弹确认。
 *   hook 是脚本不是配置，放 `tools/` 与 check-*.mjs 同级更顺手，也避开无谓的审批。
 *   触发时机（何时调哪个脚本）仍写在 `.codebuddy/settings.json` 的 hooks 段。
 *
 * stdin（IDE 给）：{ hook_event_name, session_id, cwd, tool_name?, tool_input?, ... }
 * stdout（我们给）：
 *   PreToolUse  → { hookSpecificOutput: { hookEventName, permissionDecision: "deny", permissionDecisionReason } }
 *   SessionStart→ { hookSpecificOutput: { hookEventName, additionalContext } }
 *   Stop        → { continue: false, stopReason }   ← 作为 feedback 回给 agent
 */
function readStdin() {
  return new Promise((resolve) => {
    let raw = "";
    let done = false;
    const finish = (v) => {
      if (done) return;
      done = true;
      resolve(v);
    };
    try {
      if (process.stdin.isTTY) return finish({});
      process.stdin.setEncoding("utf8");
      process.stdin.on("data", (c) => (raw += c));
      process.stdin.on("end", () => {
        try {
          finish(JSON.parse(raw || "{}"));
        } catch (e) {
          finish({});
        }
      });
      process.stdin.on("error", () => finish({}));
      setTimeout(() => finish({}), 3000);
    } catch (e) {
      finish({});
    }
  });
}

/** 放行且不输出任何东西 */
function pass() {
  process.exit(0);
}

/** 硬拦截一条命令（PreToolUse） */
function deny(reason) {
  process.stdout.write(
    JSON.stringify({
      hookSpecificOutput: {
        hookEventName: "PreToolUse",
        permissionDecision: "deny",
        permissionDecisionReason: reason,
      },
    })
  );
  process.exit(0);
}

/** 会话开始时注入一段环境简报 */
function context(text) {
  process.stdout.write(
    JSON.stringify({
      hookSpecificOutput: { hookEventName: "SessionStart", additionalContext: text },
    })
  );
  process.exit(0);
}

/** 收工时把问题顶回给 agent（只用于 Stop） */
function feedback(text) {
  process.stdout.write(JSON.stringify({ continue: false, stopReason: text }));
  process.exit(0);
}

module.exports = { readStdin, pass, deny, context, feedback };
