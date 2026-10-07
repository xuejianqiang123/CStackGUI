/* Palette.tsx -- 控件面板：点一下加到画布，拖一下就放到鼠标位置 */
import { useRef, useState } from "react";
import { KINDS, type KindInfo } from "../model";

type Props = {
  onAdd: (type: string) => void;
  onStartDrop: (type: string) => void;
};

export default function Palette({ onAdd, onStartDrop }: Props) {
  const [open, setOpen] = useState<string | null>(null);
  const dragRef = useRef<{ type: string; x: number; y: number } | null>(null);

  const groups: { name: string; items: KindInfo[] }[] = [];
  for (const k of KINDS) {
    let g = groups.find((x) => x.name === k.group);
    if (!g) {
      g = { name: k.group, items: [] };
      groups.push(g);
    }
    g.items.push(k);
  }

  return (
    <div className="panel palette">
      <div className="panel-head">控件</div>
      <div className="palette-body">
        {groups.map((g) => (
          <div key={g.name} className="palette-group">
            <div className="palette-group-name" onClick={() => setOpen(open === g.name ? null : g.name)}>
              {g.name}
            </div>
            <div className="palette-items">
              {g.items.map((k) => (
                <button
                  key={k.type}
                  className="palette-item"
                  title={k.container ? `${k.label}（容器，可容纳子控件）` : k.label}
                  onPointerDown={(e) => {
                    dragRef.current = { type: k.type, x: e.clientX, y: e.clientY };
                    /* 鼠标指针没有隐式捕获：不 capture，指针一出按钮 move/up 就收不到，
                       快速一拖 onStartDrop 不触发、松手也不加控件（点了像没反应）。 */
                    e.currentTarget.setPointerCapture(e.pointerId);
                  }}
                  onPointerUp={(e) => {
                    const started = dragRef.current;
                    dragRef.current = null;
                    if (!started) return;
                    const moved = Math.hypot(e.clientX - started.x, e.clientY - started.y);
                    if (moved > 6) return; // 拖动由 canvas 处理
                    onAdd(k.type);
                  }}
                  onPointerMove={(e) => {
                    const started = dragRef.current;
                    if (!started) return;
                    if (Math.hypot(e.clientX - started.x, e.clientY - started.y) > 6) {
                      dragRef.current = null;
                      onStartDrop(k.type);
                    }
                  }}
                >
                  <span className={`glyph glyph-${k.type}`} />
                  <span className="palette-label">{k.label}</span>
                </button>
              ))}
            </div>
          </div>
        ))}
      </div>
      <div className="panel-foot">点一下加到画布 · 按住拖到指定位置</div>
    </div>
  );
}
