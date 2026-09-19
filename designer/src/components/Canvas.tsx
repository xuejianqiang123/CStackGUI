/* Canvas.tsx -- 画布：选中、拖动、8 向缩放、拖进/拖出容器
 *
 * 坐标一律是「窗口内的逻辑像素」，和 .cgui 里存的值、Win32 运行时用的值完全一致。
 * 有容器（分组框 / 容器块）时，子控件坐标相对容器；拖动结束后会重新判定归属。
 */
import { useEffect, useRef, useState } from "react";
import WidgetView from "./WidgetView";
import { absRect, containerAt, findNode, findParent, isInside, makeNode, snap, type CgDoc, type CgNode } from "../model";

type Props = {
  doc: CgDoc;
  selectedId: string | null;
  zoom: number;
  snapOn: boolean;
  showGrid: boolean;
  onSelect: (id: string | null) => void;
  onBeginHistory: () => void;
  onApply: (mutator: (doc: CgDoc) => void) => void;
  /** 从调色板拖进来的类型 */
  pendingDrop: string | null;
  onDropDone: () => void;
};

type DragState =
  | { kind: "none" }
  | { kind: "move"; id: string; startX: number; startY: number; origin: { x: number; y: number }; moved: boolean }
  | {
      kind: "resize";
      id: string;
      dir: string;
      startX: number;
      startY: number;
      origin: { x: number; y: number; w: number; h: number };
    };

const HANDLES = ["nw", "n", "ne", "e", "se", "s", "sw", "w"] as const;

export default function Canvas(props: Props) {
  const { doc, selectedId, zoom, snapOn, showGrid, onSelect, onBeginHistory, onApply, pendingDrop, onDropDone } = props;
  const stageRef = useRef<HTMLDivElement>(null);
  const [drag, setDrag] = useState<DragState>({ kind: "none" });
  const [snapLines, setSnapLines] = useState<{ x: number[]; y: number[] }>({ x: [], y: [] });

  const selected = selectedId ? findNode(doc, selectedId) : null;

  /* -------------------------------------------------- 指针交互 */
  const startMove = (e: React.PointerEvent, node: CgNode) => {
    e.stopPropagation();
    onSelect(node.id);
    if (e.button !== 0) return;
    onBeginHistory();
    setDrag({
      kind: "move",
      id: node.id,
      startX: e.clientX,
      startY: e.clientY,
      origin: { x: node.rect.x, y: node.rect.y },
      moved: false,
    });
  };

  const startResize = (e: React.PointerEvent, dir: string) => {
    if (!selected) return;
    e.stopPropagation();
    onBeginHistory();
    setDrag({
      kind: "resize",
      id: selected.id,
      dir,
      startX: e.clientX,
      startY: e.clientY,
      origin: { ...selected.rect },
    });
  };

  useEffect(() => {
    if (drag.kind === "none") return;

    const onMove = (e: PointerEvent) => {
      const dx = (e.clientX - drag.startX) / zoom;
      const dy = (e.clientY - drag.startY) / zoom;

      if (drag.kind === "move") {
        onApply((d) => {
          const n = findNode(d, drag.id);
          if (!n) return;
          const rawX = drag.origin.x + dx;
          const rawY = drag.origin.y + dy;
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

          n.rect.x = nx;
          n.rect.y = ny;
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
    };

    const onUp = () => {
      if (drag.kind === "move") {
        /* 拖动结束：重新判定它该待在哪个容器里 */
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
  }, [drag, doc, zoom, snapOn, onApply]);

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

  return (
    <div className="viewport" onPointerDown={() => onSelect(null)}>
      <div className="stage" ref={stageRef} style={{ transform: `scale(${zoom})` }}>
        <div className="app-window" style={{ width: doc.app.width, height: doc.app.height, background: doc.app.bg }}>
          <div className="titlebar">
            <span className="titlebar-icon" />
            <span className="titlebar-text">{doc.app.title}</span>
            <span className="titlebar-size">
              {doc.app.width}×{doc.app.height}
            </span>
          </div>

          {/* 菜单栏在窗口框架上，不在客户区里（和运行时一致） */}
          {menuNode ? (
            <div
              className={`menubar${selectedId === menuNode.id ? " selected" : ""}`}
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
          <div className="client-area" onPointerDown={() => onSelect(null)}>
            {showGrid && <div className="grid-layer" style={{ backgroundSize: "16px 16px" }} />}
            {doc.widgets.map((n) => (
              <WidgetView
                key={n.id}
                node={n}
                depth={0}
                selectedId={selectedId}
                onSelect={(id) => onSelect(id)}
                onPointerDown={startMove}
              />
            ))}
            {snapLines.x.map((x, i) => (
              <div key={`sx${i}`} className="snapline-v" style={{ left: x }} />
            ))}
            {snapLines.y.map((y, i) => (
              <div key={`sy${i}`} className="snapline-h" style={{ top: y }} />
            ))}

            {/* 选中框画在客户区坐标系里，和控件同一套坐标 */}
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

      <div className="viewport-hint">
        拖拽移动 · Delete 删除 · Ctrl+D 复制 · 拖到分组框里会自动成为子控件
      </div>
    </div>
  );
}
