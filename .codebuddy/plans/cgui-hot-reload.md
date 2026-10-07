# 实现计划：`.cgui` 外部变更热重载（画布自己跟上磁盘）

> 「向 py880 偷学」第三轮。前两轮见 `py880-absorption.md`（多选 + 对齐）、`refs-rename.md`（引用改名）。
> 本文件是这一轮的**唯一契约**。**与落盘代码不一致就报回来，以代码为准**。

---

## 0. 为什么是它（对应 py880 的哪一条）

py880 的头号卖点是 **「说出需求，AI 搭界面」**：AI 改完界面，人接着用鼠标拖。
本工程不走"内嵌大模型"那条路（要 API key、要联网，与"离线单 exe"的定位打架），
**发布包里已经带了一套 AI 技能**（`packaging/codebuddy/skills/{cgui-edit,cstackgui-dev}`），
所以本工程的"AI 改界面"路线是：**AI 编码助手改 `.cgui` 文本 → 画布跟上**。

**现在这一步是断的**：外部（AI、编辑器、`git checkout`）把 `.cgui` 改了，已经开着的画布**不闻不问**，
使用者得关掉再打开，或者被"改了没生效"骗一次（画布上是旧的、磁盘上是新的）。

这一轮把它接上：**磁盘变了，画布自己跟上；画布上有没保存的改动时先问，不冲掉。**

### 非目标（本轮明确不做）

- 不做"多窗口/多文档"（那是 F2 跨端 epic）。
- 不做"光标的协同/合并"（不做 CRDT、不做三方合并）——冲突只有两个选项：载入外部版本，或留着我的。
- 不做"文件被删/被改名"的处理（`.cgui` 被删掉就只是不再有提示，不弹错）。
- 不新增任何 `.cgui` 字段、不动 `runtime/`。

### 验收标准（每条都要能失败）

| # | 验收 | 怎么验（会红的方式） |
|---|---|---|
| A1 | 画布**没有未保存改动**时，外部改 `.cgui` → 画布**自动**刷新（不用点任何东西） | 真机：起画布指向一份临时 `.cgui`，在外部把窗口标题改掉，截图看画布标题栏是不是新值 |
| A2 | 画布**有未保存改动**时，外部改动**不覆盖**画布，只出一条通知条 | 真机：先改一下画布（dirty）、再改外部文件，截图应看到通知条 + 画布内容还是自己的 |
| A3 | **自己保存不触发**（保存后不该冒"已载入外部改动"） | 真机：Ctrl+S 之后等 2s，截图没有通知条、也不该有 toast |
| A4 | 外部写进去**读不懂**的内容 → 忽略（画布不崩、不说"已载入"） | 真机：往文件里写 `{`（坏 JSON），截图应仍在原状 |
| A5 | **切换文件后只盯新文件**：旧文件再改，画布不理 | 代码路径 + 探针（`watchStart` 先停旧的） |
| A6 | 关窗后**不再轮询**（无定时器泄漏） | 代码路径：`win.on("closed")` / `before-quit` 里 `watchStop()` |
| A7 | 自建探针能红 | 反例：把"比较内容"那步去掉 → 自己保存也会被当成外部改动（A3 的断言应失败） |

## 1. 设计决定（定了，理由在此）

- **用轮询 `mtimeMs:size` 而不是 `fs.watch`**：编辑器（VS Code 这类）保存时常常"写临时文件再改名"，
  Windows 上 `fs.watch` 盯着的那个句柄会跟着消失、之后**再也不报事件**（静默失效，最难查）。
  轮询没有这个坑；`.cgui` 就几 KB，700ms 一次 `stat` 的开销可以忽略。
- **"是不是我们自己写的"靠比内容，不靠比时间**：主进程记住最后一次读/写到的**文本**，
  轮询发现时间戳变了就**读出来比一下文本**，一样就什么都不做。
  只比时间戳会把"自己刚保存"也当成外部改动（A3）。
- **有未保存改动时不自动载入**：改为挂起 + 通知条（「载入外部版本」/「留着我的改动」）。
  自动覆盖等于替使用者丢掉他正在改的东西，不可逆。
- **载入外部版本时清空撤销栈**：那份历史属于另一条文档血脉，混着用会让 Ctrl+Z 回到"外部改动之前"，
  是最难解释的一种状态。清掉并在提示里说清楚（"撤销历史已重置"）。
- **通知条放在画布底部**（`.viewport` 与 `.viewport-hint` 之间，正常文档流）：
  顶部已经被对齐/分布条占了，用绝对定位会撞；底部这条只在需要时出现，不占常驻空间。
- **主进程顺带修一处不一致**：`cgui:exportC` 写完 `.cgui` 不更新 `currentFile`
  （画布侧 `filePath` 更新了、主进程不知道），热重载要盯新文件，顺手把这个补上。

## 2. 改动清单（每步自带验证）

| 步 | 文件 | 改什么 | 验证 |
|---|---|---|---|
| S1 | `designer/electron/main.mjs` | 轮询监视器（`watchStart` / `watchStop` / `pollWatch` / `noteDiskContent`）；`app:initialFile`、`cgui:open`、`cgui:save` 三处接上；`cgui:exportC` 写完 `.cgui` 后重新指向并同步 `currentFile`；窗口关闭停止轮询 | A1/A3/A5/A6；check-format 的「electron main process syntax」那一项 |
| S2 | `designer/electron/preload.cjs` | 加 `onFileChanged`（注册前先 `removeAllListeners`，同 `onRunOutput` / `onMenu` 的既有约定） | A1 |
| S3 | `designer/src/bridge.ts` | `Bridge` 加 `onFileChanged`；Electron 分支接上；**浏览器降级给 no-op**（否则浏览器模式崩） | `tsc --noEmit` |
| S4 | `designer/src/App.tsx` | 订阅；无改动→自动载入（清历史/选择 + 提示），有改动→挂起通知条；`doSave` 成功后清掉通知条 | A1/A2/A4 |
| S5 | `designer/src/styles.css` | `.notice-bar` 系列（单强调色、描边不叠阴影、按钮 ≥24px） | 截图 |
| S6 | `docs/ARCHITECTURE.md §8` | 「已落地」补一条：`.cgui` 外部变更热重载 | 逐字核对 |
| S7 | `tools/shot-canvas.ps1` | 加可选 `-File <path>`：把 `.cgui` 当命令行参数传给画布（**本轮的验证需要它，也方便以后**；注释保持 ASCII） | A1 用它跑 |

### 边界与异常

| 场景 | 预期行为 |
|---|---|
| 外部改动 + 画布 dirty | 挂起，通知条；**不覆盖** |
| 外部改动 + 画布干净 | 自动载入，清历史与选择，toast「已载入外部改动（撤销历史已重置）」 |
| 外部写进坏 JSON | 忽略 + toast，画布保持原状 |
| 同一秒内被改多次 | 轮询天然合并（只认最后一次内容） |
| `.cgui` 被删掉 / 改名 | `stat` 抛错就跳过，不弹错、不退出 |
| 导出了 C 骨架（`.cgui` 换目录） | 监视器重新指向新文件；旧文件再改不再提示 |
| 浏览器模式（无 Electron） | `onFileChanged` 是空实现，行为与今天一致 |
| 窗口关闭 / 应用退出 | `clearInterval`，不留定时器 |

## 3. 回滚方式

改动集中在画布侧（`designer/src/**`、`designer/electron/**`、`tools/shot-canvas.ps1`）+ 一行文档。
`.cgui` 契约与 `runtime/` **零改动** → 旧文档、旧 exe、旧 release 包不受影响。

## 4. 验证命令

```bat
cd designer && npx tsc --noEmit && npx vite build
tools\check-format.bat                    :: 含 electron 主进程语法那一项
powershell -NoProfile -ExecutionPolicy Bypass -File build\probe-hotreload.ps1   :: 真机：起画布 → 外部改文件 → 两次截图
```

## 5. 进度

- [x] P0 上下文：摸清 `currentFile` 的 4 个落点、preload/bridge 的事件套路、`cguiFromArgv` 支持命令行带文件
- [x] P1 计划：本文件
- [x] P2 实现 S1~S7
- [x] P3 验证：真机探针（A1/A3/A4 + 反例）+ `tsc --noEmit` + `vite build` + `tools\check-format.bat`（含 electron 主进程语法）= 全绿
- [x] P4 审查：`.codebuddy/code-reviews/2026-10-07-热重载.md`（无 critical / high；一条 🟡 当场修掉）
- [x] P5 提交
- [x] P7 收敛：A1~A7 逐条对账，见下

### 计划外顺带修的两处（同轮，已写进审查报告）

1. **样式/动作对话框抬头从词中间折行**（用户点名要修的遗留）：根因是抬头那行塞了长内联代码
   `<code>{"{ \"use\": \"名字\", ...覆盖 }"}</code>`。按本项目已有套路修 —— 长说明挪到正文图例
   （`ActionsDialog` 的快捷键说明就是这么放的），抬头只留"标题 + 条数"；另加 `.modal-head code
   { white-space: nowrap }` 兜底。
2. **画布读入不跳 UTF-8 BOM**：运行时 `runtime/src/core/cg_json.c:560` 明确"自动跳过 BOM，
   编辑器保存的 .cgui 常带"，画布这边 `parseDoc` 直接 `JSON.parse` —— 同一份文件 exe 跑得好、
   画布却说"不是合法 JSON"。热重载上线后外部编辑器是一等场景，这个两端口径不一致会更常撞上，
   所以顺手对齐。

### 验收对账（P7）

| # | 结果 | 证据 |
|---|---|---|
| A1 画布干净时外部改动自动刷新 | **Converged** | 真机：`before` 标题「深色终端（app.dark 示例）」→ `after` `HOT RELOAD OK`（全程没碰画布） |
| A2 画布脏时不覆盖、出通知条 | Converged | 代码路径（`if (dirty) { setExternal(...); return; }`）+ 目视 `canvas-notice4.png`（条上两个按钮完整可见） |
| A3 自己保存不触发 | **Converged** | 真机：Ctrl+S 后 `file:changed lines = 1`；**反例拆掉"比内容"闸门后 = 2** |
| A4 坏 JSON 被忽略、不崩 | **Converged** | 真机：写 `{ this is not json at all` → toast「内容读不懂，已忽略」，文档纹丝不动 |
| A5 换文件后只盯新文件 | Converged（代码路径） | `watchStart` 开头就 `watchStop()`；真机单实例未覆盖"同时开两个画布" |
| A6 关窗停止轮询 | Converged（代码路径） | `win.on("closed")` 里 `watchStop()` |
| A7 探针能红 | **Converged** | 见 A3 的反例（1 → 2） |

### 遗留

- 🟡 **A5/A6 没有真机验证**：要覆盖得"同时开两个画布 + 手动关窗并观察定时器"，代价与本轮价值不匹配；
  两条都是"一行调用"且由构造保证（`watchStop` 在 `watchStart` 开头、`closed` 回收调）。
- 🟢 观测：`pollWatch` 用固定 700ms 间隔且是异步的，理论上会重叠执行；单文件几 KB 不可能慢过 700ms，
  且 `watchStamp` 写入幂等，真重叠也只是多读一次。
- 🟢 观测：启动日志里 `app:info ... file=(none)` 会让人以为"命令行带的文件没打开" ——
  其实只是 `info()` 比 `initialFile()` 先 resolve（同轮 `file:changed` 照样出现）。
- 🟢 观测：`build\probe-hotreload.ps1` 按 SOP 留在 `build\`（gitignore），不进库；下次要用直接跑。
- **下一轮候选**：F2 多窗体/窗体引用（跨端 epic，要独立 SDD）→ F8 生成代码的分区保留
  （让 main.c 能安全重新生成，而不是"宁可不同步"）。
