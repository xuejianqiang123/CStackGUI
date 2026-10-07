/* Canvas.tsx -- 画布：选中、拖动、8 向缩放、拖进/拖出容器
 *
 * 坐标一律是「窗口内的逻辑像素」，和 .cgui 里存的值、Win32 运行时用的值完全一致。
 * 有容器（分组框 / 容器块）时，子控件坐标相对容器；拖动结束后会重新判定归属。
 */
import { useEffect, useMemo, useRef, useState } from "react";
import WidgetView from "./WidgetView";
import {
  absRect,
  alignNodes,
  containerAt,
  findNode,
  findParent,
  isInside,
  makeNode,
  snap,
  topLevelOnly,
  walk,
  type CgAlignMode,
  type CgDoc,
  type CgNode,
} from "../model";
import { useImage } from "../imagecache";

type Props = {
  doc: CgDoc;
  /** 选中集（顺序 = 点选顺序，最后一个是主选框） */
  selectedIds: string[];
  zoom: number;
  snapOn: boolean;
  showGrid: boolean;
  /** 点选：additive = Shift/Ctrl 加选；id 为 null = 点空白 */
  onSelect: (id: string | null, additive?: boolean) => void;
  /** 框选：一次给一串 id */
  onSelectMany: (ids: string[]) => void;
  onBeginHistory: () => void;
  onApply: (mutator: (doc: CgDoc) => void) => void;
  /** 从调色板拖进来的类型 */
  pendingDrop: string | null;
  onDropDone: () => void;
  /** 有外部改动挂起（.cgui 被别的程序改了，而画布上有未保存改动）：底部出通知条让人先选 */
  externalPending: boolean;
  onLoadExternal: () => void;
  onDismissExternal: () => void;
};

type DragState =
  | { kind: "none" }
  | {
      kind: "move";
      /** 主选框 id：吸附与对齐线都以它为准 */
      id: string;
      /** 真正要一起动的节点（已剔掉「祖先也在选中集里」的子控件，见 model/align.ts 铁律 1） */
      ids: string[];
      /** 每个节点的起始坐标：整组按同一个位移走，相对间距因此不变 */
      origins: Record<string, { x: number; y: number }>;
      startX: number;
      startY: number;
      moved: boolean;
    }
  | {
      kind: "resize";
      id: string;
      dir: string;
      startX: number;
      startY: number;
      origin: { x: number; y: number; w: number; h: number };
      moved: boolean;
    }
  /** 空白处拖框选。sx/sy/cx/cy 是**客户区**内的逻辑像素；ox/oy 是框选起手时
   *  客户区左上角的屏幕坐标 —— 指针坐标减掉它才是客户区坐标，而控件坐标正是这个口径。 */
  | {
      kind: "marquee";
      ox: number;
      oy: number;
      sx: number;
      sy: number;
      cx: number;
      cy: number;
      /** 起手时的选择集（Shift 加选时要把它们并进去） */
      base: string[];
      moved: boolean;
    };

const HANDLES = ["nw", "n", "ne", "e", "se", "s", "sw", "w"] as const;

/** 拖动/框选共用的最小位移：低于它算「点一下」，不改坐标、不压历史 */
const DRAG_MIN = 3;

export default function Canvas(props: Props) {
  const {
    doc,
    selectedIds,
    zoom,
    snapOn,
    showGrid,
    onSelect,
    onSelectMany,
    onBeginHistory,
    onApply,
    pendingDrop,
    onDropDone,
    externalPending,
    onLoadExternal,
    onDismissExternal,
  } = props;
  const stageRef = useRef<HTMLDivElement>(null);
  const [drag, setDrag] = useState<DragState>({ kind: "none" });
  const [snapLines, setSnapLines] = useState<{ x: number[]; y: number[] }>({ x: [], y: [] });

  /** 主选框：实线外框 + 8 个缩放手柄。多选时只有它有手柄，免得一屏都是握点 */
  const primaryId = selectedIds.length > 0 ? selectedIds[selectedIds.length - 1] : null;
  const selected = primaryId ? findNode(doc, primaryId) : null;
  const selectedSet = useMemo(() => new Set(selectedIds), [selectedIds]);

  /** 窗口图标（`app.icon`）的预览图：运行时把它当窗口 / 托盘图标（`LoadImageW(IMAGE_ICON)`），
   *  画布把它缩到标题栏那一小块；读不到就保持原来的占位小方块。 */
  const appIcon = useImage(doc.app.icon);

  /* -------------------------------------------------- 指针交互 */
  const startMove = (e: React.PointerEvent, node: CgNode) => {
    e.stopPropagation();
    const additive = e.shiftKey || e.ctrlKey || e.metaKey;
    /* Shift/Ctrl + 点：只做加选/减选，不进拖动 —— 否则「加选」会顺手把控件挪到吸附点上 */
    if (additive) {
      onSelect(node.id, true);
      return;
    }
    if (!selectedSet.has(node.id)) onSelect(node.id);
    if (e.button !== 0) return;

    /* 拖的是**整组**；点一个没被选中的控件时它就是新的单选。
       这里不压历史、也不动坐标：等真的开始拖（见 onMove 的位移阈值）再压，
       否则「点一下选中」也会进撤销栈、还可能被吸附挪走。 */
    const moving = selectedSet.has(node.id) ? topLevelOnly(doc, selectedIds) : [node.id];
    const origins: Record<string, { x: number; y: number }> = {};
    for (const id of moving) {
      const n = findNode(doc, id);
      if (n) origins[id] = { x: n.rect.x, y: n.rect.y };
    }
    setDrag({
      kind: "move",
      id: node.id,
      ids: Object.keys(origins),
      origins,
      startX: e.clientX,
      startY: e.clientY,
      moved: false,
    });
  };

  const startResize = (e: React.PointerEvent, dir: string) => {
    if (!selected) return;
    e.stopPropagation();
    setDrag({
      kind: "resize",
      id: selected.id,
      dir,
      startX: e.clientX,
      startY: e.clientY,
      origin: { ...selected.rect },
      moved: false,
    });
  };

  /* 在客户区空白处按下 = 拖框选。坐标换算成**客户区内的逻辑像素** —— 控件的 rect / absRect
     就是这个口径；拿 stage 的矩形来算会整体差一个标题栏 + 菜单栏的高度。 */
  const startMarquee = (e: React.PointerEvent<HTMLDivElement>) => {
    if (e.button !== 0) return;
    const rect = e.currentTarget.getBoundingClientRect();
    const lx = (e.clientX - rect.left) / zoom;
    const ly = (e.clientY - rect.top) / zoom;
    const additive = e.shiftKey || e.ctrlKey || e.metaKey;
    setDrag({
      kind: "marquee",
      ox: rect.left,
      oy: rect.top,
      sx: lx,
      sy: ly,
      cx: lx,
      cy: ly,
      base: additive ? [...selectedIds] : [],
      moved: false,
    });
  };

  useEffect(() => {
    if (drag.kind === "none") return;

    const onMove = (e: PointerEvent) => {
      /* 框选分支必须放最前面：它既不用 move/resize 的 startX/startY，也不压历史 */
      if (drag.kind === "marquee") {
        const lx = (e.clientX - drag.ox) / zoom;
        const ly = (e.clientY - drag.oy) / zoom;
        if (!drag.moved && Math.abs(lx - drag.sx) < DRAG_MIN && Math.abs(ly - drag.sy) < DRAG_MIN) return;
        setDrag({ ...drag, cx: lx, cy: ly, moved: true });
        return;
      }

      const dx = (e.clientX - drag.startX) / zoom;
      const dy = (e.clientY - drag.startY) / zoom;

      /* 「点一下」没真拖动时绝不能改坐标：位移低于阈值直接忽略，
       * 否则 4px 吸附会在点选的瞬间把控件挪到栅格上（坐标自己变的根因）。 */
      if (!drag.moved && Math.abs(e.clientX - drag.startX) < DRAG_MIN && Math.abs(e.clientY - drag.startY) < DRAG_MIN) return;

      if (!drag.moved) onBeginHistory();   /* 历史压在真正开始拖的一刻 */

      if (drag.kind === "move") {
        const o0 = drag.origins[drag.id];
        if (!o0) return;
        onApply((d) => {
          const n = findNode(d, drag.id);
          if (!n) return;
          const rawX = o0.x + dx;
          const rawY = o0.y + dy;
          let nx = snap(rawX, 4, snapOn);
          let ny = snap(rawY, 4, snapOn);

          /* 简单对齐：和同级控件/窗口边缘比较，近则吸附（顺手，不做全面对齐系统） */
          const parent = findParent(d, drag.id);
          const sibs = (parent ? parent.children ?? [] : d.widgets).filter((s) => s.id !== drag.id);
          const vx: number[] = [];
          const vy: number[] = [];
          const boundsW = parent ? parent.rect.w : d.app.width;
          const boundsH = parent ? parent.rect.h : d.app.height;
          for (const s of sibs) {
            for (const cand of [s.rect.x, s.rect.x + s.rect.w / 2, s.rect.x + s.rect.w]) {
              if (Math.abs(cand - nx) <= 5) vx.push(cand);
              if (Math.abs(cand - (nx + n.rect.w)) <= 5) vx.push(cand - n.rect.w);
            }
            for (const cand of [s.rect.y, s.rect.y + s.rect.h / 2, s.rect.y + s.rect.h]) {
              if (Math.abs(cand - ny) <= 5) vy.push(cand);
              if (Math.abs(cand - (ny + n.rect.h)) <= 5) vy.push(cand - n.rect.h);
            }
          }
          if (Math.abs(nx) <= 5) vx.push(0);
          if (Math.abs(boundsW - (nx + n.rect.w)) <= 5) vx.push(boundsW - n.rect.w);
          if (Math.abs(ny) <= 5) vy.push(0);
          if (Math.abs(boundsH - (ny + n.rect.h)) <= 5) vy.push(boundsH - n.rect.h);
          if (vx.length) nx = vx[0];
          if (vy.length) ny = vy[0];
          setSnapLines({ x: vx.slice(0, 1), y: vy.slice(0, 1) });

          /* 整组按主选框算出来的位移平移：组内相对位置一个像素都不会变 */
          const ddx = nx - o0.x;
          const ddy = ny - o0.y;
          for (const id of drag.ids) {
            const org = drag.origins[id];
            const t = findNode(d, id);
            if (!org || !t) continue;
            t.rect.x = org.x + ddx;
            t.rect.y = org.y + ddy;
          }
        });
        setDrag({ ...drag, moved: true });
        return;
      }

      /* resize */
      onApply((d) => {
        const n = findNode(d, drag.id);
        if (!n) return;
        const o = drag.origin;
        let { x, y, w, h } = o;
        const dir = drag.dir;
        if (dir.includes("e")) w = o.w + dx;
        if (dir.includes("s")) h = o.h + dy;
        if (dir.includes("w")) {
          x = o.x + dx;
          w = o.w - dx;
        }
        if (dir.includes("n")) {
          y = o.y + dy;
          h = o.h - dy;
        }
        const minW = 8;
        const minH = dir.includes("n") || dir.includes("s") ? 4 : 8;
        if (w < minW) {
          if (dir.includes("w")) x = o.x + o.w - minW;
          w = minW;
        }
        if (h < minH) {
          if (dir.includes("n")) y = o.y + o.h - minH;
          h = minH;
        }
        n.rect = {
          x: snap(x, 4, snapOn),
          y: snap(y, 4, snapOn),
          w: Math.max(2, snap(w, 4, snapOn)),
          h: Math.max(2, snap(h, 4, snapOn)),
        };
      });
      setDrag({ ...drag, moved: true });
    };

    const onUp = () => {
      if (drag.kind === "marquee") {
        const x0 = Math.min(drag.sx, drag.cx);
        const y0 = Math.min(drag.sy, drag.cy);
        const w = Math.abs(drag.cx - drag.sx);
        const h = Math.abs(drag.cy - drag.sy);
        if (drag.moved) {
          /* 命中判定用**绝对坐标**（容器里的子控件也要能被框到），命中结果再归一化一次：
             框住整个分组框时只选分组框，不把里面的子控件也一个个选上。 */
          const hits: string[] = [];
          walk(doc.widgets, (n) => {
            const r = absRect(doc, n);
            if (r.x < x0 + w && r.x + r.w > x0 && r.y < y0 + h && r.y + r.h > y0) hits.push(n.id);
          });
          onSelectMany(topLevelOnly(doc, [...drag.base, ...hits]));
        } else if (drag.base.length === 0) {
          onSelect(null);   /* 点空白 = 取消选择；带 Shift 起框时不动原有选择 */
        }
      }
      if (drag.kind === "move" && drag.moved && drag.ids.length === 1) {
        /* 拖动结束：重新判定它该待在哪个容器里（只是点选、没拖动就别改归属） */
        onApply((d) => {
          const node = findNode(d, drag.id);
          if (!node) return;
          const parent = findParent(d, drag.id);
          const abs = absRect(d, node);
          const target = containerAt(d, abs.x + abs.w / 2, abs.y + abs.h / 2, node.id);
          const targetParent = target ?? null;
          const sameParent = (targetParent?.id ?? null) === (parent?.id ?? null);
          if (sameParent) return;
          if (targetParent && isInside(d, targetParent.id, node.id)) return;

          const list = parent ? parent.children ?? [] : d.widgets;
          const i = list.findIndex((n) => n.id === node.id);
          if (i < 0) return;
          list.splice(i, 1);

          const base = targetParent
            ? { x: absRect(d, targetParent).x, y: absRect(d, targetParent).y }
            : { x: 0, y: 0 };
          node.rect.x = abs.x - base.x;
          node.rect.y = abs.y - base.y;

          if (targetParent && targetParent.type === "tabs") {
            targetParent.children = targetParent.children ?? [];
            node.page = targetParent.value ?? 0;
            targetParent.children.push(node);
          } else {
            /* 从 tabs 拖出时清掉残留的分页号，否则脏 page 会一直写进 .cgui */
            delete node.page;
            d.widgets.push(node);
          }
        });
      }
      setDrag({ kind: "none" });
      setSnapLines({ x: [], y: [] });
    };

    window.addEventListener("pointermove", onMove);
    window.addEventListener("pointerup", onUp);
    return () => {
      window.removeEventListener("pointermove", onMove);
      window.removeEventListener("pointerup", onUp);
    };
  }, [drag, doc, zoom, snapOn, onApply, onBeginHistory, onSelect, onSelectMany]);

  /* -------------------------------------------------- 调色板拖入 */
  useEffect(() => {
    if (!pendingDrop) return;
    const place = (clientX: number, clientY: number) => {
      const rect = stageRef.current?.getBoundingClientRect();
      if (!rect) return;
      const px = (clientX - rect.left) / zoom;
      const py = (clientY - rect.top) / zoom;
      const target = containerAt(doc, px, py);
      const base = target ? absRect(doc, target) : { x: 0, y: 0 };
      const node = makeNode(pendingDrop, snap(px - base.x - 40, 4, snapOn), snap(py - base.y - 16, 4, snapOn));
      /* 拖进标签页：默认归到它当前显示的那一页 */
      if (target?.type === "tabs") node.page = target.value ?? 0;
      onBeginHistory();
      onApply((d) => {
        if (target) {
          const t = findNode(d, target.id);
          if (t) {
            t.children = t.children ?? [];
            t.children.push(node);
            return;
          }
        }
        d.widgets.push(node);
      });
      onSelect(node.id);
      onDropDone();
    };
    const onUp = (e: PointerEvent) => place(e.clientX, e.clientY);
    window.addEventListener("pointerup", onUp, { once: true });
    return () => window.removeEventListener("pointerup", onUp);
  }, [pendingDrop, doc, zoom, snapOn, onApply, onBeginHistory, onSelect, onDropDone]);

  const parentOfSelected = selected ? findParent(doc, selected.id) : null;
  /* 窗口只能有一根菜单栏，和运行时一样只认第一个 */
  const menuNode = doc.widgets.find((n) => n.type === "menu") ?? null;

  /* 对齐/分布的作用对象要归一化（祖先也被选中的子控件剔掉）：
     否则工具条上写着「N 个控件」，实际被挪的却是另一批。 */
  const alignIds = topLevelOnly(doc, selectedIds);

  /* 框选矩形：只有真拖动过才画，免得「点一下空白」闪一个 0×0 的框 */
  const marqueeRect =
    drag.kind === "marquee" && drag.moved
      ? {
          left: Math.min(drag.sx, drag.cx),
          top: Math.min(drag.sy, drag.cy),
          width: Math.abs(drag.cx - drag.sx),
          height: Math.abs(drag.cy - drag.sy),
        }
      : null;

  const doAlign = (mode: CgAlignMode) => {
    onBeginHistory();
    onApply((d) => {
      alignNodes(d, selectedIds, mode);
    });
  };

  /* ------------------------------------------------------------------
   * 窗口框架：把 .cgui 的 app 属性（边框 / 控制按钮）如实画出来，所见即所得。
   * 画布上的标题栏 = Win32 的非客户区，客户区尺寸仍是 app.width × app.height。
   * ------------------------------------------------------------------ */
  const app = doc.app;
  const showTitlebar = app.border !== "none"; /* 无边框 = 没有标题栏 */
  const sysmenu = app.controlBox !== false;    /* 控制按钮组（含关闭钮） */

  return (
    /* 外层是画布栏：滚动区 + 固定底栏（提示行不进滚动区，放大内容时不会盖在窗口上） */
    <div className="canvas-root">
      {/* 对齐/分布条：选中 ≥2 才出现，压在画布顶部居中 —— 不常驻，不白占地方 */}
      {alignIds.length > 1 ? (
        <div className="align-bar" role="toolbar" aria-label="对齐与分布">
          <span className="align-group">
            <span className="align-label">水平</span>
            <button className="mini" title="左对齐（对到选中项外接框的左边）" onClick={() => doAlign("left")}>
              左
            </button>
            <button className="mini" title="水平居中" onClick={() => doAlign("hcenter")}>
              中
            </button>
            <button className="mini" title="右对齐" onClick={() => doAlign("right")}>
              右
            </button>
          </span>

          <span className="align-group">
            <span className="align-label">垂直</span>
            <button className="mini" title="上对齐" onClick={() => doAlign("top")}>
              上
            </button>
            <button className="mini" title="垂直居中" onClick={() => doAlign("vcenter")}>
              中
            </button>
            <button className="mini" title="下对齐" onClick={() => doAlign("bottom")}>
              下
            </button>
          </span>

          <span className="align-group">
            <span className="align-label">等距</span>
            <button
              className="mini"
              disabled={alignIds.length < 3}
              title={alignIds.length < 3 ? "至少选中 3 个才能等距分布" : "横向等距：首尾不动，中间的间隙均分"}
              onClick={() => doAlign("hdist")}
            >
              横
            </button>
            <button
              className="mini"
              disabled={alignIds.length < 3}
              title={alignIds.length < 3 ? "至少选中 3 个才能等距分布" : "纵向等距：首尾不动，中间的间隙均分"}
              onClick={() => doAlign("vdist")}
            >
              纵
            </button>
          </span>

          <span className="align-group">
            <span className="align-label">尺寸</span>
            <button className="mini" title="等宽：取选中项里最宽的那个（放大小的，不缩大的）" onClick={() => doAlign("samew")}>
              等宽
            </button>
            <button className="mini" title="等高：取选中项里最高的那个（放大小的，不缩大的）" onClick={() => doAlign("sameh")}>
              等高
            </button>
            <button className="mini" title="同尺寸：宽和高都取选中项里的最大值" onClick={() => doAlign("samesize")}>
              同尺寸
            </button>
          </span>

        </div>
      ) : null}

      <div className="viewport" onPointerDown={() => onSelect(null)}>
        <div className="stage" ref={stageRef} style={{ transform: `scale(${zoom})` }}>
          <div className={`app-window${showTitlebar ? "" : " borderless"}`} style={{ width: doc.app.width, height: doc.app.height, background: doc.app.bg }}>
            {showTitlebar ? (
              <div className="titlebar">
                {appIcon ? (
                  <img className="titlebar-icon-img" src={appIcon} alt="" draggable={false} />
                ) : (
                  <span className="titlebar-icon" />
                )}
                <span className="titlebar-text">{app.title}</span>
                <span className="titlebar-size">
                  {app.width}×{app.height}
                </span>
                {sysmenu ? (
                  <span className="titlebar-controls" title="窗口控制按钮（跟随「控制按钮 / 最小化 / 最大化」属性）">
                    {app.minimizeBox !== false ? <span className="tb-btn" title="最小化按钮">–</span> : null}
                    {app.maximizeBox !== false ? <span className="tb-btn" title="最大化按钮">□</span> : null}
                    <span className="tb-btn tb-close" title="关闭按钮">
                      ✕
                    </span>
                  </span>
                ) : null}
              </div>
            ) : null}

            {/* 菜单栏在窗口框架上，不在客户区里（和运行时一致） */}
            {menuNode ? (
              <div
                className={`menubar${selectedSet.has(menuNode.id) ? " selected" : ""}`}
                title={`${menuNode.id}（菜单栏：没有矩形，运行时挂在窗口上）`}
                onPointerDown={(e) => {
                  e.stopPropagation();
                  onSelect(menuNode.id);
                }}
              >
                {(menuNode.nodes ?? []).map((item, i) => (
                  <span key={i} className="menubar-item">
                    {item.text}
                  </span>
                ))}
                {!menuNode.nodes?.length ? <span className="muted small">菜单栏（还没有菜单项）</span> : null}
              </div>
            ) : null}
            <div className="client-area" onPointerDown={startMarquee}>
              {showGrid && <div className="grid-layer" style={{ backgroundSize: "16px 16px" }} />}
              {doc.widgets.map((n) => (
                <WidgetView
                  key={n.id}
                  node={n}
                  depth={0}
                  selected={selectedSet}
                  primaryId={primaryId}
                  onPointerDown={startMove}
                />
              ))}
              {snapLines.x.map((x, i) => (
                <div key={`sx${i}`} className="snapline-v" style={{ left: x }} />
              ))}
              {snapLines.y.map((y, i) => (
                <div key={`sy${i}`} className="snapline-h" style={{ top: y }} />
              ))}

              {/* 框选矩形：客户区坐标系，和控件同一套 */}
              {marqueeRect ? <div className="marquee" style={marqueeRect} /> : null}

              {/* 选中框画在客户区坐标系里，和控件同一套坐标。
                  多选时**只有主选框**带 8 个手柄；其余选中项由 WidgetView 的
                  .selected-2 虚线表示，免得一屏都是握点。 */}
              {selected && (
                <div
                  className="handles"
                  style={{
                    left: absRect(doc, selected).x,
                    top: absRect(doc, selected).y,
                    width: selected.rect.w,
                    height: selected.rect.h,
                  }}
                >
                  {HANDLES.map((dir) => (
                    <span key={dir} className={`handle handle-${dir}`} onPointerDown={(e) => startResize(e, dir)} />
                  ))}
                  <span className="size-tip">
                    {Math.round(selected.rect.w)}×{Math.round(selected.rect.h)}
                    {parentOfSelected ? ` · in ${parentOfSelected.id}` : ""}
                  </span>
                </div>
              )}
            </div>
          </div>
        </div>
      </div>

      {/* 外部改了 .cgui、而画布上有未保存改动：问一句，绝不替人丢东西。
          放在视口与提示行之间（正常文档流）—— 顶部已经被对齐/分布条占着了。 */}
      {externalPending ? (
        <div className="notice-bar">
          <span className="notice-text">
            <strong>.cgui 在外部被改了</strong>（编辑器 / AI 技能 / git 换版本），而画布上还有没保存的改动：
            载入外部版本会丢掉它们。
          </span>
          <button className="mini" onClick={onLoadExternal}>
            载入外部版本
          </button>
          <button className="mini" onClick={onDismissExternal}>
            留着我的改动
          </button>
        </div>
      ) : null}

      <div className="viewport-hint">
        拖拽移动 · Shift 点选多选（空白处可拖框选） · Delete 删除 · Ctrl+A 全选 · 拖进分组框会成为子控件
        {selectedIds.length > 1 ? ` · 已选中 ${selectedIds.length} 个（Ctrl+A 选的是顶层），对齐工具在上方` : ""}
      </div>
    </div>
  );
}
