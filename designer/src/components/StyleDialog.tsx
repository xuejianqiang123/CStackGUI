/* StyleDialog.tsx -- 「样式」对话框（工具条「样式」按钮）
 *
 * 管理 .cgui 顶层的 styles{}：一条命名样式 = 一组字体/颜色字段（与控件 style 同一套字段），
 * 控件用三种形态引用它（契约 §2）：内联对象 / `"style": "名字"` / `{ "use": "名字", ...覆盖 }`。
 * 于是「同一套字号/配色」只写一遍，改一处就全变。
 *
 * 三件要在界面上说清楚的事：
 *   1. **改名会连带更新引用**（和「属性」面板改控件 id 连带改 bind.src 同一个做法）：
 *      改名时把每个控件的 style 一起改到新名，所以上面那句"改一处全变"才是真的。
 *      手改 .cgui 里的名字才会留下悬空引用 —— 那种情况运行时当"没有命名样式"处理
 *      （**不报错**，只是外观悄悄按没有样式来），靠「属性」面板里的标红自查。
 *   2. 一条样式**一个字段都没有**也能存在（别的控件还引用着它）；这种空样式本身不改变外观，
 *      只是给引用点一个"以后往这里填"的位置。
 *   3. 卡片上写「被 N 处引用」：改名/删除之前先让人看见影响面。
 *
 * 名字（= styles 的键）是唯一会被「删掉再建」的字段，所以它单独走「失焦才提交」，
 * 并且空名字/重名**当场标红**（不必等失焦）。
 *
 * 「复制」= py880 那句"复制主题做自己的配色"的最小落地：以这条样式为初值另建一条，
 * 新的随便改，旧的原样不动。
 */
import { useState } from "react";
import { countRefs, isSafeStyleName, renameStyleRefs, type CgDoc, type CgStyle } from "../model";
import { Row } from "./Inspector/PanelUi";

type Props = {
  open: boolean;
  doc: CgDoc;
  onClose: () => void;
  onBeginHistory: () => void;
  onApply: (mutator: (doc: CgDoc) => void) => void;
};

/** 对齐可选值（与运行时的 cg_align_t 一致；左对齐全工程默认，不写出去） */
const ALIGN_LABEL: { value: NonNullable<CgStyle["align"]>; label: string }[] = [
  { value: "left", label: "左（缺省）" },
  { value: "center", label: "居中" },
  { value: "right", label: "右" },
];

function nextStyleName(styles: Record<string, CgStyle>): string {
  let i = 1;
  while (styles[`style_${i}`]) i += 1;
  return `style_${i}`;
}

type CardProps = {
  name: string;
  style: CgStyle;
  /** 全部样式名（判重名用；自己也在里面，同名会走「没改」分支提前返回） */
  taken: Set<string>;
  /** 有多少个控件引用着这条样式（0 = 没人引用，改名或删除都不影响任何控件） */
  refCount: number;
  onRename: (to: string) => void;
  onPatch: (patch: Partial<CgStyle>) => void;
  onRemove: () => void;
  onDuplicate: () => void;
};

/** 一条样式 = 一张卡片。字段每改一下就落盘（和「动作」「程序配置」同一套手感），
 *  只有样式名是失焦/回车才提交。 */
function StyleCard({ name, style, taken, refCount, onRename, onPatch, onRemove, onDuplicate }: CardProps) {
  const [draft, setDraft] = useState(name);

  /* 空名字 / 重名当场标红（不必等失焦，契约要求"空名字当场标红"） */
  const next = draft.trim();
  const nameError = !isSafeStyleName(next)
    ? "样式名不能为空"
    : next !== name && taken.has(next)
      ? `已经有叫「${next}」的样式了`
      : "";

  const commitName = () => {
    if (nameError || next === name) return;
    onRename(next);
  };

  return (
    <div className="act-card">
      <div className="act-card-head">
        <input
          className={nameError ? "input mono act-name bad" : "input mono act-name"}
          value={draft}
          spellCheck={false}
          title="样式名：.cgui 顶层 styles 的键，控件用 style 引用它（纯名字 / use + 覆盖）"
          onChange={(e) => setDraft(e.target.value)}
          onBlur={commitName}
          onKeyDown={(e) => {
            if (e.key === "Enter") e.currentTarget.blur();
          }}
        />
        <span
          className="act-refs muted small"
          title={
            refCount > 0
              ? `${refCount} 个控件用 style 引用着它；改名会连带把它们一起改到新名字`
              : "没有控件引用它，改名或删除都不影响任何控件"
          }
        >
          {refCount > 0 ? `${refCount} 处引用` : "未被引用"}
        </span>
        <button className="mini" onClick={onDuplicate} title="以这条样式为初值另建一条：新的随便改，旧的原样不动">
          复制
        </button>
        <button
          className="mini"
          onClick={onRemove}
          title={
            refCount > 0
              ? `删除这个样式：还有 ${refCount} 个控件引用它，删了它们会变成引用不存在的样式`
              : "删除这个样式（没有控件引用它）"
          }
        >
          删除
        </button>
      </div>
      {nameError ? <p className="act-note warn">{nameError}</p> : null}

      <Row label="字体" hint="字体族名，如 Microsoft YaHei UI；留空 = 跟随系统 UI 字体">
        <input
          className="input"
          value={style.font ?? ""}
          placeholder="Microsoft YaHei UI"
          onChange={(e) => onPatch({ font: e.target.value.trim() || undefined })}
        />
      </Row>

      <Row label="字号" hint="磅；0 或留空 = 跟随系统 UI 字体">
        <input
          className="input narrow"
          type="number"
          value={style.size ?? 0}
          onChange={(e) => onPatch({ size: Math.round(Number(e.target.value) || 0) })}
        />
        <label className="check-inline">
          <input type="checkbox" checked={style.bold === true} onChange={(e) => onPatch({ bold: e.target.checked })} />
          粗体
        </label>
      </Row>

      <Row label="文字色" hint="前景色；留空 = 跟随系统（或运行时缺省色）">
        <input
          className="input color"
          type="color"
          value={style.fg ?? "#1d1b20"}
          onChange={(e) => onPatch({ fg: e.target.value })}
        />
        <button className="mini" onClick={() => onPatch({ fg: undefined })}>
          清除
        </button>
      </Row>

      <Row label="背景色" hint="底色；留空 = 透明 / 跟随系统（不透明白色请显式选色）">
        <input
          className="input color"
          type="color"
          value={style.bg ?? "#ffffff"}
          onChange={(e) => onPatch({ bg: e.target.value })}
        />
        <button className="mini" onClick={() => onPatch({ bg: undefined })}>
          清除
        </button>
      </Row>

      <Row label="对齐" hint="只对「标签」和「输入框」生效；其它控件的文字对齐由控件自身决定">
        <select
          className="input"
          value={style.align ?? "left"}
          onChange={(e) => onPatch({ align: e.target.value as CgStyle["align"] })}
        >
          {ALIGN_LABEL.map((a) => (
            <option key={a.value} value={a.value}>
              {a.label}
            </option>
          ))}
        </select>
      </Row>

      <Row label="圆角" hint="自绘控件的圆角半径（像素）；0 = 用控件缺省">
        <input
          className="input narrow"
          type="number"
          min={0}
          value={style.radius ?? 0}
          onChange={(e) => onPatch({ radius: Math.round(Number(e.target.value) || 0) })}
        />
        <span className="muted small">px，0 = 缺省</span>
      </Row>
    </div>
  );
}

export default function StyleDialog({ open, doc, onClose, onBeginHistory, onApply }: Props) {
  if (!open) return null;

  const styles = doc.styles ?? {};
  const names = Object.keys(styles);
  const taken = new Set(names);
  /* 每条样式被几个控件引用（一次遍历同时算完两张表，见 model/refs.ts） */
  const styleRefs = countRefs(doc).styles;
  const refCountOf = (name: string) => styleRefs.get(name) ?? 0;

  const patchStyle = (name: string, patch: Partial<CgStyle>) => {
    onBeginHistory();
    onApply((d) => {
      const s = d.styles?.[name];
      if (!s) return;
      const bag = s as unknown as Record<string, unknown>;
      for (const k of Object.keys(patch)) {
        const v = (patch as Record<string, unknown>)[k];
        if (v === undefined) delete bag[k]; /* 清除 = 删掉该字段，不写进文件 */
        else bag[k] = v;
      }
    });
  };

  /** 改名：**同一次 onApply 里**把引用也改掉 —— 这样"改名"在撤销栈里是一步，
   *  也不会留下"表改了、引用还指着旧名"的中间态。 */
  const renameStyle = (from: string, to: string) => {
    onBeginHistory();
    onApply((d) => {
      if (!d.styles) return;
      const s = d.styles[from] ?? {};
      delete d.styles[from];
      d.styles[to] = s;
      renameStyleRefs(d, from, to);
    });
  };

  const removeStyle = (name: string) => {
    onBeginHistory();
    onApply((d) => {
      if (!d.styles) return;
      delete d.styles[name];
      if (!Object.keys(d.styles).length) d.styles = undefined;
    });
  };

  const addStyle = () => {
    const name = nextStyleName(styles);
    onBeginHistory();
    onApply((d) => {
      d.styles = { ...(d.styles ?? {}), [name]: { size: 9 } };
    });
  };

  /** 复制一条样式（py880 那句"复制主题做自己的配色"）：字段逐字搬过去，**不搬引用** ——
   *  新样式是独立的一条，谁去引用它由使用者自己决定。 */
  const duplicateStyle = (name: string) => {
    const base = `${name}_副本`;
    let to = base;
    let i = 2;
    while (styles[to]) {
      to = `${base}${i}`;
      i += 1;
    }
    onBeginHistory();
    onApply((d) => {
      const src = d.styles?.[name];
      if (!src) return;
      d.styles = { ...(d.styles ?? {}), [to]: { ...src } };
    });
  };

  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal-card" onClick={(e) => e.stopPropagation()}>
        <div className="modal-head">
          <strong>样式</strong>
          <span className="muted small">
            共 {names.length} 条 · 控件用 <code>"style": "名字"</code> 或{" "}
            <code>{"{ \"use\": \"名字\", ...覆盖 }"}</code> 引用它
          </span>
          <button className="mini" onClick={onClose} title="关闭">
            ×
          </button>
        </div>

        <div className="modal-body">
          {names.length === 0 ? (
            <div className="hint-box">
              <div className="hint-title">什么是命名样式</div>
              <p className="muted small">
                一条命名样式 = 一组字体 / 颜色字段（字体、字号、粗体、文字色、背景色、对齐、圆角），
                和单个控件的 <code>style</code> 是同一套字段。以前同一套字号要在几十个控件上各写一遍，
                现在写一条样式，控件去引用它 —— 改一处，全变。
              </p>
              <p className="muted small">
                控件有三种引用写法：<code>"style": {"{ \"size\": 11 }"}</code>（内联，不引用样式）、
                <code>"style": "log"</code>（整个样式来自 <code>styles.log</code>）、
                <code>{"{ \"use\": \"log\", \"size\": 11 }"}</code>（先取样式、再用写了的字段覆盖）。
                合并规则是<strong>内联优先</strong>；<code>use</code> 指向不存在的名字时按"没有命名样式"处理，不报错。
              </p>
              <p className="muted small">
                引用点在「属性」面板的<strong>样式</strong>那一栏选。这里只编辑字段值，
                <strong>不预览</strong>外观（画布不做样式实时预览）。
              </p>
            </div>
          ) : (
            names.map((name) => (
              <StyleCard
                key={name}
                name={name}
                style={styles[name]}
                taken={taken}
                refCount={refCountOf(name)}
                onRename={(to) => renameStyle(name, to)}
                onPatch={(p) => patchStyle(name, p)}
                onRemove={() => removeStyle(name)}
                onDuplicate={() => duplicateStyle(name)}
              />
            ))
          )}
        </div>

        <div className="modal-foot">
          <p className="muted small">
            <strong>改名会连带更新引用</strong>：改名时每个控件的 style 一起改到新名字，不会留下悬空引用
            （只有手改 .cgui 里的名字才会，那种情况「属性」面板把失效的引用标红）。
            一条样式可以一个字段都不填，只要还有人引用着它；深色模式下的配色以控件写死的颜色为准，
            样式里写了的颜色会盖过深色缺省。
          </p>
          <button className="mini" onClick={addStyle}>
            新增样式
          </button>
          <button className="primary" onClick={onClose}>
            完成
          </button>
        </div>
      </div>
    </div>
  );
}
