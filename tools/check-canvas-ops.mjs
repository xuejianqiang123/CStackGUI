/* check-canvas-ops.mjs -- 画布文档层的 headless 契约（store + ops，不需要 DOM/Electron）
 *
 * 为什么存在（2026-10-18 路线图 G1/G2）：画布此前 0 自动验证 —— 改坏撤销栈、
 * 改坏复制的 id 唯一性，门禁全绿、用户当场踩坑。文档操作层完全不依赖 DOM，
 * 是画布自动化验证的第一块资产。这里钉死的都是"协同的架构前置"承诺：
 *
 *   1. 命令可序列化往返：JSON.parse(JSON.stringify(cmd)) 后重放到同构 doc，
 *      结果与原命令逐字节同构 —— 命令日志（journal）/ 远程重放的前提；
 *   2. 命令自包含：applyCmd 不掷随机数、不读全局（构造与应用分离）；
 *   3. id 唯一性：makeId 跨调用不撞（团队分工 / 合并两份 .cgui 的前提）；
 *   4. 撤销栈语义与迁移前逐条对齐：edit 压栈、新编辑清 redo 分支、上限
 *      HISTORY_LIMIT、replaceDoc 清历史且 dirty 归零。
 *
 * 用法：node tools\check-canvas-ops.mjs（designer 源码不在时跳过，同 check-support 口径）
 * 退出码：0 = 契约成立；1 = 有违反（逐条打印）
 */
import { execSync } from "node:child_process";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, "..");

if (!fs.existsSync(path.join(root, "designer", "src", "model.ts"))) {
  console.log("[skip] canvas ops contract (no designer sources)");
  process.exit(0);
}

/* 1. 编译 model 桶（与 check-support / check-format 编 model.ts 同一套路；
 *    model/ 相对 import 必须带 .js 后缀，见 model.ts 头注释） */
const outDir = path.join(root, "build", "canvasjs");
try {
  execSync(
    "npx --prefix designer tsc designer/src/model.ts --outDir build/canvasjs " +
      "--target es2022 --module es2022 --moduleResolution node --skipLibCheck --strict",
    { cwd: root, stdio: "pipe", shell: true },
  );
} catch (e) {
  console.error("[fail] 编译 designer/src/model.ts 失败");
  console.error(String(e.stdout ?? "") + String(e.stderr ?? ""));
  process.exit(1);
}

const m = await import(`file://${path.join(outDir, "model.js").replace(/\\/g, "/")}`);
const {
  HISTORY_LIMIT,
  applyCmd,
  emptyDoc,
  makeDuplicateCmd,
  makeId,
  makeInsertCmd,
  makeRemoveCmd,
  newStore,
  serializeDoc,
  templateDoc,
} = m;

let checks = 0;
const ok = (what, cond) => {
  checks += 1;
  if (!cond) {
    console.error(`[fail] ${what}`);
    process.exitCode = 1;
  }
};

/* ------------------------------------------------------------ makeId */
{
  const seen = new Set();
  for (let i = 0; i < 1000; i++) seen.add(makeId("button"));
  ok("makeId 连打 1000 个不撞", seen.size === 1000);
  ok("makeId 保留可读前缀", [...seen].every((id) => id.startsWith("button_")));
}

/* ------------------------------------------------------------ 命令：自包含 + JSON 往返重放 */
{
  /* 命令在"构造时点"的空文档上造好 —— 构造可以掷随机数（新 id），
     应用必须确定：同一命令对象喂给两份同构 doc，结果必须逐字节同构。 */
  const seed = templateDoc("blank");
  const cmd0 = makeInsertCmd(seed, "group", 10, 10);
  const gid = cmd0.items[0].node.id;
  const child = makeInsertCmd(seed, "label", 20, 20);
  child.items[0].parentId = gid;
  const cmds = [cmd0, child];

  const dA = structuredClone(seed);
  const dB = structuredClone(seed);
  const dC = structuredClone(seed);
  for (const c of cmds) applyCmd(dA, c); /* 原对象 */
  for (const c of cmds) applyCmd(dB, JSON.parse(JSON.stringify(c))); /* JSON 往返后 */
  for (const c of cmds) applyCmd(dC, c); /* **原对象再放一遍到第三份 doc**：命令不得被消费污染 */
  ok("命令 JSON 往返重放：serialize 逐字节同构", serializeDoc(dA) === serializeDoc(dB));
  ok("命令对象可重放（应用不消费/不污染命令）", serializeDoc(dA) === serializeDoc(dC));
  ok("重放后容器与子控件都到位", dA.widgets.length === 1 && (dA.widgets[0].children ?? []).length === 1);

  /* remove：JSON 往返 + 幂等（同一命令删两次不崩、结果一致） */
  const d1 = structuredClone(dA);
  const rm = makeRemoveCmd(d1, [gid]);
  ok("makeRemoveCmd 顶层目标构造出 remove 命令", rm !== null && rm.ids.length === 1);
  applyCmd(d1, JSON.parse(JSON.stringify(rm)));
  const afterFirst = serializeDoc(d1);
  applyCmd(d1, JSON.parse(JSON.stringify(rm)));
  ok("remove 命令重放幂等", serializeDoc(d1) === afterFirst);
  ok("remove 真的删掉了目标", d1.widgets.length === 0);

  /* duplicate：容器连子树复制，子树 id 全部重排且不与 taken 撞 */
  const d3 = structuredClone(dA);
  const made = makeDuplicateCmd(d3, [gid, d3.widgets[0].children[0].id]);
  ok("makeDuplicateCmd 收敛为顶层（子控件不单独复制）", made !== null && made.cmd.items.length === 1);
  applyCmd(d3, made.cmd);
  const ids = new Set();
  let dupOk = true;
  const collect = (nodes) =>
    nodes.forEach((n) => {
      if (ids.has(n.id)) dupOk = false;
      ids.add(n.id);
      collect(n.children ?? []);
    });
  collect(d3.widgets);
  ok("复制后全树 id 仍唯一", dupOk);
  ok("duplicate 返回的新 id 与树内一致", made.newIds.every((id) => ids.has(id)));

  /* move / align */
  const d4 = templateDoc("blank");
  applyCmd(d4, makeInsertCmd(d4, "button", 100, 100));
  const bid = d4.widgets[0].id;
  applyCmd(d4, { type: "move", ids: [bid], dx: -10, dy: 5 });
  ok("move 命令生效", d4.widgets[0].rect.x === 90 && d4.widgets[0].rect.y === 105);
  applyCmd(d4, { type: "align", ids: [bid], mode: "left" });
  ok("align 命令生效（left 不动 x）", d4.widgets[0].rect.x === 90);
}

/* ------------------------------------------------------------ store：撤销栈语义 */
{
  const st = newStore(emptyDoc());
  const snap0 = st.snapshot();
  ok("初始快照 canUndo/canRedo 均为 false", !snap0.canUndo && !snap0.canRedo);
  ok("无变更时快照引用稳定（useSyncExternalStore 前提）", st.snapshot() === snap0);

  st.edit((d) => { d.app.title = "改了一次"; });
  ok("edit 后 dirty=true 且 canUndo", st.snapshot().dirty && st.snapshot().canUndo);
  st.edit((d) => { d.app.title = "改了两次"; });
  st.undo();
  ok("undo 回到上一步", st.snapshot().doc.app.title === "改了一次");
  st.redo();
  ok("redo 重做", st.snapshot().doc.app.title === "改了两次");
  st.undo();
  st.edit((d) => { d.app.title = "新分支"; });
  ok("新编辑清空 redo 分支", !st.snapshot().canRedo);

  for (let i = 0; i < HISTORY_LIMIT + 50; i++) st.edit((d) => { d.app.width = 300 + i; });
  ok(`撤销栈封顶 ${HISTORY_LIMIT} 步`, (() => { let n = 0; while (st.snapshot().canUndo) { st.undo(); n++; } return n === HISTORY_LIMIT; })());

  st.replaceDoc(emptyDoc());
  ok("replaceDoc 清历史且 dirty 归零", !st.snapshot().canUndo && !st.snapshot().canRedo && !st.snapshot().dirty);

  let notified = 0;
  const un = st.subscribe(() => { notified += 1; });
  st.edit((d) => { d.app.title = "订阅者可见"; });
  ok("subscribe 收到变更通知", notified === 1);
  un();
  st.edit((d) => { d.app.title = "退订后不再通知"; });
  ok("退订后不再通知", notified === 1);
  st.markSaved();
  ok("markSaved 清 dirty", !st.snapshot().dirty);
}

if (process.exitCode) {
  console.error("[fail] 画布文档层契约被破坏（store/ops）");
} else {
  console.log(`[ok] 画布文档层契约成立：${checks} 项（命令 JSON 往返重放 / 幂等 / id 唯一 / 撤销栈语义）`);
}
