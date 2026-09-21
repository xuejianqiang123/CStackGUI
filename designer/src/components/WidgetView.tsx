/* WidgetView.tsx -- 把一个 .cgui 节点画成「接近 Win32 原生控件」的样子
 *
 * 画布的职责是让用户在编译前就能看出运行时大概长什么样，
 * 所以这里刻意用平实的原生风格，而不是网页风格。
 */
import type { CSSProperties } from "react";
import { EVENT_LABEL, CG_EVENTS, type CgNode } from "../model";

const fontOf = (node: CgNode): CSSProperties => {
  const style = node.style ?? {};
  const px = style.size ? `${style.size}pt` : "12px";
  return {
    fontFamily: style.font ? `"${style.font}", "Microsoft YaHei UI", sans-serif` : '"Microsoft YaHei UI", sans-serif',
    fontSize: px,
    fontWeight: style.bold ? 700 : 400,
    color: style.fg ?? "#1D1B20",
  };
};

function inner(node: CgNode) {
  const value = node.value ?? 40;
  const min = node.min ?? 0;
  const max = node.max ?? 100;
  const pct = max > min ? Math.min(100, Math.max(0, ((value - min) / (max - min)) * 100)) : 0;

  switch (node.type) {
    case "button": {
      const st = node.style ?? {};
      const skin: CSSProperties = {};
      if (st.bg) {
        skin.background = st.bg;
        const r = parseInt(st.bg.slice(1, 3), 16), g2 = parseInt(st.bg.slice(3, 5), 16), b = parseInt(st.bg.slice(5, 7), 16);
        skin.color = (0.2126 * r + 0.7152 * g2 + 0.0722 * b) / 255 < 0.55 ? "#FFFFFF" : "#000000";
      }
      if (st.radius != null) skin.borderRadius = st.radius;
      return <div className="ctl-button" style={skin}>{node.text || ""}</div>;
    }

    case "label":
      return <div className="ctl-label">{node.text || ""}</div>;

    case "edit":
      return <div className="ctl-edit">{(node.text || "").replace(/\n/g, " ")}</div>;

    case "multiedit":
      return <div className="ctl-edit ctl-edit-multi">{node.text || ""}</div>;

    case "checkbox":
      return (
        <div className="ctl-check">
          <span className={`ctl-box${node.checked ? " on" : ""}`}>{node.checked ? "✓" : ""}</span>
          <span>{node.text || ""}</span>
        </div>
      );

    case "radio":
      return (
        <div className="ctl-check">
          <span className={`ctl-radio${node.checked ? " on" : ""}`}>{node.checked ? "" : ""}</span>
          <span>{node.text || ""}</span>
        </div>
      );

    case "listbox":
      return <div className="ctl-list" />;

    case "combo":
      return (
        <div className="ctl-combo">
          <span>{node.text || ""}</span>
          <span className="ctl-arrow">▾</span>
        </div>
      );

    case "slider":
      return (
        <div className="ctl-slider">
          <div className="ctl-track" />
          <div className="ctl-thumb" style={{ left: `${pct}%` }} />
        </div>
      );

    case "progress":
      return (
        <div className="ctl-progress">
          <div className="ctl-fill" style={{ width: `${pct}%` }} />
        </div>
      );

    case "separator":
      return <div className="ctl-sep" />;

    case "image":
      return <div className="ctl-image">{node.src ? node.src.split(/[\\/]/).pop() : "未设置图片"}</div>;

    case "switch":
      return (
        <div className="ctl-switch">
          {node.text ? <span className="ctl-switch-label">{node.text}</span> : null}
          <span className={`ctl-switch-track${node.checked ? " on" : ""}`}>
            <span className="ctl-switch-knob" />
          </span>
        </div>
      );

    case "spinner":
      return (
        <div className="ctl-spinner">
          <span className="ctl-spinner-text">{node.value ?? 0}</span>
          <span className="ctl-spinner-arrows">
            <span>▲</span>
            <span>▼</span>
          </span>
        </div>
      );

    case "tabs": {
      const labels = node.items ?? [];
      const cur = node.value ?? 0;
      return (
        <div className="ctl-tabs">
          {labels.map((t, i) => (
            <span key={i} className={`ctl-tab${i === cur ? " on" : ""}`}>
              {t || `页面 ${i + 1}`}
            </span>
          ))}
        </div>
      );
    }

    case "tree": {
      const lines: { text: string; depth: number }[] = [];
      const push = (items: typeof node.nodes, depth: number) => {
        for (const it of items ?? []) {
          if (lines.length >= 14) return;
          lines.push({ text: it.text, depth });
          push(it.children, depth + 1);
        }
      };
      push(node.nodes, 0);
      return (
        <div className="ctl-tree">
          {lines.map((l, i) => (
            <div key={i} className="ctl-tree-row" style={{ paddingLeft: 4 + l.depth * 14 }}>
              {l.depth > 0 ? <span className="ctl-tree-elbow">└</span> : null}
              {l.text || "(空)"}
            </div>
          ))}
        </div>
      );
    }

    case "table": {
      const cols = node.columns ?? [];
      const rows = (node.rows ?? []).slice(0, 8);
      return (
        <div className="ctl-table">
          <div className="ctl-table-head">
            {cols.map((c, i) => (
              <span key={i} className="ctl-table-cell" style={c.w ? { width: c.w, flex: "0 0 auto" } : undefined}>
                {c.title}
              </span>
            ))}
          </div>
          {rows.map((r, i) => (
            <div key={i} className="ctl-table-row">
              {cols.map((_, c) => (
                <span key={c} className="ctl-table-cell">
                  {r[c] ?? ""}
                </span>
              ))}
            </div>
          ))}
        </div>
      );
    }

    case "link":
      return <div className="ctl-link">{node.text || "链接"}</div>;

    case "iconbutton":
      return <div className="ctl-iconbtn">图标</div>;

    case "monthcal":
      return (
        <div className="ctl-monthcal">
          <div className="ctl-monthcal-head">{node.text || "2026-09"}</div>
          <div className="ctl-monthcal-grid">
            {["一", "二", "三", "四", "五", "六", "日"].map((d) => (
              <span key={d} className="ctl-monthcal-wd">
                {d}
              </span>
            ))}
            {Array.from({ length: 35 }).map((_, i) => (
              <span key={i} className="ctl-monthcal-day">
                {i + 1 <= 30 ? i + 1 : ""}
              </span>
            ))}
          </div>
        </div>
      );

    case "progressring": {
      const lo = node.min ?? 0;
      const hi = node.max ?? 100;
      const span = hi - lo || 1;
      const pct = Math.max(0, Math.min(1, ((node.value ?? 0) - lo) / span));
      const r = 42;
      const circumference = 2 * Math.PI * r;
      return (
        <svg className="ctl-ring" viewBox="0 0 100 100" preserveAspectRatio="xMidYMid meet">
          <circle cx="50" cy="50" r={r} fill="none" stroke="#E0DAE6" strokeWidth="8" />
          <circle
            cx="50"
            cy="50"
            r={r}
            fill="none"
            stroke="currentColor"
            strokeWidth="8"
            strokeDasharray={`${circumference * pct} ${circumference}`}
            transform="rotate(-90 50 50)"
          />
          <text x="50" y="56" textAnchor="middle" fontSize="20" fill="#1D1B20">
            {Math.round(pct * 100)}%
          </text>
        </svg>
      );
    }

    case "menubutton":
      return (
        <div className="ctl-button">
          {node.text || "菜单"}
          <span className="ctl-menu-caret">▾</span>
        </div>
      );

    case "richtext":
      return <div className="ctl-richtext">{(node.text || "").replace(/\n/g, " ")}</div>;

    case "datepicker":
      return (
        <div className="ctl-date">
          <span>{node.text || (node.format === "time" ? "14:30" : "2026-09-19")}</span>
          <span className="ctl-date-btn">{node.format === "time" ? "▲" : "▾"}</span>
        </div>
      );

    case "toolbar": {
      const labels = node.items ?? [];
      const cur = node.value ?? -1;
      return (
        <div className="ctl-toolbar">
          {labels.map((t, i) => (
            <span key={i} className={`ctl-toolbtn${i === cur ? " on" : ""}`}>
              {t || `按钮 ${i + 1}`}
            </span>
          ))}
        </div>
      );
    }

    case "statusbar": {
      const parts = node.items?.length ? node.items : [""];
      return (
        <div className="ctl-status">
          {parts.map((t, i) => (
            <span key={i} className="ctl-status-part">
              {t}
            </span>
          ))}
        </div>
      );
    }

    case "group":
      return <div className="ctl-group-title">{node.text || ""}</div>;

    case "timer":
      return (
        <div className="ctl-timer">
          <span className="ctl-timer-icon">⏱</span>
          <span>{node.period ? `${node.period}ms` : "已停"}</span>
        </div>
      );

    case "canvas":
      return <div className="ctl-canvas">画板</div>;

    case "colorpicker": {
      const rgb = (node.value ?? 0x6750a4).toString(16).padStart(6, "0");
      return (
        <div className="ctl-colorpicker" style={{ background: `#${rgb}` }} title={`#${rgb}`}>
          <span className="ctl-colorpicker-check">✓</span>
        </div>
      );
    }

    case "browser":
      return (
        <div className="ctl-browser">
          <span className="ctl-browser-url">{node.src || "about:blank"}</span>
        </div>
      );

    case "badge":
      return <div className="ctl-badge">{node.text || ""}</div>;

    case "tag":
      return <div className="ctl-tag">{node.text || ""}</div>;

    case "rate": {
      const stars = Math.max(1, Math.min(10, (node.max ?? 5) - (node.min ?? 0)));
      const val = Math.max(0, Math.min(stars, (node.value ?? 0) - (node.min ?? 0)));
      return (
        <div className="ctl-rate">
          {"★".repeat(val)}
          <span className="ctl-rate-off">{"☆".repeat(stars - val)}</span>
        </div>
      );
    }

    case "pagination": {
      const cur = (node.value ?? 0) - (node.min ?? 0) + 1;
      const total = Math.max(1, (node.max ?? 9) - (node.min ?? 0) + 1);
      return (
        <div className="ctl-pagination">
          <span className="ctl-pagination-btn">{cur > 1 ? "<" : ""}</span>
          <span className="ctl-pagination-info">
            {cur} / {total}
          </span>
          <span className="ctl-pagination-btn">{cur < total ? ">" : ""}</span>
        </div>
      );
    }

    case "card":
      return <div className="ctl-card-title">{node.text || ""}</div>;

    case "shape":
      return null;

    case "chart": {
      /* 画布上给一个"一眼认得出是图"的示意：坐标轴 + 网格 + 两条示意曲线。
         真实数据是运行期 cg_chart_push 灌的，这里不假装知道它长什么样。 */
      const two = (node.items?.length ?? 0) > 1;
      return (
        <svg className="chart-body" viewBox="0 0 100 60" preserveAspectRatio="none">
          <line x1="7" y1="6" x2="7" y2="54" className="chart-axis" />
          <line x1="7" y1="54" x2="97" y2="54" className="chart-axis" />
          <line x1="7" y1="18" x2="97" y2="18" className="chart-grid" />
          <line x1="7" y1="30" x2="97" y2="30" className="chart-grid" />
          <line x1="7" y1="42" x2="97" y2="42" className="chart-grid" />
          <line x1="37" y1="6" x2="37" y2="54" className="chart-grid" />
          <line x1="67" y1="6" x2="67" y2="54" className="chart-grid" />
          <polyline className="chart-s0" points="7,50 25,44 43,47 61,26 79,31 97,12" />
          {two ? <polyline className="chart-s1" points="7,34 25,36 43,33 61,35 79,32 97,34" /> : null}
        </svg>
      );
    }

    case "terminal": {
      /* 画布不知道每行的方向（那是运行时灌内容时才知道的），所以只按行首的
       * RX / TX 字样给个提示色，让画布上的观感和跑起来差不多。 */
      const lines = node.items ?? [];
      if (!lines.length) return <div className="term-hint">运行时用 cg_term_write() 灌内容</div>;
      return (
        <div className="term-body">
          {lines.map((t, i) => {
            const cls = t.includes(" TX") ? "term-tx" : t.includes(" RX") ? "term-rx" : "term-sys";
            return (
              <div key={i} className={`term-line ${cls}`}>
                {t}
              </div>
            );
          })}
        </div>
      );
    }

    default:
      return null;
  }
}

type Props = {
  node: CgNode;
  depth: number;
  selectedId: string | null;
  onSelect: (id: string) => void;
  onPointerDown: (e: React.PointerEvent, node: CgNode) => void;
};

export default function WidgetView({ node, depth, selectedId, onSelect, onPointerDown }: Props) {
  const style: CSSProperties = {
    position: "absolute",
    left: node.rect.x,
    top: node.rect.y,
    width: node.rect.w,
    height: node.rect.h,
    ...fontOf(node),
  };
  if (node.style?.bg) style.background = node.style.bg;
  if (node.style?.radius) style.borderRadius = node.style.radius;

  const bound = CG_EVENTS.filter((e) => node.events?.[e]);
  const isGroup = node.type === "group" || node.type === "box" || node.type === "tabs" || node.type === "card";
  /* 分隔条按方向画：format "row" = 上下分行（横条），缺省 "col" = 左右分栏（竖条） */
  const splitterRow = node.type === "splitter" && node.format === "row";

  /* 菜单栏没有矩形：它画在窗口框架上，由 Canvas 负责，这里不画 */
  if (node.type === "menu") return null;

  /* 标签页只画当前页的子控件，和运行时的表现一致 */
  const kids =
    node.type === "tabs"
      ? (node.children ?? []).filter((c) => (c.page ?? 0) === (node.value ?? 0))
      : (node.children ?? []);

  return (
    <div
      className={[
        "widget",
        `widget-${node.type}`,
        splitterRow ? "widget-splitter-row" : "",
        selectedId === node.id ? "selected" : "",
        node.visible === false ? "hidden-widget" : "",
        node.enabled === false ? "disabled-widget" : "",
      ]
        .filter(Boolean)
        .join(" ")}
      style={style}
      onPointerDown={(e) => onPointerDown(e, node)}
      onClick={(e) => {
        e.stopPropagation();
        onSelect(node.id);
      }}
      title={`${node.type} #${node.id}`}
    >
      {inner(node)}

      {isGroup && kids.length ? (
        <div className="group-body">
          {kids.map((c) => (
            <WidgetView
              key={c.id}
              node={c}
              depth={depth + 1}
              selectedId={selectedId}
              onSelect={onSelect}
              onPointerDown={onPointerDown}
            />
          ))}
        </div>
      ) : null}

      {node.type === "tabs" && (node.children?.length ?? 0) > kids.length ? (
        <span className="tabs-more" title="其他页的控件在运行时切页后才显示">
          +{(node.children?.length ?? 0) - kids.length}
        </span>
      ) : null}

      {bound.length > 0 && (
        <span className="event-badge" title={bound.map((b) => `${EVENT_LABEL[b]}: ${node.events?.[b]}`).join("\n")}>
          {bound.length}
        </span>
      )}
    </div>
  );
}
