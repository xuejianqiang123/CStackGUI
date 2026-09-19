/* Inspector.tsx -- 属性面板：改 id、文字、位置尺寸、字体、颜色、状态
 *
 * 每个字段都直接改文档模型，改完即可保存，没有「应用」按钮。
 */
import {
  KIND_MAP,
  findParent,
  itemsToText,
  menuToText,
  tableColumnsToText,
  tableRowsToText,
  textToItems,
  textToMenu,
  textToTableColumns,
  textToTableRows,
  textToTree,
  treeToText,
  type CgDoc,
  type CgNode,
} from "../model";

type Props = {
  doc: CgDoc;
  node: CgNode | null;
  onBeginHistory: () => void;
  onApply: (mutator: (doc: CgDoc) => void) => void;
  onSelect: (id: string | null) => void;
};

function findAny(nodes: CgNode[], id: string): CgNode | null {
  for (const n of nodes) {
    if (n.id === id) return n;
    const hit = n.children ? findAny(n.children, id) : null;
    if (hit) return hit;
  }
  return null;
}

function Row({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div className="insp-row">
      <span className="insp-label">{label}</span>
      <span className="insp-value">{children}</span>
    </div>
  );
}

/** 多行内容的编辑器：树/表格/条目都用纯文本编辑，格式简单、一眼能看懂。
 *  撤销栈只在获得焦点时压一次，避免每敲一个字都进历史。 */
function Area({
  label,
  hint,
  value,
  rows = 4,
  onFocus,
  onChange,
}: {
  label: string;
  hint: string;
  value: string;
  rows?: number;
  onFocus: () => void;
  onChange: (v: string) => void;
}) {
  return (
    <div className="insp-area">
      <div className="insp-area-head">
        <span>{label}</span>
        <span className="muted small">{hint}</span>
      </div>
      <textarea
        className="input mono insp-textarea"
        rows={rows}
        value={value}
        spellCheck={false}
        onFocus={onFocus}
        onChange={(e) => onChange(e.target.value)}
      />
    </div>
  );
}

export default function Inspector({ doc, node, onBeginHistory, onApply, onSelect }: Props) {
  if (!node) {
    return (
      <div className="panel-body">
        <p className="muted">选中一个控件后，这里显示它的属性。</p>
        <div className="hint-box">
          <div className="hint-title">画布在做什么</div>
          <ul className="hint-list">
            <li>这里的每个字段都会写进 .cgui</li>
            <li>.cgui 由 C 运行时解析，控件就是原生 Win32 控件</li>
            <li>事件名对应 C 侧的 <code>CG_BIND("名字", 函数)</code></li>
          </ul>
        </div>
      </div>
    );
  }

  const patch = (fn: (n: CgNode) => void) => {
    onBeginHistory();
    onApply((d) => {
      const t = findAny(d.widgets, node.id);
      if (t) fn(t);
    });
  };

  const info = KIND_MAP[node.type];
  const style = node.style ?? {};
  const parent = findParent(doc, node.id);
  const inTabs = parent?.type === "tabs";
  /** 多行文本编辑：不进撤销栈，靠聚焦时压一次快照 */
  const patchSoft = (fn: (n: CgNode) => void) => {
    onApply((d) => {
      const t = findAny(d.widgets, node.id);
      if (t) fn(t);
    });
  };

  return (
    <div className="panel-body">
      <div className="node-head">
        <span className={`glyph glyph-${node.type}`} />
        <strong>{info?.label ?? node.type}</strong>
        <span className="muted small">{node.type}</span>
      </div>

      <Row label="id">
        <input
          className="input mono"
          value={node.id}
          spellCheck={false}
          onChange={(e) => {
            const raw = e.target.value.replace(/[^0-9a-zA-Z_]/g, "_");
            if (!raw) return; /* 清空输入的过程不改 id，空 id 会让运行时查找出问题 */
            /* 查重：id 是 cg_find 的凭据，重名会让「找控件」命中错的那个。
             * 冲突时自动加后缀，输入过程依然顺畅。 */
            const taken = new Set<string>();
            const collect = (ns: CgNode[]) =>
              ns.forEach((n) => {
                taken.add(n.id);
                if (n.children) collect(n.children);
              });
            collect(doc.widgets);
            taken.delete(node.id);
            let unique = raw;
            let k = 2;
            while (taken.has(unique)) unique = `${raw}_${k++}`;
            patch((n) => {
              n.id = unique;
            });
            onSelect(unique);
          }}
        />
      </Row>
      <Row label="文字">
        <input
          className="input"
          value={node.text ?? ""}
          onChange={(e) =>
            patch((n) => {
              n.text = e.target.value;
            })
          }
        />
      </Row>

      <div className="insp-section">位置与尺寸</div>
      {node.type === "menu" ? (
        <p className="muted small">菜单栏挂在窗口上，没有位置和尺寸可调。</p>
      ) : null}
      <div className="insp-grid4">
        {(["x", "y", "w", "h"] as const).map((k) => (
          <label key={k} className="insp-num">
            <span>{k.toUpperCase()}</span>
            <input
              className="input"
              type="number"
              value={Math.round(node.rect[k])}
              onChange={(e) =>
                patch((n) => {
                  n.rect[k] = Number(e.target.value) || 0;
                })
              }
            />
          </label>
        ))}
      </div>

      <div className="insp-section">字体与颜色</div>
      <Row label="字体">
        <input
          className="input"
          placeholder="Microsoft YaHei UI"
          value={style.font ?? ""}
          onChange={(e) =>
            patch((n) => {
              n.style = { ...(n.style ?? {}), font: e.target.value || undefined };
            })
          }
        />
      </Row>
      <Row label="字号">
        <input
          className="input"
          type="number"
          value={style.size ?? 0}
          onChange={(e) =>
            patch((n) => {
              const v = Number(e.target.value) || 0;
              n.style = { ...(n.style ?? {}), size: v || undefined };
            })
          }
        />
        <label className="check-inline">
          <input
            type="checkbox"
            checked={!!style.bold}
            onChange={(e) =>
              patch((n) => {
                n.style = { ...(n.style ?? {}), bold: e.target.checked || undefined };
              })
            }
          />
          粗体
        </label>
      </Row>
      <Row label="文字色">
        <input
          className="input color"
          type="color"
          value={style.fg ?? "#1d1b20"}
          onChange={(e) =>
            patch((n) => {
              n.style = { ...(n.style ?? {}), fg: e.target.value };
            })
          }
        />
        <button
          className="mini"
          onClick={() =>
            patch((n) => {
              if (n.style) delete n.style.fg;
            })
          }
        >
          跟随系统
        </button>
      </Row>
      <Row label="背景色">
        <input
          className="input color"
          type="color"
          value={style.bg ?? "#eadcff"}
          onChange={(e) =>
            patch((n) => {
              n.style = { ...(n.style ?? {}), bg: e.target.value };
            })
          }
        />
        <button
          className="mini"
          onClick={() =>
            patch((n) => {
              if (n.style) delete n.style.bg;
            })
          }
        >
          跟随系统
        </button>
      </Row>
      <Row label="对齐">
        <select
          className="input"
          value={style.align ?? "left"}
          onChange={(e) =>
            patch((n) => {
              n.style = { ...(n.style ?? {}), align: e.target.value as "left" | "center" | "right" };
            })
          }
        >
          <option value="left">左对齐</option>
          <option value="center">居中</option>
          <option value="right">右对齐</option>
        </select>
      </Row>

      <div className="insp-section">状态</div>
      <Row label="可见">
        <label className="check-inline">
          <input
            type="checkbox"
            checked={node.visible !== false}
            onChange={(e) =>
              patch((n) => {
                n.visible = e.target.checked;
              })
            }
          />
          显示
        </label>
      </Row>
      <Row label="可用">
        <label className="check-inline">
          <input
            type="checkbox"
            checked={node.enabled !== false}
            onChange={(e) =>
              patch((n) => {
                n.enabled = e.target.checked;
              })
            }
          />
          可点击
        </label>
      </Row>
      {info?.checklist ? (
        <Row label="勾选">
          <label className="check-inline">
            <input
              type="checkbox"
              checked={!!node.checked}
              onChange={(e) =>
                patch((n) => {
                  n.checked = e.target.checked;
                })
              }
            />
            初始选中
          </label>
        </Row>
      ) : null}
      {info?.ranged ? (
        <>
          <Row label="值">
            <input
              className="input"
              type="number"
              value={node.value ?? 0}
              onChange={(e) =>
                patch((n) => {
                  n.value = Number(e.target.value) || 0;
                })
              }
            />
          </Row>
          <Row label="范围">
            <input
              className="input narrow"
              type="number"
              value={node.min ?? 0}
              onChange={(e) =>
                patch((n) => {
                  n.min = Number(e.target.value) || 0;
                })
              }
            />
            <span className="muted">—</span>
            <input
              className="input narrow"
              type="number"
              value={node.max ?? 100}
              onChange={(e) =>
                patch((n) => {
                  n.max = Number(e.target.value) || 1;
                })
              }
            />
          </Row>
        </>
      ) : null}

      {node.type === "timer" ? (
        <Row label="周期">
          <input
            className="input"
            type="number"
            min={0}
            step={100}
            value={node.period ?? 0}
            onChange={(e) =>
              patch((n) => {
                n.period = Math.max(0, Number(e.target.value) || 0);
              })
            }
          />
          <span className="muted small">毫秒，0 = 停</span>
        </Row>
      ) : null}
      {node.type === "colorpicker" ? (
        <Row label="颜色">
          <input
            className="input color"
            type="color"
            value={`#${(node.value ?? 0x6750a4).toString(16).padStart(6, "0")}`}
            onChange={(e) =>
              patch((n) => {
                n.value = parseInt(e.target.value.slice(1), 16);
              })
            }
          />
          <span className="muted small">运行时点色块弹选色对话框</span>
        </Row>
      ) : null}
      {node.type === "browser" ? (
        <Row label="地址">
          <input
            className="input mono"
            value={node.src ?? ""}
            placeholder="about:blank"
            spellCheck={false}
            onChange={(e) =>
              patch((n) => {
                n.src = e.target.value || undefined;
              })
            }
          />
        </Row>
      ) : null}
      {node.type === "shape" ? (
        <Row label="形状">
          <select
            className="input"
            value={node.format ?? "roundrect"}
            onChange={(e) => patch((n) => { n.format = e.target.value; })}
          >
            <option value="rect">矩形</option>
            <option value="roundrect">圆角矩形</option>
            <option value="ellipse">椭圆</option>
          </select>
        </Row>
      ) : null}
      {node.type === "table" ? (
        <>
          <Row label="多选">
            <label className="check-inline">
              <input
                type="checkbox"
                checked={!!node.multi}
                onChange={(e) =>
                  patch((n) => {
                    n.multi = e.target.checked || undefined;
                  })
                }
              />
              允许同时选中多行
            </label>
          </Row>
          <Row label="勾选列">
            <label className="check-inline">
              <input
                type="checkbox"
                checked={!!node.check}
                onChange={(e) =>
                  patch((n) => {
                    n.check = e.target.checked || undefined;
                  })
                }
              />
              显示行首复选框（check 事件）
            </label>
          </Row>
        </>
      ) : null}

      <Row label="悬停提示">
        <input
          className="input"
          value={node.hint ?? ""}
          placeholder="鼠标停上去显示的文字"
          onChange={(e) =>
            patch((n) => {
              n.hint = e.target.value || undefined;
            })
          }
        />
      </Row>

      <div className="insp-section">内容</div>
      {inTabs ? (
        <Row label="所在页">
          <input
            className="input narrow"
            type="number"
            min={0}
            value={node.page ?? 0}
            onChange={(e) =>
              patch((n) => {
                const v = Number(e.target.value) || 0;
                n.page = v > 0 ? v : undefined;
              })
            }
          />
          <span className="muted small">第几页（0 起）</span>
        </Row>
      ) : null}

      {node.type === "tabs" ? (
        <Row label="当前页">
          <select
            className="input"
            value={node.value ?? 0}
            onChange={(e) =>
              patch((n) => {
                n.value = Number(e.target.value);
              })
            }
          >
            {(node.items ?? []).map((t, i) => (
              <option key={i} value={i}>
                {i + 1}. {t || `页面 ${i + 1}`}
              </option>
            ))}
          </select>
        </Row>
      ) : null}

      {node.type === "image" ? (
        <Row label="图片">
          <input
            className="input mono"
            value={node.src ?? ""}
            placeholder="assets/picture.png"
            spellCheck={false}
            onChange={(e) =>
              patch((n) => {
                n.src = e.target.value || undefined;
              })
            }
          />
        </Row>
      ) : null}
      {node.type === "image" ? (
        <p className="muted small">
          路径相对 .cgui 所在目录；支持 png / jpg / bmp / gif。画布上只显示文件名，运行时才画真图。
        </p>
      ) : null}

      {info?.listLike ? (
        <Area
          label={
            node.type === "statusbar" ? "分段文字" : node.type === "tabs" ? "页签文字" : node.type === "toolbar" ? "按钮文字" : "条目"
          }
          hint="每行一条"
          value={itemsToText(node.items)}
          onFocus={onBeginHistory}
          onChange={(v) => patchSoft((n) => { n.items = textToItems(v); })}
        />
      ) : null}

      {node.type === "menu" || node.type === "menubutton" ? (
        <>
          <Area
            label="菜单项"
            hint="两空格一层 · 文本 | id · - 是分隔线 · 末尾 | v 表示勾选"
            rows={8}
            value={menuToText(node.nodes)}
            onFocus={onBeginHistory}
            onChange={(v) => patchSoft((n) => { n.nodes = textToMenu(v); })}
          />
          <p className="muted small">
            {node.type === "menu"
              ? "菜单栏没有矩形：运行时挂在窗口上（一根窗口只能有一根）。点击某一项时给这个控件派发 click，用 cg_menu_last_id() 取回 id。"
              : "点按钮会在下方弹出这个菜单；选中项同样回到控件的 click 回调。"}
          </p>
        </>
      ) : null}

      {node.type === "datepicker" ? (
        <Row label="格式">
          <select
            className="input"
            value={node.format ?? "date"}
            onChange={(e) => patch((n) => { n.format = e.target.value; })}
          >
            <option value="date">日期 2026-09-19</option>
            <option value="time">时间 14:30</option>
            <option value="datetime">日期 + 时间（两个控件拼的）</option>
          </select>
        </Row>
      ) : null}

      {node.type === "richtext" ? (
        <Area
          label="内容"
          hint="富文本可以放多行"
          rows={4}
          value={node.text ?? ""}
          onFocus={onBeginHistory}
          onChange={(v) => patchSoft((n) => { n.text = v; })}
        />
      ) : null}

      {node.type === "tree" ? (
        <Area
          label="树节点"
          hint="两个空格缩进一层"
          rows={6}
          value={treeToText(node.nodes)}
          onFocus={onBeginHistory}
          onChange={(v) => patchSoft((n) => { n.nodes = textToTree(v); })}
        />
      ) : null}

      {node.type === "table" ? (
        <>
          <Area
            label="列"
            hint="逗号分隔，可写 标题:宽度"
            rows={2}
            value={tableColumnsToText(node.columns)}
            onFocus={onBeginHistory}
            onChange={(v) => patchSoft((n) => { n.columns = textToTableColumns(v); })}
          />
          <Area
            label="行"
            hint="每行一条，单元格用逗号分隔"
            rows={5}
            value={tableRowsToText(node.rows)}
            onFocus={onBeginHistory}
            onChange={(v) => patchSoft((n) => { n.rows = textToTableRows(v); })}
          />
        </>
      ) : null}

      {node.children?.length ? (
        <p className="muted small">
          {node.type === "tabs" ? "标签页" : "这是容器"}，里面还有 {node.children.length} 个子控件
          （坐标相对本控件）。
        </p>
      ) : null}
      <p className="muted small">
        窗口：{doc.app.width}×{doc.app.height} · 缩放以画布顶部工具条为准
      </p>
    </div>
  );
}
