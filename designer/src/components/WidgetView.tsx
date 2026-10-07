/* WidgetView.tsx -- 把一个 .cgui 节点画成「接近 Win32 原生控件」的样子
 *
 * 画布的职责是让用户在编译前就能看出运行时大概长什么样，
 * 所以这里刻意用平实的原生风格，而不是网页风格。
 */
import type { CSSProperties } from "react";
import { eventLabel, resolveStyle, isNumEdit, CG_EVENTS, type CgDoc, type CgNode, type CgStyle } from "../model";
import { useImage, type Preview } from "../imagecache";

/** 画布渲染**解析后的样式**：命名样式打底、内联覆盖（resolveStyle 与运行时
 *  resolve_named_styles 同一套语义）。控件写了 "style": "log"，画布上就该是 log 的
 *  字体颜色 —— 不然又造一类"画布上看到的不一样"。F6 第一刀（2026-10-07）把命名样式
 *  从"不预览"里摘出来：resolveStyle 本就在模型层（属性面板在用），接线近乎零成本。
 *  **深色仍不预览**（那才是"把整套自绘皮肤在 Web 侧再实现一遍"，契约 §1.1 有意为之）。 */
const fontOf = (st: CgStyle): CSSProperties => {
  const px = st.size ? `${st.size}pt` : "12px";
  return {
    fontFamily: st.font ? `"${st.font}", "Microsoft YaHei UI", sans-serif` : '"Microsoft YaHei UI", sans-serif',
    fontSize: px,
    fontWeight: st.bold ? 700 : 400,
    color: st.fg ?? "#1D1B20",
  };
};

function inner(node: CgNode, img: Preview, st: CgStyle) {
  const value = node.value ?? 40;
  const min = node.min ?? 0;
  const max = node.max ?? 100;
  const pct = max > min ? Math.min(100, Math.max(0, ((value - min) / (max - min)) * 100)) : 0;

  switch (node.type) {
    case "button": {
      const skin: CSSProperties = {};
      if (img) {
        /* 背景图：运行时是 StretchBlt 整幅铺满 + 圆角裁切（cg_skin.c:472-476）。
           CSS 这边 background-size:100% 100% 就是非等比铺满，圆角交给 .ctl-button 自己的
           border-radius 裁（border-radius 会裁自己的背景图，不用再套一层 overflow）。
           有图时不再用 style.bg —— 运行时也是"有图就不走填充那条分支"。 */
        skin.backgroundImage = `url("${img}")`;
        skin.backgroundSize = "100% 100%";
        skin.backgroundRepeat = "no-repeat";
      } else if (st.bg) {
        skin.background = st.bg;
        const r = parseInt(st.bg.slice(1, 3), 16), g2 = parseInt(st.bg.slice(3, 5), 16), b = parseInt(st.bg.slice(5, 7), 16);
        skin.color = (0.2126 * r + 0.7152 * g2 + 0.0722 * b) / 255 < 0.55 ? "#FFFFFF" : "#000000";
      }
      if (st.radius != null) skin.borderRadius = st.radius;
      return (
        <div className="ctl-button" style={skin}>
          {node.text || ""}
          {/* 读到了就直接画在上面（不用再提示是哪张图）；**没读到才提示** ——
              老担心的是"填了 src 却什么都不显示，看起来像填了没用"，现在有真图了，
              那条担心只在"读不到"时才成立，所以提示也只在这时占地方。 */}
          {node.src && !img ? (
            <span className={img === null ? "ctl-imgchip bad" : "ctl-imgchip"} title={node.src}>
              {img === null ? "读不到 " : ""}
              {node.src.split(/[\\/]/).pop()}
            </span>
          ) : null}
        </div>
      );
    }

    case "label": {
      /* 运行时 label 读 style.align 设 SS_LEFT/CENTER/RIGHT（cg_win_ctl.c:227-228），
         预览要一致。.ctl-label 是 flex，靠 justifyContent 才真对齐
         （text-align 落在匿名 flex 项上不生效）。 */
      const al = st.align ?? "left";
      return (
        <div
          className="ctl-label"
          style={{ justifyContent: al === "center" ? "center" : al === "right" ? "flex-end" : "flex-start" }}
        >
          {node.text || ""}
        </div>
      );
    }

    case "edit": {
      /* 多行/对齐按字段画（原生单行框会把换行压成一行，画布照做，免得两边观感不一致）。
         数值框（调节器）显示的是 value —— 运行时那边文本也是从 value 派生的。 */
      const multi = !!node.multiline;
      const num = isNumEdit(node);
      const bits: string[] = [];
      if (node.maxlen) bits.push(`最长 ${node.maxlen} 字`);
      if (node.scroll) bits.push(["", "横向滚动", "纵向滚动", "双向滚动"][node.scroll] ?? "");
      if (node.input) {
        bits.push(
          ["", "只读", "密码", "整数文本", "小数文本", "字节", "短整数", "整数",
           "长整数", "小数", "双精度", "日期时间"][node.input] ?? "",
        );
      }
      if (node.conv) bits.push(node.conv === 1 ? "输入转小写" : "输入转大写");
      if (node.spin) {
        bits.push(
          node.spin === 1
            ? `调节器（自动，夹在 ${node.min ?? 0}~${node.max ?? 100}）`
            : "调节器（手动）",
        );
      }
      if (num) bits.push(`数值框，值是 ${node.value ?? 0}`);
      const shown = num ? String(node.value ?? 0) : node.text || "";
      return (
        <div
          className={`ctl-edit${multi ? " ctl-edit-multi" : ""}${
            node.border ? ` ctl-edit-b${node.border}` : ""
          }`}
          style={{ textAlign: st.align ?? "left", position: "relative" }}
          title={bits.length ? `输入框：${bits.join(" · ")}` : undefined}
        >
          {node.spin ? (
            <span className="ctl-spinchip" title="运行时这里有一列上下箭头">
              ⇅
            </span>
          ) : null}
          {multi ? shown : shown.replace(/\n/g, " ")}
        </div>
      );
    }

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

    case "combo": {
      /* 运行时 combo 是 CBS_DROPDOWNLIST：显示的是 items 里的当前选中项，压根不读 w->text
         （白名单 cg_win_sync.c:520-524 不含 combo；选中项在 :574-586，越界退回第 0 项）。 */
      const items = node.items ?? [];
      const sel = node.value && node.value < items.length ? node.value : 0;
      return (
        <div className="ctl-combo">
          <span>{items[sel] ?? ""}</span>
          <span className="ctl-arrow">▾</span>
        </div>
      );
    }

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
      /* 运行时是 StretchBlt 铺满整个控件矩形、非等比（cg_win_draw_canvas.c:144-147）。
         占位态照旧是虚线框 + 文件名（和运行时的占位框一个意思），但多写"读不到"三个字：
         运行时只画文件名，看不出是路径写错了、还是图本来就长这样。 */
      if (img) {
        return (
          <div className="ctl-image loaded">
            <img className="ctl-image-img" src={img} alt="" draggable={false} />
          </div>
        );
      }
      return (
        <div className="ctl-image">
          {!node.src
            ? "未设置图片"
            : img === null
              ? `读不到图片：${node.src.split(/[\\/]/).pop()}`
              : node.src.split(/[\\/]/).pop()}
        </div>
      );

    case "switch":
      return (
        <div className="ctl-switch">
          {node.text ? <span className="ctl-switch-label">{node.text}</span> : null}
          <span className={`ctl-switch-track${node.checked ? " on" : ""}`}>
            <span className="ctl-switch-knob" />
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
      /* 运行时：等比缩放、居中、**只缩不放**（小图保持原始像素），四周各让 4px
         （cg_win_ownerdraw.c:33-54）；图加载成功时不再画文字（文字只属于"缺图占位"那一支）。 */
      return (
        <div className="ctl-iconbtn">
          {img ? <img className="ctl-iconbtn-img" src={img} alt="" draggable={false} /> : node.text || "图标"}
          {/* 同上：读到了就不提示，没读到才在角落说一声是哪张图、为什么没显示 */}
          {node.src && !img ? (
            <span className={img === null ? "ctl-imgchip bad" : "ctl-imgchip"} title={node.src}>
              {img === null ? "读不到 " : ""}
              {node.src.split(/[\\/]/).pop()}
            </span>
          ) : null}
        </div>
      );

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

    case "serial":
      return (
        <div className="ctl-serial" title={node.port ?? "COM1"}>
          <span className="glyph-serial" />
          <span className="ctl-serial-port">{node.port ?? "COM1"}</span>
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

    case "led": {
      /* 灯只有三态，画布上直接照着画：0 灭（灰）/ 1 亮（style.fg 或缺省绿）/ 2 闪。
         闪的可视化就用"亮色 + 虚线边框"，不真做动画 —— 编辑时画面一直晃太干扰。 */
      const state = Math.max(0, Math.min(2, Math.round(node.value ?? 0)));
      const color = state === 0 ? "#CAC4D0" : st.fg || "#1B6E33";
      const lamp = (
        <span
          className={`led-lamp ${node.format === "square" ? "led-square" : "led-round"}`}
          style={{
            background: color,
            borderColor: state === 2 ? "#1D1B20" : color,
            borderStyle: state === 2 ? "dashed" : "solid",
          }}
        />
      );
      /* text 非空 = 灯在左（固定成方形）、文字在右；text 为空 = 灯自己占满整个矩形 */
      return node.text ? (
        <div className="ctl-led">
          {lamp}
          {/* 说明文字固定用默认字色：led 的 style.fg 在这控件上含义是"灯色"，不是文字色 */}
          <span className="led-text" style={{ color: "#1D1B20" }}>{node.text}</span>
        </div>
      ) : (
        <div className="ctl-led led-only">{lamp}</div>
      );
    }

    case "meter": {
      /* 仪表盘示意：底弧 + 按 value 比例的彩色值弧 + 指针 + 中心两行文字。
         format "arc"（缺省）= 270° 缺口朝下，"half" = 半圆。真值由 cg_meter_set 写，
         画布不假装知道实时数据。 */
      const lo = node.min ?? 0;
      const hi = node.max ?? 100;
      const span = hi - lo || 1;
      const ratio = Math.max(0, Math.min(1, ((node.value ?? 0) - lo) / span));
      const half = node.format === "half";
      const cx = 50;
      const cy = 54;
      const r = 40;
      const startDeg = half ? 180 : 135;
      const sweepDeg = half ? 180 : 270;
      const color = st.fg || "#6750A4";
      /* 角度按"y 轴向下"的屏幕坐标算：0° = 右，90° = 下，所以角度增大 = 顺时针 */
      const pt = (deg: number, radius: number) => {
        const a = (deg * Math.PI) / 180;
        return { x: cx + radius * Math.cos(a), y: cy + radius * Math.sin(a) };
      };
      const arcPath = (from: number, sweep: number) => {
        if (sweep <= 0.01) return "";
        const a = pt(from, r);
        const b = pt(from + sweep, r);
        return `M ${a.x.toFixed(2)} ${a.y.toFixed(2)} A ${r} ${r} 0 ${sweep > 180 ? 1 : 0} 1 ${b.x.toFixed(2)} ${b.y.toFixed(2)}`;
      };
      const tip = pt(startDeg + sweepDeg * ratio, r - 14);
      return (
        <svg className="meter-body" viewBox="0 0 100 100" preserveAspectRatio="xMidYMid meet">
          <path className="meter-track" d={arcPath(startDeg, sweepDeg)} />
          <path className="meter-value" d={arcPath(startDeg, sweepDeg * ratio)} style={{ stroke: color }} />
          <line className="meter-needle" x1={cx} y1={cy} x2={tip.x.toFixed(2)} y2={tip.y.toFixed(2)} style={{ stroke: color }} />
          <circle className="meter-hub" cx={cx} cy={cy} r="2.6" style={{ fill: color }} />
          <text className="meter-num" x={cx} y={half ? 84 : 76} textAnchor="middle">
            {node.value ?? 0}
          </text>
          <text className="meter-unit" x={cx} y={half ? 94 : 88} textAnchor="middle">
            {node.text || ""}
          </text>
        </svg>
      );
    }

    default:
      return null;
  }
}

type Props = {
  /** 解析命名样式要用整份文档（styles{} 在顶层）—— F6 起画布按解析后的样式渲染 */
  doc: CgDoc;
  node: CgNode;
  depth: number;
  /** 当前选中集（主选框也在里面）：这里只回答「选没选中」 */
  selected: ReadonlySet<string>;
  /** 主选框 id（最后点的那个）：实线外框；其余选中项走虚线，主次一眼可分 */
  primaryId: string | null;
  onPointerDown: (e: React.PointerEvent, node: CgNode) => void;
};

export default function WidgetView({ doc, node, depth, selected, primaryId, onPointerDown }: Props) {
  /* 预览图必须在**任何提前 return 之前**取：下面就有 `if (node.type === "menu") return null;`，
     hook 放到它后面就成了条件调用 —— 而控件类型是能在「属性」面板里改的，一改就"Hooks 数量不一致"崩溃
     （和 Inspector 里那条注释记的是同一个坑）。 */
  const img = useImage(node.src);
  /* 解析一次，整棵渲染共用：命名样式打底、内联覆盖（引用了不存在的名字 → effective
     就是内联自己，与运行时"当没有 use"同一口径）。 */
  const st = resolveStyle(doc, node.style).effective;
  const style: CSSProperties = {
    position: "absolute",
    left: node.rect.x,
    top: node.rect.y,
    width: node.rect.w,
    height: node.rect.h,
    ...fontOf(st),
  };
  if (st.bg) style.background = st.bg;
  /* 外形框（shape）的圆角由 format 决定，不走通用的 style.radius（见下面的 shape 分支） */
  if (st.radius && node.type !== "shape") style.borderRadius = st.radius;

  /* 外形框预览对齐运行时的三形态与配色（cgw_PanelProc 的 WM_PAINT，见 cg_win_draw_panel.c）：
     rect=直角 / roundrect=圆角（缺省 8px，可被 style.radius 覆盖）/ ellipse=椭圆（忽略 radius）。
     描边色取 style.fg（缺省 #b4b0bb）。是否填充只由上面的 style.bg 决定 —— 不设就是空心描边，
     这是有意的：画布上"看得见才选得中"，实心填色会把窗口遮死、也没法点中它。 */
  if (node.type === "shape") {
    const fmt = node.format ?? "roundrect";
    style.borderColor = st.fg ?? "#b4b0bb";
    if (fmt === "roundrect" && st.radius) style.borderRadius = st.radius;
  }

  const bound = CG_EVENTS.filter((e) => node.events?.[e]);
  const isGroup = node.type === "group" || node.type === "box" || node.type === "tabs" || node.type === "card" || node.type === "dialog";
  /* 分隔条按方向画：format "row" = 上下分行（横条），缺省 "col" = 左右分栏（竖条） */
  const splitterRow = node.type === "splitter" && node.format === "row";

  /* 菜单栏没有矩形：它画在窗口框架上，由 Canvas 负责，这里不画 */
  if (node.type === "menu") return null;

  /* 标签页只画当前页的子控件，和运行时的表现一致 */
  const kids =
    node.type === "tabs"
      ? (node.children ?? []).filter((c) => (c.page ?? 0) === (node.value ?? 0))
      : (node.children ?? []);

  /* visible:false 的控件在画布上用 35% 幽灵显示（.widget.hidden-widget）而不是真隐藏 ——
     有意为之：隐藏的控件必须仍可见可选中，否则用户没法把它改回来。别改成 display:none、
     也别改成运行时那种完全不可见。 */
  return (
    <div
      className={[
        "widget",
        `widget-${node.type}`,
        node.type === "shape" ? `shape-${node.format ?? "roundrect"}` : "",
        splitterRow ? "widget-splitter-row" : "",
        primaryId === node.id ? "selected" : selected.has(node.id) ? "selected-2" : "",
        node.visible === false ? "hidden-widget" : "",
        node.enabled === false ? "disabled-widget" : "",
      ]
        .filter(Boolean)
        .join(" ")}
      style={style}
      onPointerDown={(e) => onPointerDown(e, node)}
      title={`${node.type} #${node.id}`}
    >
      {inner(node, img, st)}

      {isGroup && kids.length ? (
        <div className="group-body">
          {kids.map((c) => (
            <WidgetView
              key={c.id}
              doc={doc}
              node={c}
              depth={depth + 1}
              selected={selected}
              primaryId={primaryId}
              onPointerDown={onPointerDown}
            />
          ))}
        </div>
      ) : null}

      {node.type === "dialog" ? (
        /* 多窗体的画布提示：这是个独立窗口，运行时模态打开。角上一枚小徽章 ——
           不用整块遮罩：画布还要能选中、编辑它的子控件。 */
        <span className="dlg-badge">模态</span>
      ) : null}

      {node.type === "tabs" && (node.children?.length ?? 0) > kids.length ? (
        <span className="tabs-more" title="其他页的控件在运行时切页后才显示">
          +{(node.children?.length ?? 0) - kids.length}
        </span>
      ) : null}

      {bound.length > 0 && (
        <span className="event-badge" title={bound.map((b) => `${eventLabel(node.type, b)}: ${node.events?.[b]}`).join("\n")}>
          {bound.length}
        </span>
      )}
    </div>
  );
}
