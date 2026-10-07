# 实现计划：`_ui_events.c` 自动补桩（py880「生成代码保留业务代码」的等价物）

> 「向 py880 偷学」第五轮。前四轮：`py880-absorption.md`（多选+对齐）、`refs-rename.md`（引用改名）、
> `cgui-hot-reload.md`（外部变更热重载）、`canvas-image-preview.md`（画布预览真图）。
> 本文件是这一轮的**唯一契约**。

---

## 0. 先说为什么不是 F2 多窗体

上一轮报告把 F2（多窗体/窗体引用）列为第一候选。开工前再掂一次：它要同时动
`.cgui` 契约 + 运行时窗口模型 + 画布文档模型 + codegen，是**跨端 epic**，
按本工程 SOP 得先 PRD→SDD→切片（至少三轮的活）。硬塞进一轮只会做出个半成品，
所以这轮改做 **F8 生成代码的分区保留** —— 它是一条完整、可验证、真能治病的切片。

## 1. 要治的病（有具体现象，不是"最好有"）

画布导出会生成 6 个文件（`designer/src/codegen.ts:367-376`）：`<名字>_ui_events.h`、
`<名字>_ui_events.c`、`main.c`、`<名字>.cgui`、`<名字>.rc`、`build.bat`。

而导出路径（`designer/electron/main.mjs` 的 `cgui:exportC`）**把 `main.c` 与 `*_ui_events.c` 整份跳过**：

```js
if ((f.name === "main.c" || f.name.endsWith("_ui_events.c")) && existsSync(target)) {
  skipped.push(f.name); continue;
}
```

于是用户撞上的是这条链：

1. 在画布上新绑一个事件（比如新按钮的 click → `on_new_click`）；
2. 重新导出 → **`.h` 被重新生成**（它是 `.h`，不在跳过名单里），里面有新原型，
   并把它加进了 `CG_BIND` 宏；
3. `main.c` 的 `k_bindings[]` 直接展开那个宏 → 引用了 `on_new_click`；
4. 但 `_ui_events.c` 被跳过 → **没有 `on_new_click` 的实现** → **链接失败 `undefined reference`**。

用户唯一的出路是**手抄一遍函数签名**（抄错就是下一个坑，而运行时不会为此报错）。

**py880 的 F8 治的正是这一类**：生成的代码可以安全地重新生成，而人写的业务代码不丢。

### 顺带修的一处「注释说谎」

`codegen.ts:271-272` 生成的 `main.c` 第一行写：

> `@generated-by-cstackgui:main.c —— 画布产物。看到这行说明本文件可以被画布重新导出；`

而导出路径**从不覆盖 main.c**（`existsSync` 就跳过）。这个标记真实用途是
`main.mjs` 用它区分"画布产物 vs 手写工程"（`handwritten` 判定）。**注释按事实改**。

## 2. 方案：只补不改（insert-only），不重写整份文件

被打听过的另一条路是 py880 那种"整份重生成 + 区域标记保留"：
把 `_ui_events.c` 整份重新生成，只保留标记之间的用户代码。**否掉它**，理由：

- 用户会在那份文件里加**标记之外**的东西：`#include`、全局变量、**辅助函数**、
  甚至一整个 `#if 0` 块。整份重写的合并器认不出这些，**丢掉就是丢用户的代码** ——
  而这个功能存在的全部意义恰恰是"不弄坏用户的代码"。
- 能安全重写的前提是"结构严格可解析"，而 C 的块结构要写个小词法器才敢解析（字符串/注释里的
  花括号、`#if` 分支）。为一个"刷新文档注释"的收益去冒这个险，不划算
  （回调签名本来就固定不许改，Doxygen 刷新没有实际价值）。

**采取的方案**：老文件**一个字节都不改**，只做两件事 ——

1. 老文件里**缺哪个回调**，就在末尾补上那个桩（并标注"以下是画布自动补的"）；
2. 老文件里**已有**的回调，**原样留着**（哪怕它已经没有事件绑定了 —— **永不删用户的代码**）。

这一条方案同时给出两个承诺，而且都容易验证：
- **不会弄坏**（不含新回调时逐字节不变，连 mtime 都不动，不会触发无谓的重新编译）；
- **不会漏桩**（新绑的事件一定会有实现，链接不会断）。

## 3. 改动清单

| 步 | 文件 | 改什么 | 验证 |
|---|---|---|---|
| S1 | `designer/electron/merge-events.mjs`（新） | 纯函数 `mergeEvents(oldText, newText) -> { content, added[], mode }`，零 electron 依赖（这样 Node 探针能直接 import；`tools\check-electron.mjs` 是按目录 glob 的，自动进语法门禁） | A1~A5 |
| S2 | `designer/electron/main.mjs` | 导出路径：`_ui_events.c` 不再整份跳过，走 `mergeEvents`；结果里带上 `addedCallbacks` | A1/A2/A8 |
| S3 | `designer/src/codegen.ts` | `main.c` 首行标记的注释按事实重写（它从不被重新导出）；`_ui_events.c` 的抬头说明补一句"重新导出会补桩、但不会动你写的东西" | A9 |
| S4 | `designer/src/bridge.ts` | `ExportResult` 加 `addedCallbacks?: string[]` | `tsc` |
| S5 | `designer/src/App.tsx` | 导出结果提示里列出"已自动补好的空函数"；顺手改掉那句过时注释（"导出会覆盖 main.c / `<名字>_cgui.c`" —— 前者不覆盖、后者早已不存在） | A9 |
| S6 | `docs/ARCHITECTURE.md` | 把导出的覆盖规则写准：谁会被重写、谁只补桩、谁永不碰 | 逐字核对 |
| S7 | `build/probe-merge.mjs`（临时） | 纯函数探针，正反例各跑 | A1~A7 |
| S8 | `build/probe-merge-e2e.mjs`（临时） | 端到端：用 `build/modeljs` 的 `generateC` 造工程 → 编译 → 加新绑定 → 走合并 → 再查不变量 / 再编译 | A6/A8 |

### 边界与异常

| 场景 | 预期行为 |
|---|---|
| `_ui_events.c` 不存在（首次导出） | 直接写新内容（等于今天的行为） |
| 存在、且一个回调都不缺 | **逐字节不变**，并且**不写文件**（`skipped` 里报"已是最新"，mtime 不动） |
| 存在、缺 1 个回调 | 只在末尾追加那一个桩，其余一字不动 |
| 用户把回调改名（画布上也改了事件名） | 老函数**保留**（删了可能丢逻辑），新名字补桩；界面上把补的名字列出来 |
| 老文件是**手写**的 `_ui_events.c`（不是画布生成的） | 同一条路：只补缺失的桩。**手写工程整体仍然只同步 `.cgui`**（`handwritten` 那条判断不动） |
| 老文件末尾不是完整语句（被截断） | 追加前先确保行尾换行；追加内容自带注释分节，语法上仍是独立块 |
| 同名回调出现两次（用户自己复制过一份） | 认"出现过"即视为已有，不重复补 |

## 4. 验收标准（每条都要能失败）

| # | 验收 | 怎么验（会红的方式） |
|---|---|---|
| A1 | 缺回调 → 补桩，且 `added` 列出名字 | 探针：老文件只有 `on_a`，新内容有 `on_a`+`on_b` → 结果含 `on_b` 定义、`added=["on_b"]` |
| A2 | 一个不缺 → **逐字节不变** | 探针：`merge(old, new) === old`（字符串相等），且 `added` 为空 |
| A3 | 用户自加的 include / 辅助函数 / 注释**一字不少** | 探针：老文件里塞一段 `static int helper(void){return 1;}` + `#include`，断言结果里原样存在 |
| A4 | 幂等 | 探针：`merge(merge(old,new).content, new).content` 与第一次结果相等 |
| A5 | 空老文件 → 直接用新内容 | 探针 |
| A6 | **链接不变量**：`.h` 里 `CG_BIND` 列出的每个名字，在 `.c` 里都有定义 | 探针里对真实 `generateC` 的输出做一次（这条正是"undefined reference"的机器等价物） |
| A7 | 探针能红 | 反例：把"已有就跳过"的判断写反 → A2/A4 必须失败 |
| A8 | 真机：补桩后的工程**真的能编译** | `build-app.bat <临时工程>`；没补桩时应当失败（同一条命令，前后对照） |
| A9 | 全仓不再有"main.c 会被重新导出""画布上只显示文件名"这类与实现相反的注释 | grep 核对 |

## 5. 回滚方式

改动落在画布侧（`designer/electron/**`、`designer/src/**`）+ 文档。`.cgui` 契约、`runtime/` **零改动**。

## 6. 验证命令

```bat
cd designer && npx tsc --noEmit && npx vite build
tools\check-format.bat                       :: 含 electron 主进程语法（新增的 .mjs 自动纳入）
node build\probe-merge.mjs                   :: 纯函数 + 反例
node build\probe-merge-e2e.mjs               :: 造工程 + 链接不变量
build-app.bat apps\<临时工程>                 :: 真机编译（工具链在的话）
```

## 7. 进度

- [x] P0 上下文：确认病根（`.h` 重生成而 `.c` 被跳过 → 链接断）、确认 `apps/*` 已 gitignore、
      确认 `check-electron.mjs` 按目录 glob、确认 `build-app.bat` 能编任意目录
- [x] P1 计划：本文件
- [x] P2 实现 S1~S8（新增 `designer/electron/merge-events.mjs`；改主进程 / codegen / bridge / App / 两份文档）
- [x] P3 验证：纯函数探针 + 端到端（真机编译与 selftest）+ `tsc` + `vite build` + `check-format` = 全绿
- [x] P4 审查：`.codebuddy/code-reviews/2026-10-07-事件补桩.md`（无 critical / high；一条 🟡 当场修掉）
- [x] P5 提交
- [x] P7 收敛：A1~A9 逐条对账，见下

### 验收对账（P7）

| # | 结果 | 证据 |
|---|---|---|
| A1 缺回调 → 补桩并列出名字 | **Converged** | 纯函数探针 + e2e（`added=on_b_click`） |
| A2 一个不缺 → 逐字节不变 | **Converged** | 探针断言 `content === old`、`mode=same`；e2e 再导出一次仍是 `same` |
| A3 用户自加的 include / 辅助函数 / 注释一字不少 | **Converged** | 探针断言；e2e 里 `static int my_helper` 在合并后仍在 |
| A4 幂等 | **Converged** | 探针 + e2e |
| A5 空老文件 → 直接用新内容 | **Converged** | 探针（`mode=fresh`） |
| A6 链接不变量（`.h` 的每个 BIND 在 `.c` 里有定义） | **Converged** | 探针机器检查：补桩前缺 `on_b_click`、补桩后齐全 |
| A7 探针能红 | **Converged** | 两次反例：判断写反 → 8 条 FAIL；严格正则 → 2 条 FAIL |
| A8 真机：不补桩编译失败 / 补桩后编译通过 | **Converged** | 真实 `ld.exe: main.o: undefined reference to 'on_b_click'` → 补桩后编译过 → `--selftest OK` |
| A9 不再有与实现相反的注释 | **Converged** | grep `可以被画布重新导出` / `导出会覆盖 main.c` / `重新导出不会覆盖它` → 全仓 0 命中 |

### 本轮自己撞到的坑（两张网兜着）

第一版 `merge-events.mjs` **语法就是错的**：块注释里写了注释收尾那两个字符（想表达"生成器那份注释的
收尾紧贴着签名"），注释被提前闭合、剩下半句成了代码。探针第一跑就报
`SyntaxError: Unexpected identifier '$'`；而 `tools\check-electron.mjs`（它的文件头记着同款历史事故）
也把它兜住了 —— 本轮新增的 `.mjs` 自动进网，门禁输出从 `2 个 .mjs` 变 `3 个 .mjs`。

### 遗留

- 🟡 **`main.c` 仍然不重新生成**：它的标记注释已按事实改（那个标记的真实用途是区分"画布产物 vs 手写工程"），
  但"画布改进了样板，已有工程拿不到"这件事没解决。**有意不做**：真让它重新导出就会抹掉用户手改的
  `main.c`，而它是纯样板、价值低、风险高。
- 🟢 **改名后旧回调会留成死代码**：这是"永不删用户的代码"的直接代价（要判"这函数真没人用了吗"，
  在 C 里没有可靠答案）。
- 🟢 **`splitBlocks` 认不出生成器形状时静默不补桩**：调用点只有一个（主进程传的就是 `generateC` 的产物），
  而且"宁可不动文件"正是这个功能的第一承诺。
- 🟢 临时工程留在 `apps\probemerge\`（含 exe，`.gitignore` 的 `apps/*` 覆盖，不脏仓库），
  探针 `build\probe-merge-e2e.mjs` 可随时复跑。
- 🟢 观测：`codegen.ts` 的桶导入改成 `./model.js` 是为让编译产物能被 Node 直接跑（探针要真跑 `generateC`），
  源码里看着多余，注释里写了原因。
- **下一轮候选**：F2 多窗体/窗体引用（跨端 epic，须独立走 PRD→SDD→切片）。
