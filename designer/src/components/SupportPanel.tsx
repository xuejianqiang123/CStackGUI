/* SupportPanel.tsx -- 「支持库」页签
 *
 * 面向新手：不用背函数名，按用途一层层点下去，选中看说明/示例，双击把调用代码复制走。
 * 数据在 ../supportlib.ts（唯一真相是 runtime/include/cstackgui.h）。
 */
import { useEffect, useMemo, useState } from "react";
import {
  SUPPORT_LIBS,
  countCommands,
  filterSupportLibs,
  firstCommand,
  type SupCmd,
  type SupGroup,
  type SupLib,
} from "../supportlib";
import { callbacksNamed, type FocusHit } from "../model/supportfocus";
import type { CgDoc } from "../model";

type Props = {
  /** 提示信息交给 App 的 toast */
  onSay: (msg: string) => void;
  /** 界面文档：用于"这个函数被哪些控件用着"的反查（不传则不显示反查） */
  doc?: CgDoc;
  /** 选中控件 -> 相关关键词：切到本页签时预置搜索框（双向连接的正向一头）。
   *  传空数组表示该控件没什么可查的，不覆盖用户已输入的搜索词。 */
  focusKeywords?: string[];
  /** 反查命中时选中画布上的控件（双向连接的反向一头：点函数 -> 高亮用它的控件） */
  onFindWidgets?: (ids: string[]) => void;
};

/** 无剪贴板权限时的兜底（Electron 里一般用不到，浏览器模式可能） */
function legacyCopy(text: string): boolean {
  try {
    const ta = document.createElement("textarea");
    ta.value = text;
    ta.style.position = "fixed";
    ta.style.opacity = "0";
    document.body.appendChild(ta);
    ta.select();
    const ok = document.execCommand("copy");
    document.body.removeChild(ta);
    return ok;
  } catch {
    return false;
  }
}

export default function SupportPanel({ onSay, doc, focusKeywords, onFindWidgets }: Props) {
  const [kw, setKw] = useState("");
  const [selected, setSelected] = useState<SupCmd | null>(null);
  /* 折叠状态按路径存；搜索时忽略它（命中都要看得见） */
  const [closed, setClosed] = useState<Set<string>>(new Set());
  /* 反查：当前选中的支持库函数被哪些控件用着（切函数时重算，用 useMemo 而非 effect） */
  const focus = useMemo<FocusHit[]>(() => (doc && selected ? callbacksNamed(doc, selected.fn) : []), [doc, selected]);

  /* 选中控件变了 -> 预置相关搜索词。**不覆盖用户手动输入**（focusKeywords 是"切页签那一下"
     给的默认视角，用户清空/改写后就该听他的）。空数组（label/box 这类没词可查）不动搜索框。 */
  useEffect(() => {
    if (focusKeywords && focusKeywords.length > 0) setKw(focusKeywords.join(" "));
  }, [focusKeywords]);

  const total = useMemo(() => countCommands(), []);
  const libs = useMemo(() => filterSupportLibs(SUPPORT_LIBS, kw), [kw]);
  const searching = kw.trim().length > 0;
  const hits = useMemo(
    () => libs.reduce((n, lib) => n + lib.groups.reduce((m, g) => m + countGroup(g), 0), 0),
    [libs],
  );

  /* 帮助卡片显示什么：用户点过且还在当前结果里 → 用它；
   * 否则（刚打开 / 搜索把选中的过滤掉了）落到第一条命中 —— 新手一进来就有东西看。 */
  const visibleFns = useMemo(
    () => new Set(libs.flatMap((lib) => lib.groups.flatMap(collectCmds)).map((x) => x.fn)),
    [libs],
  );
  const shown = selected && visibleFns.has(selected.fn) ? selected : libs.length ? firstCommand(libs[0]) : null;

  const copy = (text: string, what: string) => {
    const done = () => onSay(`已复制${what}：${text.split("\n")[0]}`);
    const fallback = () => (legacyCopy(text) ? done() : onSay("复制失败：剪贴板不可用，手动选中复制吧"));
    if (navigator.clipboard?.writeText) {
      navigator.clipboard.writeText(text).then(done).catch(fallback);
    } else {
      fallback();
    }
  };

  const isClosed = (key: string) => !searching && closed.has(key);

  const toggle = (key: string) => {
    setClosed((cur) => {
      const next = new Set(cur);
      if (next.has(key)) next.delete(key);
      else next.add(key);
      return next;
    });
  };

  const renderCmd = (cmd: SupCmd) => (
    <li key={cmd.fn}>
      <div
        className={shown?.fn === cmd.fn ? "sup-row sup-cmd on" : "sup-row sup-cmd"}
        title={`${cmd.brief}\n${cmd.decl}\n（双击复制调用代码）`}
        onClick={() => setSelected(cmd)}
        onDoubleClick={() => copy(cmd.sample ?? cmd.decl, cmd.fn)}
      >
        <code className="sup-fn">{cmd.fn}</code>
        <span className="sup-cname">{cmd.name}</span>
      </div>
    </li>
  );

  const renderGroup = (lib: SupLib, group: SupGroup, parentKey: string) => {
    const key = `${parentKey}/${group.label}`;
    const kids = group.children ?? [];
    const n = countGroup(group);
    return (
      <li key={key}>
        <div className="sup-row sup-group" onClick={() => toggle(key)}>
          <span className={isClosed(key) ? "sup-caret" : "sup-caret open"} />
          <span className="sup-label">{group.label}</span>
          <span className="sup-count">{n}</span>
        </div>
        {isClosed(key) ? null : (
          <ul className="sup-list">
            {group.cmds.map(renderCmd)}
            {kids.map((child) => renderGroup(lib, child, key))}
          </ul>
        )}
      </li>
    );
  };

  return (
    <div className="sup">
      <div className="sup-search">
        <input
          className="input"
          value={kw}
          placeholder="搜索命令，如：消息框 / 表格 / cg_find"
          spellCheck={false}
          onChange={(e) => setKw(e.target.value)}
        />
        <div className="sup-tip muted small">
          {searching ? `找到 ${hits} 个命令` : `共 ${total} 个命令，按用途分类`}
        </div>
      </div>

      <div className="sup-tree">
        {libs.length === 0 ? (
          <div className="muted small sup-empty">没有匹配的命令，换个词试试</div>
        ) : (
          <ul className="sup-list sup-root">
            {libs.map((lib) => (
              <li key={lib.key}>
                <div className="sup-row sup-lib" onClick={() => toggle(lib.key)}>
                  <span className={isClosed(lib.key) ? "sup-caret" : "sup-caret open"} />
                  <span className="sup-label">{lib.label}</span>
                </div>
                {isClosed(lib.key) ? null : (
                  <>
                    <div className="sup-lib-hint muted small">{lib.hint}</div>
                    <ul className="sup-list">
                      {lib.groups.map((g) => renderGroup(lib, g, lib.key))}
                    </ul>
                  </>
                )}
              </li>
            ))}
          </ul>
        )}
      </div>

      {shown ? (
        <div className="sup-help">
          <div className="sup-help-head">
            <span className="sup-help-name">{shown.name}</span>
            <button className="mini" onClick={() => copy(shown.sample ?? shown.decl, shown.fn)}>
              复制
            </button>
          </div>
          <code className="sup-help-fn">{shown.fn}</code>
          <pre className="code-block sup-decl">{shown.decl}</pre>
          <p className="muted small sup-brief">{shown.brief}</p>

          {shown.params?.length ? (
            <>
              <div className="insp-section">参数</div>
              <ul className="hint-list small">
                {shown.params.map((p) => (
                  <li key={p}>{p}</li>
                ))}
              </ul>
            </>
          ) : null}

          {shown.ret ? (
            <>
              <div className="insp-section">返回</div>
              <p className="muted small sup-brief">{shown.ret}</p>
            </>
          ) : null}

          {shown.sample ? (
            <>
              <div className="insp-section">示例</div>
              <pre className="code-block sup-sample">{shown.sample}</pre>
              <button className="mini" onClick={() => copy(shown.sample ?? "", "示例")}>
                复制示例
              </button>
            </>
          ) : null}

          {/* 反查（双向连接的反向一头）：这个函数当前被哪些控件/事件绑着。
              空 = 文档里没拿它当回调（不是错误 —— 多数函数在业务代码里手写调用，
              只有少数能当事件回调）。命中可一键选中画布上的控件。 */}
          {focus.length > 0 ? (
            <>
              <div className="insp-section">用到它的控件</div>
              <ul className="hint-list small">
                {focus.map((f) => (
                  <li key={`${f.widgetId}:${f.event}`}>
                    <code>{f.widgetId === "__window" ? "主窗口" : f.widgetId}</code>
                    <span className="muted small"> {f.widgetType} · {f.event}</span>
                    {onFindWidgets && f.widgetId !== "__window" ? (
                      <button
                        className="mini"
                        onClick={() => onFindWidgets([f.widgetId])}
                        title="在画布上选中这个控件"
                      >
                        定位
                      </button>
                    ) : null}
                  </li>
                ))}
              </ul>
            </>
          ) : null}
        </div>
      ) : (
        <div className="sup-help muted small">
          没有匹配的命令。换个说法试试：<code>消息框</code>、<code>表格</code>、<code>定时器</code>、<code>cg_find</code>。
        </div>
      )}
    </div>
  );
}

/** 统计一个分类下（含子分类）的命令条数 */
function countGroup(group: SupGroup): number {
  return group.cmds.length + (group.children ?? []).reduce((n, g) => n + countGroup(g), 0);
}

/** 收集一个分类下（含子分类）的全部命令 */
function collectCmds(group: SupGroup): SupCmd[] {
  return [...group.cmds, ...(group.children ?? []).flatMap(collectCmds)];
}
