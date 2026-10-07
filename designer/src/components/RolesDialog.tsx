/* RolesDialog.tsx -- 「色彩角色」对话框（工具条「色彩」按钮）
 *
 * 管理 .cgui 顶层的 roles{}：一条角色 = 一个语义色的浅色 / 深色两份值，
 * 颜色字段写 "@角色名" 引用它 —— 浅色取 light 槽、深色取 dark 槽，切主题全套跟着走。
 *
 * 与「样式」对话框同一套手感（刻意保持一致，学的是同一份范本）：
 *   1. **改名会连带更新引用**（renameRoleRefs：控件 style 的 fg/bg 引用与 app.bg_role 一起改），
 *      所以"改一处全变"才是真的。手改 .cgui 里的名字才会留悬空引用 ——
 *      运行时按"没写"处理，解析期出 WARN、selftest 报错。
 *   2. 只写了一个槽也行（另一个模式下的引用当没写）；两个槽都空的条目没意义，存盘时会被剔掉。
 *   3. 卡片上写「被 N 处引用」：改名/删除之前先让人看见影响面（fg/bg 各算一处）。
 *
 * 名字（= roles 的键）单独走「失焦才提交」，空名字/重名当场标红（不必等失焦）。
 */
import { useState } from "react";
import { countRefs, isSafeStyleName, renameRoleRefs, type CgDoc, type CgRole } from "../model";
import { Row } from "./Inspector/PanelUi";

type Props = {
  open: boolean;
  doc: CgDoc;
  onClose: () => void;
  onBeginHistory: () => void;
  onApply: (mutator: (doc: CgDoc) => void) => void;
};

function nextRoleName(roles: Record<string, CgRole>): string {
  let i = 1;
  while (roles[`role_${i}`]) i += 1;
  return `role_${i}`;
}

type CardProps = {
  name: string;
  role: CgRole;
  /** 全部角色名（判重名用；自己也在里面，同名会走「没改」分支提前返回） */
  taken: Set<string>;
  /** 有多少个字段引用着这条角色（fg/bg 各算一处） */
  refCount: number;
  onRename: (to: string) => void;
  onPatch: (patch: Partial<CgRole>) => void;
  onRemove: () => void;
  onDuplicate: () => void;
};

/** 一条角色 = 一张卡片。字段每改一下就落盘，只有名字是失焦/回车才提交。 */
function RoleCard({ name, role, taken, refCount, onRename, onPatch, onRemove, onDuplicate }: CardProps) {
  const [draft, setDraft] = useState(name);

  const next = draft.trim();
  const nameError = !isSafeStyleName(next)
    ? "角色名不能为空"
    : next !== name && taken.has(next)
      ? `已经有叫「${next}」的角色了`
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
          title="角色名：.cgui 顶层 roles 的键，颜色字段用 “@名字” 引用它"
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
              ? `${refCount} 处字段引用着它；改名会连带把它们一起改到新名字`
              : "没有字段引用它，改名或删除都不影响任何控件"
          }
        >
          {refCount > 0 ? `${refCount} 处引用` : "未被引用"}
        </span>
        <button className="mini" onClick={onDuplicate} title="以这条角色为初值另建一条：新的随便改，旧的原样不动">
          复制
        </button>
        <button
          className="mini"
          onClick={onRemove}
          title={refCount > 0 ? `删除这条角色：还有 ${refCount} 处引用，删了它们会按「没写」处理（外观退缺省）` : "删除这条角色（没有字段引用它）"}
        >
          删除
        </button>
      </div>
      {nameError ? <p className="act-note warn">{nameError}</p> : null}

      <Row label="浅色" hint="浅色模式下引用它的颜色；留空 = 这个模式下当没写">
        <input
          className="input color"
          type="color"
          value={role.light ?? "#6750a4"}
          onChange={(e) => onPatch({ light: e.target.value })}
        />
        <button className="mini" onClick={() => onPatch({ light: undefined })}>
          清除
        </button>
      </Row>

      <Row label="深色" hint="深色模式下引用它的颜色；留空 = 这个模式下当没写">
        <input
          className="input color"
          type="color"
          value={role.dark ?? "#d0bcff"}
          onChange={(e) => onPatch({ dark: e.target.value })}
        />
        <button className="mini" onClick={() => onPatch({ dark: undefined })}>
          清除
        </button>
      </Row>
    </div>
  );
}

export default function RolesDialog({ open, doc, onClose, onBeginHistory, onApply }: Props) {
  if (!open) return null;

  const roles = doc.roles ?? {};
  const names = Object.keys(roles);
  const taken = new Set(names);
  const roleRefs = countRefs(doc).roles;
  const refCountOf = (name: string) => roleRefs.get(name) ?? 0;

  const patchRole = (name: string, patch: Partial<CgRole>) => {
    onBeginHistory();
    onApply((d) => {
      const r = d.roles?.[name];
      if (!r) return;
      const bag = r as unknown as Record<string, unknown>;
      for (const k of Object.keys(patch)) {
        const v = (patch as Record<string, unknown>)[k];
        if (v === undefined) delete bag[k]; /* 清除 = 删掉该槽，不写进文件 */
        else bag[k] = v;
      }
    });
  };

  /** 改名：**同一次 onApply 里**把引用也改掉 —— 撤销栈里是一步，也不会留中间态。 */
  const renameRole = (from: string, to: string) => {
    onBeginHistory();
    onApply((d) => {
      if (!d.roles) return;
      const r = d.roles[from] ?? {};
      delete d.roles[from];
      d.roles[to] = r;
      renameRoleRefs(d, from, to);
    });
  };

  const removeRole = (name: string) => {
    onBeginHistory();
    onApply((d) => {
      delete d.roles?.[name];
      if (d.roles && !Object.keys(d.roles).length) d.roles = undefined;
    });
  };

  const addRole = () => {
    const name = nextRoleName(roles);
    onBeginHistory();
    onApply((d) => {
      d.roles = { ...(d.roles ?? {}), [name]: { light: "#6750A4", dark: "#D0BCFF" } };
    });
  };

  /** 复制一条角色：字段逐字搬过去，**不搬引用** —— 新角色是独立的一条。 */
  const duplicateRole = (name: string) => {
    const base = `${name}_副本`;
    let to = base;
    let i = 2;
    while (roles[to]) {
      to = `${base}${i}`;
      i += 1;
    }
    onBeginHistory();
    onApply((d) => {
      const src = d.roles?.[name];
      if (!src) return;
      d.roles = { ...(d.roles ?? {}), [to]: { ...src } };
    });
  };

  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal-card" onClick={(e) => e.stopPropagation()}>
        <div className="modal-head">
          <strong>色彩角色</strong>
          <span className="muted small">共 {names.length} 条</span>
          <button className="mini" onClick={onClose} title="关闭">
            ×
          </button>
        </div>

        <div className="modal-body">
          {names.length ? (
            <p className="act-legend muted small">
              颜色字段（文字色 / 背景色 / 窗口底色）写 <code>"@角色名"</code> 引用它：浅色取「浅色」槽、
              深色取「深色」槽，切主题全套跟着走。卡片上的「N 处引用」就是当前有多少个字段指着这个名字。
            </p>
          ) : null}
          {names.length === 0 ? (
            <div className="hint-box">
              <div className="hint-title">什么是色彩角色</div>
              <p className="muted small">
                一条角色 = 一个语义色在浅色 / 深色模式下的两份值。同一套界面要伺候两个主题时，
                把"品牌色 / 警示色 / 表面底色"这类**语义**定义成角色，颜色字段按名引用 ——
                切换深浅，全套跟着走，不用维护两份 .cgui。
              </p>
              <p className="muted small">
                引用写法：颜色字段直接写 <code>"@角色名"</code>（文字色 / 背景色 / 窗口底色都行）。
                只写一个槽也行——另一个模式下按「没写」处理。角色指向的名字不存在时同样当没写，
                不会报错中断（启动时会有一条提示）。
              </p>
              <p className="muted small">
                引用点在「属性」面板的<strong>样式</strong>一栏或这里改名时自动带上。
                这里只编辑字段值，<strong>不预览</strong>深色下的整页观感（画布不做深色预览，跑起来看）。
              </p>
            </div>
          ) : (
            names.map((name) => (
              <RoleCard
                key={name}
                name={name}
                role={roles[name]}
                taken={taken}
                refCount={refCountOf(name)}
                onRename={(to) => renameRole(name, to)}
                onPatch={(p) => patchRole(name, p)}
                onRemove={() => removeRole(name)}
                onDuplicate={() => duplicateRole(name)}
              />
            ))
          )}
        </div>

        <div className="modal-foot">
          <p className="muted small">
            <strong>改名会连带更新引用</strong>：改名时所有 "@旧名" 一起改到新名字，不会留悬空引用。
            只写一个槽的角色，另一个模式下的引用按「没写」处理。深色下的整页观感要跑起来看。
          </p>
          <button className="mini" onClick={addRole}>
            新增角色
          </button>
          <button className="primary" onClick={onClose}>
            完成
          </button>
        </div>
      </div>
    </div>
  );
}
