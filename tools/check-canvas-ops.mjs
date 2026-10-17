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
  makeImportCmd,
  newStore,
  parseTableCsv,
  serializeDoc,
  simApply,
  simulateResize,
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

/* ------------------------------------------------------------ grow 预演（layoutsim，镜像 runtime 算法） */
{
  const box = (x, y, w, h, grow) => ({ x, y, w, h, grow });

  /* 两带各一个 grow：delta 100 按权重各吃 50，后带整体平移 50（带间距不变） */
  {
    const bs = [box(0, 0, 100, 40, 1), box(0, 50, 100, 20, 0), box(0, 80, 100, 40, 1)];
    simApply(bs, 100, 120, 100, 220);
    ok("预演：竖向两 grow 带按权重各吃 50", bs[0].h === 90 && bs[2].h === 90);
    ok("预演：中间 grow=0 带被整体平移 50、自身不变", bs[1].y === 100 && bs[1].h === 20);
  }

  /* 同带两个 grow 成员各自把这一带填满（不是二选一）。
     契约口径：「同带」= 该轴上位置区间**重叠**（左右并排不重叠 = 两条带），
     所以这里用两块横向重叠的矩形来构造同带。 */
  {
    const bs = [box(0, 0, 100, 40, 1), box(60, 0, 80, 40, 1)];
    simApply(bs, 140, 40, 240, 40);
    ok("预演：同带两个 grow 成员各自吃满 delta", bs[0].w === 200 && bs[1].w === 180);
    ok("预演：同带成员位置互不位移", bs[0].x === 0 && bs[1].x === 60);
  }

  /* 基准重算：来回拉伸必须原地不动（不累积误差） */
  {
    const mk = () => [box(0, 0, 100, 40, 2), box(0, 50, 100, 30, 1), box(0, 90, 100, 30, 0)];
    const bs = mk();
    simApply(bs, 100, 120, 100, 320);
    simApply(mk(), 100, 120, 100, 320);
    const stretch = bs.map((b) => ({ ...b }));
    simApply(stretch, 100, 320, 100, 120);
    ok(
      "预演：基准重算，拉伸后缩回与原基准逐字段一致",
      JSON.stringify(stretch) === JSON.stringify(mk()),
    );
  }

  /* 缩小压到 0 不出负数；grow 夹取 0..99 */
  {
    const bs = [box(0, 0, 100, 40, 1)];
    simApply(bs, 100, 40, 100, 10);
    ok("预演：缩小压到 0 不出负数", bs[0].h === 10);
    const over = [box(0, 0, 100, 40, 999)];
    simApply(over, 100, 40, 200, 40);
    ok("预演：grow 超上限按 99 算（吃满 delta）", over[0].w === 200);
  }

  /* simulateResize：顶层 + 面板递归；无 grow 时原样 */
  {
    const doc = templateDoc("blank");
    doc.app.width = 400;
    doc.app.height = 300;
    const g = JSON.parse(JSON.stringify(makeInsertCmd(doc, "group", 0, 0).items[0].node));
    g.rect = { x: 0, y: 0, w: 400, h: 300 };
    g.grow = 1;
    const inner = JSON.parse(JSON.stringify(makeInsertCmd(doc, "button", 10, 10).items[0].node));
    inner.rect = { x: 10, y: 10, w: 100, h: 30 };
    inner.grow = 1;
    g.children = [inner];
    doc.widgets = [g];
    const sim = simulateResize(doc, 600, 300);
    ok("预演：顶层 grow 容器吃宽度", sim.get(g.id).w === 600);
    ok(
      "预演：面板递归（子控件按面板新尺寸再算一遍，这里横向 delta 也被子控件吃到）",
      sim.get(inner.id).w === 300 && sim.get(inner.id).x === 10,
    );
    const same = simulateResize(doc, 400, 300);
    ok("预演：now == base 时全部原样", same.get(g.id).w === 400 && same.get(inner.id).w === 100);
  }
}

/* ------------------------------------------------------------ 点表批量导入（tableimport） */
{
  /* 契约（roadmap §G4 钉死）：坏行逐行报错、好行照进；注释/空行静默跳过 */
  const csv = [
    "# 注释行不算",
    "",
    "启动,button",
    "温度,progress,60",
    "坏行" /* 1 列 */,
    "无类型,notatype" /* 未知类型 */,
    "坏值,edit,abc" /* 值不是数字 */,
    "多列,button,1,extra" /* 4 列 */,
    "标签,label",
  ].join("\n");
  const { rows, issues } = parseTableCsv(csv);
  ok(
    "导入：合法行照进、坏行不拖累好行",
    rows.length === 3 && rows[0].type === "button" && rows[1].type === "progress" && rows[2].type === "label",
  );
  ok(
    "导入：坏行逐行报错且行号正确（5/6/7/8）",
    issues.length === 4 && issues[0].line === 5 && issues[1].line === 6 && issues[2].line === 7 && issues[3].line === 8,
  );
  ok("导入：值只进合法行", rows[1].value === 60 && rows[0].value === undefined);

  /* 批量命令：网格摆位 + 唯一 id + JSON 往返重放同构（journal 纪律） */
  const doc0 = templateDoc("blank");
  const made = makeImportCmd(doc0, rows);
  ok("导入命令构造成功（3 个控件）", made !== null && made.newIds.length === 3);
  const dA = structuredClone(doc0);
  const dB = structuredClone(doc0);
  applyCmd(dA, made.cmd);
  applyCmd(dB, JSON.parse(JSON.stringify(made.cmd)));
  ok("导入命令 JSON 往返重放：serialize 逐字节同构", serializeDoc(dA) === serializeDoc(dB));
  ok(
    "导入网格：≤8 个排一行、列位递增（探针实测 3 个全在 y=24）",
    dA.widgets[0].rect.y === dA.widgets[1].rect.y &&
      dA.widgets[1].rect.y === dA.widgets[2].rect.y &&
      dA.widgets[0].rect.x < dA.widgets[1].rect.x &&
      dA.widgets[1].rect.x < dA.widgets[2].rect.x,
  );
  /* 9 行 → 第 9 个换到第二行（8 列网格的跨行边界） */
  const many = Array.from({ length: 9 }, (_, i) => ({ text: `第${i + 1}个`, type: "label" }));
  const madeMany = makeImportCmd(doc0, many);
  const dM = structuredClone(doc0);
  applyCmd(dM, madeMany.cmd);
  ok(
    "导入网格：第 9 个换行（8 列边界）",
    dM.widgets[8].rect.y > dM.widgets[7].rect.y && dM.widgets[7].rect.y === dM.widgets[0].rect.y,
  );
  const impIds = new Set();
  let impUniq = true;
  for (const w of dA.widgets) {
    if (impIds.has(w.id)) impUniq = false;
    impIds.add(w.id);
  }
  ok("导入后 id 全局唯一", impUniq && made.newIds.every((id) => impIds.has(id)));
  ok("导入：ranged 控件的值随 serialize 落盘", serializeDoc(dA).includes('"value": 60'));
  ok("导入：无合法行构造返回 null", makeImportCmd(doc0, []) === null);
}

if (process.exitCode) {
  console.error("[fail] 画布文档层契约被破坏（store/ops/layoutsim/tableimport）");
} else {
  console.log(
    `[ok] 画布文档层契约成立：${checks} 项（命令 JSON 往返重放 / 幂等 / id 唯一 / 撤销栈语义 / grow 预演镜像 / 点表导入）`,
  );
}
