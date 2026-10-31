# `tools/hooks/` —— 硬约束层

软约束写在 `SKILL.md` 里（靠 AI 自觉），硬约束写在这里（IDE 强制）。
两者缺一不可：只有软约束，一次手滑就不可恢复；只有硬约束，遇到没覆盖的情况照样乱来。

## 为什么放 `tools/hooks/` 而不是 `.codebuddy/hooks/`

本 IDE 把 `.codebuddy` 下紧随其后的 `hooks` 视为**受保护配置路径**，每次写入都要人工确认。
hook 是脚本不是配置，与 `check-*.mjs` 同级放在 `tools/` 更顺，也避开无谓审批。
**触发时机**（哪个事件调哪个脚本）仍写在 `.codebuddy/settings.json` 的 `hooks` 段。

## 三个 hook

| 文件 | 事件 | 作用 | 失败时 |
|---|---|---|---|
| `guard-command.cjs` | `PreToolUse` | 按**命令段首**拦破坏性命令（`rmdir /s`、`del /s`、`Remove-Item -Recurse`、`git clean -f`、`git reset --hard`、`git push --force`、`format`/`diskpart`） | 静默放行 |
| `verify-on-stop.cjs` | `Stop` | 收工跑三个技能门禁，**只对"上次绿这次红"回注**（既有红灯不背锅）；无改动跳过；`stop_hook_active` 为真立刻放行 | 静默放行 |
| `inject-context.cjs` | `SessionStart` | 注入环境简报（分支 / 未提交规模 / 技能编制 / 上次门禁），限长 500 字 | 静默放行 |
| `_lib.cjs` | — | 公共垫片：stdin 契约、`pass/deny/context/feedback` 四种输出 | — |

**所有 hook 的异常路径一律 `pass()`** —— 判据脚本崩了不能卡住主流程。宁可少拦一次，
也不能因为脚本自己出错把工作流锁死。

## 接线（`.codebuddy/settings.json` 追加）

> 该文件是受保护路径，**需要人工确认一次**才能写入。

```jsonc
"hooks": {
  "SessionStart": [
    { "hooks": [{ "type": "command", "timeout": 15, "command": "node tools/hooks/inject-context.cjs" }] }
  ],
  "PreToolUse": [
    { "matcher": "Bash|execute_command",
      "hooks": [{ "type": "command", "timeout": 10, "command": "node tools/hooks/guard-command.cjs" }] }
  ],
  "Stop": [
    { "hooks": [{ "type": "command", "timeout": 60, "command": "node tools/hooks/verify-on-stop.cjs" }] }
  ]
}
```

## 自测（不依赖 IDE，随时可跑）

```bat
:: 危险命令应被 deny（输出 permissionDecision: deny）
node -e "console.log(JSON.stringify({tool_input:{command:'rmdir /s /q build'}}))" | node tools\hooks\guard-command.cjs

:: 普通命令应静默放行（无输出）
node -e "console.log(JSON.stringify({tool_input:{command:'git status'}}))" | node tools\hooks\guard-command.cjs

:: 收工验证应写基线且不报警
node -e "console.log(JSON.stringify({stop_hook_active:false}))" | node tools\hooks\verify-on-stop.cjs
type tools\hooks\.last-gate.json

:: 会话简报
node -e "console.log('{}')" | node tools\hooks\inject-context.cjs
```

## 放行开关

命中 `guard` 想放行：在 `%USERPROFILE%\.codebuddy\hooks.json` 里加

```jsonc
{ "guard": { "disable": ["git-reset-hard", "git-push-force"] } }
```

不建这个文件 = 全部规则启用。

## 三条已知事实（省得重复踩坑）

1. **Windows 下 IDE 用 cmd.exe 执行 hook**，`$VAR` / `$()` / `;` 全部失效，
   且失败被吞成 `{allowed:true}` —— 表现为"什么都不发生"，只能靠自测发现。
2. **hook 里的命令必须是相对路径** `node tools/hooks/xxx.cjs`（cwd = 工作区根）。
3. `.last-gate.json` 是运行态基线，**不要提交**（建议进 `.gitignore`）。
