/* Inspector/StyleSection.tsx -- 「样式 / 字体与颜色」整段（style.* 字段族 + 命名样式引用）
 *
 * 字体（含展开的字体选择器）、字号与粗体、文字色 / 背景色（含「透明」开关）、
 * 对齐、圆角。背景那一块按控件类型分两种画法：标签/链接/分割线默认透明。
 * 字体选择器的展开状态留在 Inspector.tsx，用 fontOpen + onToggleFont 传进来。
 *
 * 命名样式（契约 §2）：本段第一行是「命名样式」下拉 —— 选一条 styles{} 里的样式，
 * 下面的字段就是**本控件的局部覆盖**（写了的盖过样式，没写的继承）。
 * 改任何一个字段都**保留 use**：三种形态（纯命名 / use+覆盖 / 内联）由 normalizeStyle 收敛。
 * 画布不做样式实时预览（契约 §4）：这里只显示字段值；画布上控件外观只吃内联字段
 * （见 WidgetView.tsx 的 inlineStyle）。
 */
import {
  STYLE_KEYS,
  inlineStyle,
  normalizeStyle,
  resolveStyle,
  type CgAlign,
  type CgDoc,
  type CgNode,
  type CgStyle,
  type CgStyleKey,
  type CgStyleRef,
} from "../../model";
import { Row, type Patch } from "./PanelUi";
import { roleColor } from "../WidgetView";

/** 字体选择器候选： Windows 常见字体，label 用中文名好认，value 写进 .cgui */
const FONT_CHOICES: { label: string; value: string }[] = [
  { label: "微软雅黑", value: "Microsoft YaHei UI" },
  { label: "宋体", value: "SimSun" },
  { label: "黑体", value: "SimHei" },
  { label: "楷体", value: "KaiTi" },
  { label: "仿宋", value: "FangSong" },
  { label: "等线", value: "DengXian" },
  { label: "Segoe UI", value: "Segoe UI" },
  { label: "Arial", value: "Arial" },
  { label: "Calibri", value: "Calibri" },
  { label: "Consolas", value: "Consolas" },
  { label: "Times New Roman", value: "Times New Roman" },
  { label: "Courier New", value: "Courier New" },
];
/** 大小候选（磅）；运行时 style.size 缺省跟随系统 UI 字体 */
const FONT_SIZES = [8, 9, 10, 11, 12, 14, 16, 18, 20, 24, 28, 32, 36];

/** 自绘配色写死 M3、运行时不读 style.fg / style.bg 的类型
 *  （cg_skin.c:414,418,447-451,538,542,560-565,504）：画布上给颜色两行加提示，免得白填。 */
const FIXED_M3_COLOR = new Set(["switch", "checkbox", "radio"]);

/** 字段名 -> 中文（「覆盖了哪几个字段」要说人话） */
const FIELD_LABEL: Record<CgStyleKey, string> = {
  font: "字体",
  size: "字号",
  bold: "粗体",
  fg: "文字色",
  bg: "背景色",
  align: "对齐",
  radius: "圆角",
};

/** 下拉里一条样式的写法：名字 + 它主要设了什么（不说出来就不知道引用的是哪一条） */
function styleSummary(s: CgStyle | undefined): string {
  if (!s) return "";
  const bits: string[] = [];
  if (s.font) bits.push(s.font);
  if (s.size) bits.push(`${s.size}pt`);
  if (s.bold) bits.push("粗体");
  if (s.fg) bits.push(s.fg);
  if (s.bg) bits.push(s.bg);
  return bits.length ? ` — ${bits.join(" · ")}` : "（空样式）";
}

/** 「这个值来自命名样式」的小标记（颜色 / 下拉 / 勾选框没法用 placeholder 表达继承） */
function SourceTag({ fromStyle }: { fromStyle: boolean }) {
  if (!fromStyle) return null;
  return (
    <span className="sty-tag" title="这个值来自命名样式（本控件没写，是继承下来的）">
      样式
    </span>
  );
}

export type StyleSectionProps = {
  doc: CgDoc;
  node: CgNode;
  patch: Patch;
  /** 字体选择器是否展开（状态在 Inspector.tsx） */
  fontOpen: boolean;
  onToggleFont: () => void;
};

export default function StyleSection({ doc, node, patch, fontOpen, onToggleFont }: StyleSectionProps) {
  /** 本控件自己写了的字段（不含 use） */
  const own = inlineStyle(node.style);
  const resolved = resolveStyle(doc, node.style);
  const eff = resolved.effective;
  /** 引用的命名样式（名字存在时）；失效引用就是 undefined */
  const named = !resolved.missing && resolved.use ? doc.styles?.[resolved.use] : undefined;
  const styleNames = Object.keys(doc.styles ?? {});
  /* 标签 / 链接 / 分割线默认不画底色（透明）；这里给个显式开关，用户一眼能看清 */
  const transparentBg = node.type === "label" || node.type === "link" || node.type === "separator";

  /** 改 style：保留命名引用，改完按三种形态收敛（只有引用 -> 字符串；只有字段 -> 对象） */
  const editStyle = (fn: (s: CgStyleRef) => void) =>
    patch((n) => {
      const base: CgStyleRef = {};
      if (typeof n.style === "string") base.use = n.style;
      else if (n.style) {
        const src = n.style as unknown as Record<string, unknown>;
        const dst = base as unknown as Record<string, unknown>;
        for (const k of STYLE_KEYS) if (src[k] !== undefined) dst[k] = src[k];
      }
      fn(base);
      n.style = normalizeStyle(base);
    });

  const setField = <K extends CgStyleKey>(key: K, value: CgStyle[K]) =>
    editStyle((s) => {
      (s as unknown as Record<string, unknown>)[key] = value;
    });
  const clearField = (key: CgStyleKey) =>
    editStyle((s) => {
      delete (s as unknown as Record<string, unknown>)[key];
    });
  const setUse = (name: string | undefined) =>
    editStyle((s) => {
      if (name) s.use = name;
      else delete s.use;
    });
  /** 粗体：勾上 = true（覆盖）；取消时若样式里是粗体，要写 false 显式盖掉，否则只是删掉自己的字段 */
  const setBold = (on: boolean) => {
    if (on) setField("bold", true);
    else if (named?.bold === true) setField("bold", false);
    else clearField("bold");
  };
  /** 对齐：左对齐是缺省；只有"样式里不是左对齐"时才需要显式写 left 去盖它 */
  const setAlign = (v: CgAlign) => {
    if (v === "left" && named?.align !== "center" && named?.align !== "right") clearField("align");
    else setField("align", v);
  };

  /* 失效引用：运行时当"没有 use"，不报错、不中断（契约 §2），画布必须当场标出来 */
  const stale = resolved.use && resolved.missing ? resolved.use : "";
  const overrides = resolved.overrides.map((k) => FIELD_LABEL[k]);

  return (
    <>
      <div className="insp-section">样式</div>
      <Row
        label="命名样式"
        hint="引用一条命名样式（工具条「样式」里新建）；下面字段里写了的会盖过它。选「（无）」= 只用自己的字段"
      >
        <select className="input" value={resolved.use} onChange={(e) => setUse(e.target.value || undefined)}>
          <option value="">（无）</option>
          {styleNames.map((n) => (
            <option key={n} value={n}>
              {n}
              {styleSummary(doc.styles?.[n])}
            </option>
          ))}
        </select>
      </Row>

      {stale ? (
        <div className="hint-box warn-box">
          <div className="hint-title">引用了不存在的样式「{stale}」</div>
          <p className="muted small">
            运行时把 <code>use</code> 当成"没写"，只用本控件自己的字段（不报错、不中断）。
            多半是这个样式被删掉了，或者 .cgui 被手改过 —— 在「样式」对话框里改名会连带改引用，
            手改文件里的名字不会。想清掉就在上面选「（无）」或点这里。
          </p>
          <button className="mini" onClick={() => setUse(undefined)}>
            清掉这个引用
          </button>
        </div>
      ) : null}

      {resolved.use && !stale ? (
        <p className="muted small sty-note">
          当前用样式「{resolved.use}」{styleSummary(named)}。
          {overrides.length ? (
            <>
              本控件覆盖了 <strong>{overrides.join("、")}</strong>。
            </>
          ) : (
            "没有就地覆盖，全部来自样式。"
          )}
        </p>
      ) : null}
      {!resolved.use && styleNames.length === 0 ? (
        <p className="muted small sty-note">还没有命名样式：工具条上的「样式」按钮里可以新建。</p>
      ) : null}

      <div className="insp-section">字体与颜色</div>
      <Row label="字体" hint={own.font === undefined && named?.font ? `来自样式「${resolved.use}」` : undefined}>
        <input
          className="input"
          value={own.font ?? ""}
          placeholder={named?.font ? `${named.font}（样式）` : "Microsoft YaHei UI"}
          onChange={(e) => {
            const v = e.target.value;
            if (v.trim()) setField("font", v);
            else clearField("font");
          }}
        />
        <button className="mini" title="展开选择字体" onClick={onToggleFont}>
          Aa
        </button>
      </Row>
      {fontOpen ? (
        <div className="font-picker">
          <div className="fp-cols">
            <div className="fp-col fp-grow">
              <div className="fp-head">字体（字体族）</div>
              <div className="fp-list">
                {FONT_CHOICES.map((f) => (
                  <button
                    key={f.value}
                    className={`fp-item${(eff.font ?? "") === f.value ? " on" : ""}`}
                    onClick={() => setField("font", f.value)}
                  >
                    <span className="fp-name">{f.label}</span>
                    <span className="fp-sample" style={{ fontFamily: `'${f.value}'` }}>
                      示例 Aa 123
                    </span>
                  </button>
                ))}
              </div>
            </div>
            <div className="fp-col">
              <div className="fp-head">大小（磅）</div>
              <div className="fp-list fp-sizes">
                {FONT_SIZES.map((s) => (
                  <button
                    key={s}
                    className={`fp-item${(eff.size ?? 10) === s ? " on" : ""}`}
                    onClick={() => setField("size", s)}
                  >
                    {s}
                  </button>
                ))}
              </div>
            </div>
            <div className="fp-col">
              <div className="fp-head">字形</div>
              <div className="fp-list fp-sizes">
                <button className={`fp-item${!eff.bold ? " on" : ""}`} onClick={() => setBold(false)}>
                  常规
                </button>
                <button className={`fp-item${eff.bold ? " on" : ""}`} onClick={() => setBold(true)}>
                  粗体
                </button>
              </div>
            </div>
          </div>
          <div
            className="fp-preview"
            style={{
              fontFamily: `'${eff.font || "Microsoft YaHei UI"}', sans-serif`,
              fontSize: `${Math.max(12, (eff.size ?? 10) * 1.5)}px`,
              fontWeight: eff.bold ? 700 : 400,
            }}
          >
            示例：按钮 Button 123
          </div>
          <p className="muted small">
            点一下立即生效；字号 0 表示跟随系统 UI 字体。选择器只把值填进字段，画布不预览命名样式（契约 §4）。
          </p>
        </div>
      ) : null}
      <Row
        label="字号"
        hint={
          own.size === undefined && named?.size !== undefined
            ? `来自样式「${resolved.use}」：${named.size}`
            : "0 或留空 = 跟随系统 UI 字体"
        }
      >
        <input
          className="input"
          type="number"
          value={own.size ?? ""}
          placeholder={named?.size !== undefined ? String(named.size) : "10"}
          onChange={(e) => {
            const v = e.target.value;
            if (v === "") clearField("size");
            else setField("size", Math.round(Number(v) || 0));
          }}
        />
        <label className="check-inline">
          <input type="checkbox" checked={eff.bold === true} onChange={(e) => setBold(e.target.checked)} />
          粗体
        </label>
        <SourceTag fromStyle={own.bold === undefined && named?.bold !== undefined} />
      </Row>
      <Row
        label={node.type === "led" ? "亮灯颜色" : node.type === "meter" ? "主色" : "文字色"}
        hint={
          FIXED_M3_COLOR.has(node.type)
            ? "开关 / 复选框 / 单选钮 的配色由控件自绘固定（M3），运行时不读 style.fg，改了不生效"
            : own.fg === undefined && named?.fg
              ? `来自样式「${resolved.use}」`
              : undefined
        }
      >
        {/* 角色下拉（深色皮肤 G1/T3）：选角色 = fg 写成 "@名"，颜色跟随主题深浅；
            选「字面色」回到取色器。角色/槽缺失时画布与运行时都按没写处理。 */}
        <select
          className="input narrow"
          value={own.fg?.startsWith("@") ? own.fg : ""}
          title="选「字面色」用旁边的取色器；选角色则颜色跟随主题深浅"
          onChange={(e) => (e.target.value ? setField("fg", e.target.value) : clearField("fg"))}
        >
          <option value="">字面色</option>
          {Object.keys(doc.roles ?? {}).map((r) => (
            <option key={r} value={`@${r}`}>
              @{r}
            </option>
          ))}
        </select>
        <input
          className="input color"
          type="color"
          disabled={!!own.fg?.startsWith("@")}
          value={roleColor(doc, eff.fg) ?? (node.type === "led" ? "#1b6e33" : node.type === "meter" ? "#6750a4" : "#1d1b20")}
          onChange={(e) => setField("fg", e.target.value)}
        />
        <button className="mini" title="清掉本控件写的文字色（回到命名样式 / 缺省）" onClick={() => clearField("fg")}>
          {node.type === "led" ? "恢复缺省绿" : "跟随系统"}
        </button>
        <SourceTag fromStyle={own.fg === undefined && !!named?.fg} />
      </Row>
      {transparentBg ? (
        <>
          <Row
            label="背景"
            hint={
              named?.bg
                ? "命名样式里有背景色：勾「透明」只清掉本控件的背景，样式里的背景仍然生效"
                : undefined
            }
          >
            <label className="check-inline">
              <input type="checkbox" checked={eff.bg === undefined} onChange={(e) => (e.target.checked ? clearField("bg") : setField("bg", "#ffffff"))} />
              透明（不画底色）
            </label>
          </Row>
          {eff.bg ? (
            <Row label="背景色" hint={own.bg === undefined && named?.bg ? `来自样式「${resolved.use}」` : undefined}>
              <input className="input color" type="color" value={eff.bg} onChange={(e) => setField("bg", e.target.value)} />
              <SourceTag fromStyle={own.bg === undefined && !!named?.bg} />
            </Row>
          ) : null}
        </>
      ) : (
        <Row
          label="背景色"
          hint={
            FIXED_M3_COLOR.has(node.type)
              ? "开关 / 复选框 / 单选钮 的底色由控件自绘固定（M3），运行时不读 style.bg，改了不生效"
              : own.bg === undefined && named?.bg
                ? `来自样式「${resolved.use}」`
                : undefined
          }
        >
          <input
            className="input color"
            type="color"
            value={eff.bg ?? "#eadcff"}
            onChange={(e) => setField("bg", e.target.value)}
          />
          <button className="mini" title="清掉本控件写的背景色（回到命名样式 / 缺省）" onClick={() => clearField("bg")}>
            跟随系统
          </button>
          <SourceTag fromStyle={own.bg === undefined && !!named?.bg} />
        </Row>
      )}
      <Row
        label="对齐"
        hint={
          own.align === undefined && named?.align
            ? `来自样式「${resolved.use}」`
            : "只对「标签」和「输入框」生效（运行时按这个字段给控件设对齐样式）；其它控件的文字对齐由控件自身决定，改了不生效"
        }
      >
        <select className="input" value={eff.align ?? "left"} onChange={(e) => setAlign(e.target.value as CgAlign)}>
          <option value="left">左对齐</option>
          <option value="center">居中</option>
          <option value="right">右对齐</option>
        </select>
        <SourceTag fromStyle={own.align === undefined && named?.align !== undefined} />
      </Row>
      <Row
        label="圆角"
        hint={
          own.radius === undefined && named?.radius !== undefined
            ? `来自样式「${resolved.use}」：${named.radius}`
            : "自绘控件的圆角半径（像素）：当前由运行时消费的只有 按钮 / 复选框 / 单选钮 / 输入框 / 卡片 / 外形框；开关、列表、表格等改了不生效。填 0 或清空 = 用控件缺省"
        }
      >
        <input
          className="input narrow"
          type="number"
          min={0}
          value={own.radius ?? ""}
          placeholder={named?.radius !== undefined ? String(named.radius) : "0"}
          onChange={(e) => {
            const v = e.target.value;
            if (v === "") clearField("radius");
            else setField("radius", Math.round(Number(v) || 0));
          }}
        />
        <span className="muted small">px，留空 = 缺省</span>
        <SourceTag fromStyle={own.radius === undefined && named?.radius !== undefined} />
      </Row>
    </>
  );
}
