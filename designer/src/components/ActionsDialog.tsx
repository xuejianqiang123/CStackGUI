/* ActionsDialog.tsx -- 「动作」对话框（工具条「动作」按钮）
 *
 * 管理 .cgui 顶层的 actions：一条动作 = 显示文字 + 快捷键 + 一个 C 回调名。
 * 菜单项用 "action": "save" 引用它，控件也有自己的「动作」下拉 —— 于是
 * 「一个回调、一处启用状态」同时管住所有引用点（不用给菜单项和按钮各绑一份回调）。
 *
 * 两件要注意的事（在界面上明说）：
 *   1. **改名不会自动更新引用**（v1 的约定）：引用处会变成「不存在的动作」，自检会报出来；
 *   2. 快捷键写错不会崩：运行时忽略它并写警告日志，--selftest 会报——所以旁边给了可用键列表。
 *
 * 名字（= actions 的键）是唯一会被「删掉再建」的字段，所以它单独走「失焦才提交」：
 * 否则每敲一个字符就重建一次键、还会把引用打断。
 */
import { useState } from "react";
import { isSafeActionName, type CgAction, type CgDoc } from "../model";
import { Row } from "./Inspector/PanelUi";

type Props = {
  open: boolean;
  doc: CgDoc;
  onClose: () => void;
  onBeginHistory: () => void;
  onApply: (mutator: (doc: CgDoc) => void) => void;
};

/** 快捷键的可用键（逐字对齐契约与 cstackgui.h：写错了运行时只是忽略 + 警告） */
const ACCEL_HINT =
  "修饰键 Ctrl / Shift / Alt 可任意顺序、大小写不敏感；键 = A-Z、0-9、F1-F24、Esc、Enter、Tab、" +
  "Space、Back（退格）、Delete、Insert、Home、End、PageUp、PageDown、Left、Right、Up、Down。" +
  "不允许无修饰键的单个字母或数字（裸 S 会让用户没法往输入框里打 s），裸功能键（F5）可以。";

function nextActionName(actions: Record<string, CgAction>): string {
  let i = 1;
  while (actions[`action_${i}`]) i += 1;
  return `action_${i}`;
}

type CardProps = {
  name: string;
  action: CgAction;
  /** 全部动作名（判重名用；自己也在里面，同名会走「没改」分支提前返回） */
  taken: Set<string>;
  onRename: (to: string) => void;
  onPatch: (patch: Partial<CgAction>) => void;
  onRemove: () => void;
};

/** 一条动作 = 一张卡片。text/accel/handler 每敲一下就落盘（和「程序配置」同一套手感），
 *  只有动作名是失焦/回车才提交。 */
function ActionCard({ name, action, taken, onRename, onPatch, onRemove }: CardProps) {
  const [draft, setDraft] = useState(name);
  const [error, setError] = useState("");

  const commitName = () => {
    const next = draft.trim();
    if (next === name) {
      setError("");
      return;
    }
    if (!isSafeActionName(next)) {
      setDraft(name);
      setError(next ? `「${next}」不能当动作名` : "动作名不能为空");
      return;
    }
    if (taken.has(next)) {
      setDraft(name);
      setError(`已经有叫「${next}」的动作了`);
      return;
    }
    setError("");
    onRename(next);
  };

  return (
    <div className="act-card">
      <div className="act-card-head">
        <input
          className="input mono act-name"
          value={draft}
          spellCheck={false}
          title="动作名：.cgui 顶层 actions 的键，菜单项和控件用 action 引用它"
          onChange={(e) => setDraft(e.target.value)}
          onBlur={commitName}
          onKeyDown={(e) => {
            if (e.key === "Enter") e.currentTarget.blur();
          }}
        />
        <button className="mini" onClick={onRemove} title="删除这个动作（引用它的菜单项/控件会变成「不存在的动作」）">
          删除
        </button>
      </div>
      {error ? <p className="act-note warn">{error}</p> : null}

      <Row label="文字" hint="引用它的菜单项/控件显示这段文字；留空则用引用点自己的文字">
        <input
          className="input"
          value={action.text ?? ""}
          placeholder="（留空 = 用引用点自己的文字）"
          onChange={(e) => onPatch({ text: e.target.value || undefined })}
        />
      </Row>

      <Row
        label="快捷键"
        hint="Ctrl/Shift/Alt + 键；留空 = 没有快捷键。不允许无修饰键的单个字母/数字（裸 S 会让用户没法往输入框里打 s），裸功能键（F5）可以"
      >
        <input
          className="input mono"
          value={action.accel ?? ""}
          placeholder="Ctrl+S"
          spellCheck={false}
          onChange={(e) => onPatch({ accel: e.target.value || undefined })}
        />
      </Row>

      <Row label="回调名" hint="C 侧 CG_BIND 的名字：导出 C 骨架时会生成同名空函数">
        <input
          className="input mono"
          value={action.handler ?? ""}
          placeholder={`on_${name}`}
          spellCheck={false}
          onChange={(e) => onPatch({ handler: e.target.value.trim() || undefined })}
        />
      </Row>

      <Row label="">
        <label className="check-inline">
          <input
            type="checkbox"
            checked={action.checked === true}
            onChange={(e) => onPatch({ checked: e.target.checked || undefined })}
          />
          初始勾选（菜单项打勾）
        </label>
        <label className="check-inline">
          <input
            type="checkbox"
            checked={action.enabled !== false}
            onChange={(e) => onPatch({ enabled: e.target.checked ? undefined : false })}
          />
          启用
        </label>
      </Row>

      {!action.handler ? (
        <p className="act-note warn">还没填回调名：这个动作会被运行时忽略，--selftest 会报错。</p>
      ) : null}
    </div>
  );
}

export default function ActionsDialog({ open, doc, onClose, onBeginHistory, onApply }: Props) {
  if (!open) return null;

  const actions = doc.actions ?? {};
  const names = Object.keys(actions);
  const taken = new Set(names);

  const patchAction = (name: string, patch: Partial<CgAction>) => {
    onBeginHistory();
    onApply((d) => {
      const a = d.actions?.[name];
      if (!a) return;
      if ("text" in patch) a.text = patch.text;
      if ("accel" in patch) a.accel = patch.accel;
      if ("handler" in patch) a.handler = patch.handler;
      if ("checked" in patch) a.checked = patch.checked;
      if ("enabled" in patch) a.enabled = patch.enabled;
    });
  };

  const renameAction = (from: string, to: string) => {
    onBeginHistory();
    onApply((d) => {
      if (!d.actions) return;
      const a = d.actions[from] ?? {};
      delete d.actions[from];
      d.actions[to] = a;
    });
  };

  const removeAction = (name: string) => {
    onBeginHistory();
    onApply((d) => {
      if (!d.actions) return;
      delete d.actions[name];
      if (!Object.keys(d.actions).length) d.actions = undefined;
    });
  };

  /** 新建：回调名先按 `on_<动作名>` 填好 —— 空 handler 的动作会被运行时忽略，
   *  而这个名字正好是导出骨架时那个空函数的名字，用户只要去实现它。 */
  const addAction = () => {
    const name = nextActionName(actions);
    onBeginHistory();
    onApply((d) => {
      d.actions = { ...(d.actions ?? {}), [name]: { handler: `on_${name}` } };
    });
  };

  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div className="modal-card" onClick={(e) => e.stopPropagation()}>
        <div className="modal-head">
          <strong>动作</strong>
          <span className="muted small">
            共 {names.length} 条 · 菜单项与控件引用它，C 侧靠 <code>CG_BIND</code> 按名字找回调
          </span>
          <button className="mini" onClick={onClose} title="关闭">
            ×
          </button>
        </div>

        <div className="modal-body">
          {names.length ? (
            <p className="act-legend muted small">快捷键的写法：{ACCEL_HINT}</p>
          ) : null}
          {names.length === 0 ? (
            <div className="hint-box">
              <div className="hint-title">什么是动作</div>
              <p className="muted small">
                一条动作 = 显示文字 + 快捷键 + 一个 C 回调名。菜单项用 <code>"action": "save"</code> 引用它，
                控件在「属性 → 动作」里选它：点击、快捷键都走同一个回调，
                <code>cg_action_set_enabled</code> 一次就能把菜单项和按钮一起变灰。
              </p>
              <p className="muted small">
                快捷键由消息循环里的加速键表处理，所以焦点在输入框里时照样生效
                （窗口级 keydown 事件做不到这一点）。
              </p>
            </div>
          ) : (
            names.map((name) => (
              <ActionCard
                key={name}
                name={name}
                action={actions[name]}
                taken={taken}
                onRename={(to) => renameAction(name, to)}
                onPatch={(p) => patchAction(name, p)}
                onRemove={() => removeAction(name)}
              />
            ))
          )}
        </div>

        <div className="modal-foot">
          <p className="muted small">
            选好动作后，菜单项 / 控件的点击就走这个回调；<strong>改名不会自动更新引用</strong>
            （引用处会变成「不存在的动作」，<code>--selftest</code> 会报出来）。
            导出 C 骨架时会为每个回调名生成空函数。
          </p>
          <button className="mini" onClick={addAction}>
            新增动作
          </button>
          <button className="primary" onClick={onClose}>
            完成
          </button>
        </div>
      </div>
    </div>
  );
}
