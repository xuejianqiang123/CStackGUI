/* format-roundtrip.mjs -- 格式契约回归测试
 *
 * 这条缝最容易坏：画布用 TS 写 .cgui，运行时用 C 读 .cgui，
 * 两边对字段的理解一旦漂移，画布会「能存但跑不起来」。
 *
 * 测法：
 *   1. 用画布的模型代码读一个 .cgui（parseDoc）
 *   2. 改点东西（挪位置、改文字、加控件、绑事件）后重新写出来（serializeDoc）
 *   3. 把写出来的文件交给 C 运行时自检（gallery.exe --selftest）
 *
 * 用法：node tools/format-roundtrip.mjs <exe> <输入.cgui> <输出.cgui>
 * 需要先编译画布模型：npx tsc src/model.ts --outDir ../build/modeljs ...
 */
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.join(here, "..");

const exe = process.argv[2] ? path.resolve(process.argv[2]) : path.join(root, "examples", "gallery", "gallery.exe");
const input = process.argv[3] ? path.resolve(process.argv[3]) : path.join(root, "examples", "gallery", "gallery.cgui");
const output = process.argv[4] ? path.resolve(process.argv[4]) : path.join(root, "build", "roundtrip_gallery.cgui");

const modelPath = path.join(root, "build", "modeljs", "model.js");
if (!fs.existsSync(modelPath)) {
  console.error(`[fail] 找不到 ${modelPath}，先用 tsc 把 designer/src/model.ts 编译到 build/modeljs/`);
  process.exit(2);
}

const { parseDoc, serializeDoc, findNode, makeNode, walk } = await import(
  `file://${modelPath.replace(/\\/g, "/")}`
);

/* 1. 读 */
const original = fs.readFileSync(input, "utf8");
const parsed = parseDoc(original);
if (!parsed.doc) {
  console.error(`[fail] 画布模型读不了 ${input}: ${parsed.error}`);
  process.exit(1);
}
const doc = parsed.doc;

/* 2. 改：模拟用户在画布上做的事 */

/** 文档里第一个已绑定的回调名。探针控件要复用它 —— 随便编一个名字的话，
 *  C 侧自检会把「名字没登记」当成失败，那是测试自己造出来的假故障。 */
let knownCallback = "";
walk(doc.widgets, (n) => {
  if (knownCallback) return;
  for (const name of Object.values(n.events ?? {})) {
    if (name) {
      knownCallback = String(name);
      return;
    }
  }
});

let touched = 0;
walk(doc.widgets, (n) => {
  if (n.type === "button" && !n.events?.click && knownCallback) {
    n.events = { ...(n.events ?? {}), click: knownCallback };
    touched += 1;
  }
});

const probeId = "chk_roundtrip";
if (!findNode(doc, probeId)) {
  const node = makeNode("checkbox", 24, 220);
  node.id = probeId;
  node.text = "画布新增的复选框";
  node.checked = true;
  if (knownCallback) node.events = { change: knownCallback };
  /* 顺手挂一个控件绑定：它是"画布能存、运行时读不懂"里代价最高的一类 ——
   * 丢了不报错、不崩，只是两个控件悄悄不同步，没人会发现。下面第 4 步核对它。 */
  let srcId = "";
  walk(doc.widgets, (n) => {
    if (srcId) return;
    /* 绑定的源挑"值语义"的控件：滑块 / 进度条 / 数值输入框（edit + 调节器） */
    if (n.type === "slider" || n.type === "progress" || (n.type === "edit" && n.spin)) srcId = n.id;
  });
  if (srcId) node.bind = { src: srcId };
  doc.widgets.push(node);
  touched += 1;
}

const title = findNode(doc, "lab_title");
if (title) {
  title.text = "由画布写回";
  title.rect.x += 6;
  touched += 1;
}

/* 有树/表格/标签页/状态栏的话，连结构化内容一起走一遍：
 * 这些字段最容易两边理解不一致，必须让 C 侧真的读一遍 */
walk(doc.widgets, (n) => {
  if (n.type === "tabs") {
    n.items = [...(n.items ?? []), "画布加的第三页"];
    n.children = n.children ?? [];
    const child = n.children[0];
    if (child) {
      child.page = 2;
      n.children.push({ ...structuredClone(child), id: `${child.id}_p2`, page: 2 });
    }
    touched += 1;
  }
  if (n.type === "tree") {
    n.nodes = [...(n.nodes ?? []), { text: "画布加的根节点", expanded: true, children: [{ text: "子" }] }];
    touched += 1;
  }
  if (n.type === "table") {
    n.columns = [...(n.columns ?? []), { title: "画布加的列", w: 90 }];
    n.rows = [...(n.rows ?? []), ["x", "y", "z"]];
    touched += 1;
  }
  if (n.type === "statusbar") {
    n.items = [...(n.items ?? []), "画布加的分段"];
    touched += 1;
  }
  if (n.type === "menu" || n.type === "menubutton") {
    /* 菜单是「嵌套 + id + 分隔线 + 勾选」的组合，最值得让 C 侧真读一遍 */
    n.nodes = [
      ...(n.nodes ?? []),
      { text: "-" },
      { text: "画布加的菜单", id: "m_roundtrip" },
      { text: "带子项", id: "m_roundtrip_sub", checked: true, children: [{ text: "子项", id: "m_sub" }] },
    ];
    touched += 1;
  }
  if (n.type === "toolbar") {
    n.items = [...(n.items ?? []), "画布加的按钮"];
    touched += 1;
  }
  if (n.type === "dialog") {
    /* dialog 的 children 是「第二个窗口」的控件树 —— 往返丢一个子控件，
     * 用户界面就缺一块，而且 C 侧自检**发现不了**（少的那个不是错误，只是没了），
     * 与 bind/action/styles 同属"丢了不报错"的那类，必须在这里塞探针、第 3 步逐字核对。
     * 探针按钮的事件复用已登记的回调（knownCallback），别造"未登记"的假故障 ——
     * 那会让第 4 步的自检以 5 退出，是测试自己造的假故障。 */
    n.children = n.children ?? [];
    if (n.center !== false) {
      n.center = false;   /* 顺带走一遍「不居中」字段（T2-full 加的）：往返也不能丢，第 3 步核对 */
      touched += 1;
    }
    if (!n.children.some((c) => c.id === "chk_dlg_child")) {
      const child = makeNode("button", 16, 16);
      child.id = "chk_dlg_child";
      child.text = "对话框里加的按钮";
      if (knownCallback) child.events = { click: knownCallback };
      n.children.push(child);
      touched += 1;
    }
  }
  if (n.type === "image" && n.src) {
    n.rect.w += 10;
    touched += 1;
  }
});

/* 动作（actions）：和 bind 属于同一类"字段丢了不报错、只是功能没了"的东西 ——
 * 丢了 bind 是两个控件不再同步，丢了 action 是快捷键与菜单项一起失效。
 * 探针要挂在**支持 click 的控件**上，并且把它自己那份 click 摘掉：
 * 同时绑 click 与 action 会被运行时判为错配置（自检非 0 退出），那是测试自己造的假故障。 */
const probeAction = "cgui_roundtrip_action";
let   actionProbeHostId = "";
if (knownCallback) {
  doc.actions = { ...(doc.actions ?? {}) };
  if (!doc.actions[probeAction]) {
    doc.actions[probeAction] = {
      text: "往返探针(&R)",
      accel: "Ctrl+Shift+R",
      handler: knownCallback,
    };
    touched += 1;
  }
  walk(doc.widgets, (n) => {
    if (actionProbeHostId) return;
    if (n.type === "button" && !n.action && n.id) actionProbeHostId = n.id;
  });
  const host = actionProbeHostId ? findNode(doc, actionProbeHostId) : null;
  if (host) {
    host.action = probeAction;
    if (host.events?.click) {
      const ev = { ...host.events };
      delete ev.click;
      host.events = ev;
    }
    touched += 1;
  }
} else {
  console.log("[note] 输入文件里没有任何已绑定的回调名，跳过 action 往返探针");
}

/* 命名样式（styles{}）与它的三种引用形态 —— 和 bind / action 属于同一类"丢了不报错"的
 * 东西：丢了 styles{} 或丢了引用，运行时不报错、不崩，只是**外观悄悄变了**；
 * 更麻烦的是 **C 侧自检发现不了**（一份没有命名样式的文档照样能通过），
 * 所以必须在第 3 步逐字核对。（2026-09-27 补：此前这条缝谁都没盯着。）
 * 三种形态各挂一个：styles{} 本身、纯字符串引用、{ use, 覆盖字段 }。 */
const probeStyle = "cgui_roundtrip_style";
doc.styles = { ...(doc.styles ?? {}) };
if (!doc.styles[probeStyle]) {
  doc.styles[probeStyle] = { font: "Consolas", size: 13, fg: "#010203" };
  touched += 1;
}
let styleHostRef = "";          /* 纯引用 "名字" 的控件 */
let styleHostUse = "";          /* { "use": "名字", ...覆盖 } 的控件 */
walk(doc.widgets, (n) => {
  if (!n.id || n.style) return;                 /* 已有样式的控件不动，免得改变原文档观感 */
  if (!styleHostRef) {
    n.style = probeStyle;
    styleHostRef = n.id;
  } else if (!styleHostUse) {
    n.style = { use: probeStyle, size: 11 };
    styleHostUse = n.id;
  }
});
if (!styleHostRef) console.log("[note] 没有空闲控件可挂样式纯引用，跳过该探针");
if (!styleHostUse) console.log("[note] 没有空闲控件可挂 {use,...}，跳过该探针");
if (styleHostRef || styleHostUse) touched += 1;

/* app.dark：应用级开关，同样属于"丢了不报错、只是界面变了"的那类 */
if (doc.app) {
  doc.app.dark = "auto";
  touched += 1;
}

/* roles{} 与 @角色引用（深色皮肤 G1/T3）：表本身 + 控件颜色字段上的引用都要双向无损。
 * 角色名挂一份、给一个空闲控件写 fg="@轮到它的角色" —— 丢了不报错、只是深浅联动悄悄失效。 */
const probeRole = "cgui_roundtrip_role";
let roleHostId = "";
{
  doc.roles = { ...(doc.roles ?? {}), [probeRole]: { light: "#6750A4", dark: "#D0BCFF" } };
  touched += 1;
  walk(doc.widgets, (n) => {
    if (!roleHostId && n.id && !n.style && (n.type === "label" || n.type === "button")) roleHostId = n.id;
  });
  const host = roleHostId ? findNode(doc, roleHostId) : null;
  if (host) {
    host.style = { fg: `@${probeRole}` };
    touched += 1;
  } else {
    console.log("[note] 没有空闲控件可挂 @角色引用，跳过该探针");
  }
}

const written = serializeDoc(doc);
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, written, "utf8");
console.log(`[ok] 画布改动 ${touched} 处，写出 ${output}（${written.length} 字节）`);

/* 3. 恒等回写核对：写出去的文件再读回来，绑定的源不能变、不能丢。
 * 只盯 bind 一个字段 —— 它的失效方式是"静默"的（两个控件不再同步），
 * 光靠 C 侧自检发现不了（没有 bind 的文档照样能通过）。 */
{
  const expect = new Map();
  walk(doc.widgets, (n) => {
    if (n.bind?.src) expect.set(n.id, n.bind);
  });
  const back = parseDoc(written);
  let lost = 0;
  for (const [id, b] of expect) {
    const got = back.doc ? findNode(back.doc, id) : null;
    if (!got || got.bind?.src !== b.src || (got.bind.two === true) !== (b.two === true)) {
      console.error(`[fail] 恒等回写丢了 bind：${id} -> ${b.src}${b.two ? " (two)" : ""}`);
      lost += 1;
    }
  }
  /* 动作（actions）也逐字核对：文字 / 快捷键 / handler / 引用点，一个都不能丢。
   * 丢了 action 的表现是"快捷键与菜单项一起静默失效"，同样是自检发现不了的那类。 */
  {
    const wrote = doc.actions?.[probeAction];
    if (wrote) {
      const got = back.doc?.actions?.[probeAction];
      if (!got) {
        console.error(`[fail] 恒等回写丢了 action：${probeAction}`);
        lost += 1;
      } else {
        for (const k of ["text", "accel", "handler"]) {
          if ((got[k] ?? "") !== (wrote[k] ?? "")) {
            console.error(`[fail] action ${probeAction}.${k} 变了：${wrote[k]} -> ${got[k]}`);
            lost += 1;
          }
        }
      }
      const hostBack = actionProbeHostId && back.doc ? findNode(back.doc, actionProbeHostId) : null;
      if (!hostBack || hostBack.action !== probeAction) {
        console.error(`[fail] 恒等回写丢了控件的 action 引用：${actionProbeHostId} -> ${probeAction}`);
        lost += 1;
      }
      console.log(`[ok] 恒等回写：action "${probeAction}" 与引用点无损`);
    }
  }

  /* 命名样式（styles{}）与两种引用形态，逐字核对 —— 与 bind/action 同理：丢了不报错，
   * 只是外观悄悄变了，而 C 侧自检对此**完全无感**（它只报错不报丢）。 */
  {
    const want = doc.styles[probeStyle] ?? {};
    const got  = back.doc?.styles?.[probeStyle];
    if (!got) {
      console.error(`[fail] 恒等回写丢了命名样式：${probeStyle}`);
      lost += 1;
    } else {
      for (const k of ["font", "size", "fg", "bg", "align", "bold", "radius"]) {
        if (JSON.stringify(got[k] ?? null) !== JSON.stringify(want[k] ?? null)) {
          console.error(`[fail] 命名样式 ${probeStyle}.${k} 变了：${want[k]} -> ${got[k]}`);
          lost += 1;
        }
      }
    }
    if (styleHostRef) {
      const n = back.doc ? findNode(back.doc, styleHostRef) : null;
      if (!n || n.style !== probeStyle) {
        console.error(`[fail] 恒等回写丢了样式纯引用：${styleHostRef} -> "${probeStyle}"`);
        lost += 1;
      }
    }
    if (styleHostUse) {
      const s = back.doc ? findNode(back.doc, styleHostUse)?.style : null;
      if (!s || typeof s !== "object" || s.use !== probeStyle || Number(s.size) !== 11) {
        console.error(`[fail] 恒等回写丢了 {use,...} 形态：${styleHostUse} -> ${JSON.stringify(s)}`);
        lost += 1;
      }
    }
    if (doc.app?.dark !== undefined && back.doc?.app?.dark !== doc.app.dark) {
      console.error(`[fail] 恒等回写丢了 app.dark：${doc.app.dark} -> ${back.doc?.app?.dark}`);
      lost += 1;
    }
    console.log(`[ok] 恒等回写：命名样式 "${probeStyle}" 与两种引用形态无损`);
  }

  /* roles 与 @角色引用逐字核对（T3.5）：表本身 + 挂在控件上的引用，一个都不能丢。 */
  {
    const want = doc.roles?.[probeRole];
    const got = back.doc?.roles?.[probeRole];
    if (!want) {
      console.error("[fail] 探针没把 roles 写进文档（自测脚本问题）");
      lost += 1;
    } else if (!got || got.light !== want.light || got.dark !== want.dark) {
      console.error(`[fail] 恒等回写丢了角色：${probeRole} -> ${JSON.stringify(got ?? null)}`);
      lost += 1;
    }
    if (roleHostId) {
      const gotHost = back.doc ? findNode(back.doc, roleHostId) : null;
      if (!gotHost || gotHost.style?.fg !== `@${probeRole}`) {
        console.error(`[fail] 恒等回写丢了 @角色引用：${roleHostId} -> ${JSON.stringify(gotHost?.style ?? null)}`);
        lost += 1;
      }
    }
    if (want && got && got.light === want.light && got.dark === want.dark) {
      console.log(`[ok] 恒等回写：roles "${probeRole}" 与 @引用无损`);
    }
  }

  /* 对话框子树逐字核对（T1.3）：dialog 是唯一「children = 另一个窗口」的容器。
   * 上面塞进去的探针按钮连同原有每个子控件都要原样回来 ——
   * 数量 / id / 类型 / 文字 / 事件，一个都不能丢。 */
  {
    let dlg = null;
    walk(doc.widgets, (n) => {
      if (!dlg && n.type === "dialog") dlg = n;
    });
    if (dlg) {
      let dlgLost = 0;
      const gotDlg = back.doc ? findNode(back.doc, dlg.id) : null;
      const origCount = (dlg.children ?? []).length;
      if (!gotDlg || gotDlg.type !== "dialog") {
        console.error(`[fail] 恒等回写丢了整个 dialog：${dlg.id}`);
        lost += 1;
      } else {
        const gotChildren = gotDlg.children ?? [];
        if (gotChildren.length !== origCount) {
          console.error(
            `[fail] 恒等回写丢了 dialog 子控件：${dlg.id} 子控件 ${origCount} -> ${gotChildren.length}`,
          );
          lost += 1;
          dlgLost += 1;
        }
        if (gotDlg.center !== false) {
          console.error(`[fail] 恒等回写丢了 dialog 的 center:false：${dlg.id}`);
          lost += 1;
          dlgLost += 1;
        }
        const probe = gotChildren.find((c) => c.id === "chk_dlg_child");
        if (!probe) {
          console.error("[fail] 恒等回写丢了 dialog 探针子控件：chk_dlg_child");
          lost += 1;
          dlgLost += 1;
        } else {
          if (probe.type !== "button") {
            console.error(`[fail] 探针子控件类型变了：button -> ${probe.type}`);
            lost += 1;
            dlgLost += 1;
          }
          if ((probe.text ?? "") !== "对话框里加的按钮") {
            console.error(`[fail] 探针子控件文字变了：「对话框里加的按钮」->「${probe.text}」`);
            lost += 1;
            dlgLost += 1;
          }
          if (knownCallback && probe.events?.click !== knownCallback) {
            console.error(
              `[fail] 探针子控件的事件丢了：click ${knownCallback} -> ${probe.events?.click ?? "(无)"}`,
            );
            lost += 1;
            dlgLost += 1;
          }
        }
        if (!dlgLost) {
          console.log(`[ok] 恒等回写：dialog 子树无损（${gotChildren.length} 个子控件，含探针按钮）`);
        }
      }
    }
  }

  if (lost) process.exit(1);
  console.log(`[ok] 恒等回写：${expect.size} 处绑定（bind）无损`);
}

/* 4. 交给 C 运行时读 */
let out = "";
let code = 0;
try {
  out = execFileSync(exe, ["--selftest", output], { encoding: "utf8" });
} catch (e) {
  out = String(e.stdout ?? "") + String(e.stderr ?? "");
  code = e.status ?? 1;
}

process.stdout.write(out);

const ok = code === 0 && /selftest OK/.test(out);
if (ok) {
  console.log(`[ok] 运行时接受了画布写出的文件（${exe} --selftest 通过）`);
  process.exit(0);
}
console.error(`[fail] 运行时拒绝或报警：exit=${code}`);
process.exit(1);
