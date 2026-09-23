/* Inspector.tsx -- 属性面板：改 id、文字、位置尺寸、字体、颜色、状态
 *
 * 每个字段都直接改文档模型，改完即可保存，没有「应用」按钮。
 */
import { useState } from "react";
import { getBridge } from "../bridge";
import {
  KIND_MAP,
  findParent,
  isNumEdit,
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

/** 收集能当绑定源的控件 id（已排序）。
 *  排除自己和自己内部：绑自己没有意义，也不会是用户想要的结果。 */
function collectBindIds(nodes: CgNode[], skipId: string): string[] {
  const out: string[] = [];
  const walk = (ns: CgNode[]) => {
    for (const n of ns) {
      if (n.id === skipId) continue;   /* 连它内部一起跳过 */
      out.push(n.id);
      if (n.children) walk(n.children);
    }
  };
  walk(nodes);
  return out.sort();
}

/** @param hint 可选说明，鼠标停在标签上时显示（字段含义较长时用，别把面板挤成一团） */
function Row({ label, hint, children }: { label: string; hint?: string; children: React.ReactNode }) {
  return (
    <div className="insp-row">
      <span className="insp-label" title={hint}>
        {label}
      </span>
      <span className="insp-value">{children}</span>
    </div>
  );
}

/* 位置与尺寸的中文标签（.cgui 里仍是 rect.x/y/w/h，只是面板上好认） */
const RECT_LABEL: Record<"x" | "y" | "w" | "h", string> = {
  x: "左边",
  y: "顶边",
  w: "宽度",
  h: "高度",
};

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
  /** 字体选择器展开状态：换控件时保持原状，点 Aa 图标收起/展开 */
  const [fontOpen, setFontOpen] = useState(false);
  /** 选背景图（按钮）：Electron 弹文件对话框，优先给相对 .cgui 的路径；浏览器模式只有文件名 */
  const pickImage = async () => {
    const res = await getBridge().pickImage();
    if (res.canceled) return;
    const p = res.rel || res.path;
    if (p) patch((n) => { n.src = p; });
  };
  const parent = findParent(doc, node.id);
  const inTabs = parent?.type === "tabs";
  /** 可以当绑定源的控件（别人指向我的 id 改了就一起改，见上面 id 那段） */
  const bindIds = collectBindIds(doc.widgets, node.id);
  /* 标签 / 链接 / 分割线默认不画底色（透明）；这里给个显式开关，用户一眼能看清 */
  const transparentBg = node.type === "label" || node.type === "link" || node.type === "separator";
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
            const oldId = node.id;
            onBeginHistory();
            onApply((d) => {
              /* 别人指向这个 id 的绑定一起改掉，否则运行时找不到源、绑定静默失效 */
              if (unique !== oldId) {
                const fix = (ns: CgNode[]) =>
                  ns.forEach((n) => {
                    if (n.bind?.src === oldId) n.bind = { ...n.bind, src: unique };
                    if (n.children) fix(n.children);
                  });
                fix(d.widgets);
              }
              const t = findAny(d.widgets, oldId);
              if (t) t.id = unique;
            });
            onSelect(unique);
          }}
        />
      </Row>
      {/* 同样是 text 字段：仪表盘上是单位（画在中心数值下面），灯上是灯右边的说明文字。
          数值框（edit + 调节器）没有这一行 —— 它的显示文本由「值」派生，写了也不生效。 */}
      {isNumEdit(node) ? null : (
        <Row label={node.type === "meter" ? "单位" : "文字"}>
          <input
            className="input"
            value={node.text ?? ""}
            placeholder={node.type === "meter" ? "°C" : undefined}
            onChange={(e) =>
              patch((n) => {
                n.text = e.target.value;
              })
            }
          />
        </Row>
      )}

      <div className="insp-section">位置与尺寸</div>
      {node.type === "menu" ? (
        <p className="muted small">菜单栏挂在窗口上，没有位置和尺寸可调。</p>
      ) : null}
      <div className="insp-grid4">
        {(["x", "y", "w", "h"] as const).map((k) => (
          <label key={k} className="insp-num">
            <span>{RECT_LABEL[k]}</span>
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

      {node.type === "edit" ? (
        <>
          <div className="insp-section">输入框</div>
          {/* 预设：以前调色板里「单行输入 / 多行输入 / 数字微调」是三个类型，合并成 edit
              之后在这儿一键切回同样的组合，省得挨个改字段。 */}
          <Row label="预设" hint="多行 = Enter 变换行 + 纵向滚动；数字框 = 右侧箭头 + 整数过滤 + 值/范围">
            <span className="ev-actions">
              <button
                className={!node.multiline && !node.spin ? "mini on" : "mini"}
                onClick={() =>
                  patch((n) => {
                    delete n.multiline;
                    delete n.scroll;
                    delete n.spin;
                    delete n.input;
                  })
                }
              >
                单行
              </button>
              <button
                className={node.multiline ? "mini on" : "mini"}
                onClick={() =>
                  patch((n) => {
                    n.multiline = true;
                    n.scroll = 2;
                    delete n.spin;
                  })
                }
              >
                多行
              </button>
              <button
                className={isNumEdit(node) ? "mini on" : "mini"}
                onClick={() =>
                  patch((n) => {
                    delete n.multiline;
                    n.spin = 1;
                    n.input = 7;
                    if (n.min === undefined) n.min = 0;
                    if (n.max === undefined || n.max > 999) n.max = 99;
                    if (n.value === undefined) n.value = 0;
                  })
                }
              >
                数字框
              </button>
            </span>
          </Row>
          <Row label="最大长度" hint="最多允许输入多少字符；0 = 不限制">
            <input
              className="input narrow"
              type="number"
              value={node.maxlen ?? 0}
              onChange={(e) =>
                patch((n) => {
                  const v = Math.max(0, Math.round(Number(e.target.value) || 0));
                  if (v > 0) n.maxlen = v;
                  else delete n.maxlen;
                })
              }
            />
          </Row>
          <Row label="允许多行" hint="按 Enter 变换行（多行时通常还要配竖向滚动条）">
            <label className="check-inline">
              <input
                type="checkbox"
                checked={!!node.multiline}
                onChange={(e) =>
                  patch((n) => {
                    if (e.target.checked) n.multiline = true;
                    else delete n.multiline;
                  })
                }
              />
              <span className="muted small">Enter 变换行</span>
            </label>
          </Row>
          <Row label="滚动条">
            <select
              className="input"
              value={String(node.scroll ?? 0)}
              onChange={(e) =>
                patch((n) => {
                  const v = Number(e.target.value) || 0;
                  if (v) n.scroll = v;
                  else delete n.scroll;
                })
              }
            >
              <option value="0">无</option>
              <option value="1">横向滚动条</option>
              <option value="2">纵向滚动条</option>
              <option value="3">横向及纵向滚动条</option>
            </select>
          </Row>
          <Row label="边框" hint="缺省 = 本工程的圆角描边；其余六种和属性面板的习惯叫法一致，聚焦时一律变紫">
            <select
              className="input"
              value={String(node.border ?? 0)}
              onChange={(e) =>
                patch((n) => {
                  const v = Number(e.target.value) || 0;
                  if (v) n.border = v;
                  else delete n.border;
                })
              }
            >
              <option value="0">缺省（圆角描边）</option>
              <option value="1">无边框</option>
              <option value="2">凹入式</option>
              <option value="3">凸出式</option>
              <option value="4">浅凹入式</option>
              <option value="5">镜框式</option>
              <option value="6">单线边框式</option>
            </select>
          </Row>
          <Row label="输入方式" hint="只读 = 能选中但改不了；密码 = 显示圆点；数值/日期档位会按键过滤 + 失焦时夹回或清空">
            <select
              className="input"
              value={String(node.input ?? 0)}
              onChange={(e) =>
                patch((n) => {
                  const v = Number(e.target.value) || 0;
                  if (v) n.input = v;
                  else delete n.input;
                })
              }
            >
              <option value="0">通常方式</option>
              <option value="1">只读方式</option>
              <option value="2">密码输入</option>
              <option value="3">整数文本输入</option>
              <option value="4">小数文本输入</option>
              <option value="5">输入字节（0~255）</option>
              <option value="6">输入短整数（±32767）</option>
              <option value="7">输入整数（±21 亿）</option>
              <option value="8">输入长整数</option>
              <option value="9">输入小数</option>
              <option value="10">输入双精度小数</option>
              <option value="11">输入日期时间</option>
            </select>
          </Row>
          <Row
            label="调节器"
            hint="右侧上下箭头，开了它就是「数值框」（值是真相、文本由它派生）：自动 = 点一下按 ±1 改值并夹到下面的底限/上限；手动 = 只发「调节钮被按下」，值由业务在回调里自己改。多行输入框没有箭头"
          >
            <select
              className="input"
              disabled={!!node.multiline}
              value={String(node.spin ?? 0)}
              onChange={(e) =>
                patch((n) => {
                  const v = Number(e.target.value) || 0;
                  if (v) n.spin = v;
                  else delete n.spin;
                })
              }
            >
              <option value="0">无</option>
              <option value="1">自动</option>
              <option value="2">手动</option>
            </select>
          </Row>
          {isNumEdit(node) ? (
            <Row label="值" hint="数值框的真相：界面上显示的就是它，箭头 ±1、cg_widget_value()、双向绑定全按它走">
              <input
                className="input narrow"
                type="number"
                value={node.value ?? 0}
                onChange={(e) =>
                  patch((n) => {
                    n.value = Math.round(Number(e.target.value) || 0);
                  })
                }
              />
            </Row>
          ) : null}
          {node.spin ? (
            <Row label="底限 / 上限" hint="数值框的取值范围：自动方式点箭头时夹取，手输也会夹（手动方式仅供业务参考）">
              <span className="pair">
                <input
                  className="input narrow"
                  type="number"
                  value={node.min ?? 0}
                  onChange={(e) =>
                    patch((n) => {
                      n.min = Math.round(Number(e.target.value) || 0);
                    })
                  }
                />
                <input
                  className="input narrow"
                  type="number"
                  value={node.max ?? 100}
                  onChange={(e) =>
                    patch((n) => {
                      n.max = Math.round(Number(e.target.value) || 0);
                    })
                  }
                />
              </span>
            </Row>
          ) : null}
          <Row label="转换方式" hint="转成大写 / 小写：Windows 在输入时和写入文本时都会转（初始文字也会被转）">
            <select
              className="input"
              value={String(node.conv ?? 0)}
              onChange={(e) =>
                patch((n) => {
                  const v = Number(e.target.value) || 0;
                  if (v) n.conv = v;
                  else delete n.conv;
                })
              }
            >
              <option value="0">无</option>
              <option value="1">大写转小写</option>
              <option value="2">小写转大写</option>
            </select>
          </Row>
          <p className="muted small">
            对齐方式在下面「字体与颜色」区的「对齐」里（左 / 居中 / 右）。
          </p>
        </>
      ) : null}

      {node.type === "button" || node.type === "iconbutton" ? (
        <>
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
            <button className="mini" title="选择图片文件" onClick={pickImage}>
              更换
            </button>
            <button
              className="mini"
              title="清除图片"
              onClick={() => patch((n) => { n.src = undefined; })}
            >
              清除
            </button>
          </Row>
          <p className="muted small">
            {node.type === "iconbutton"
              ? "图标等比缩放居中显示（只缩不放，四周各留 4px）；路径相对 .cgui 所在目录，支持 png / jpg / bmp / gif。"
              : "图片拉伸铺满按钮作背景，文字画在图上；路径相对 .cgui 所在目录，支持 png / jpg / bmp / gif。"}
          </p>
        </>
      ) : null}
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
        <button
          className="mini"
          title="展开选择字体"
          onClick={() => setFontOpen(!fontOpen)}
        >
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
                    className={`fp-item${(style.font ?? "") === f.value ? " on" : ""}`}
                    onClick={() =>
                      patch((n) => {
                        n.style = { ...(n.style ?? {}), font: f.value };
                      })
                    }
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
                    className={`fp-item${(style.size ?? 10) === s ? " on" : ""}`}
                    onClick={() =>
                      patch((n) => {
                        n.style = { ...(n.style ?? {}), size: s };
                      })
                    }
                  >
                    {s}
                  </button>
                ))}
              </div>
            </div>
            <div className="fp-col">
              <div className="fp-head">字形</div>
              <div className="fp-list fp-sizes">
                <button
                  className={`fp-item${!style.bold ? " on" : ""}`}
                  onClick={() =>
                    patch((n) => {
                      if (n.style) delete n.style.bold;
                    })
                  }
                >
                  常规
                </button>
                <button
                  className={`fp-item${style.bold ? " on" : ""}`}
                  onClick={() =>
                    patch((n) => {
                      n.style = { ...(n.style ?? {}), bold: true };
                    })
                  }
                >
                  粗体
                </button>
              </div>
            </div>
          </div>
          <div
            className="fp-preview"
            style={{
              fontFamily: `'${style.font || "Microsoft YaHei UI"}', sans-serif`,
              fontSize: `${Math.max(12, (style.size ?? 10) * 1.5)}px`,
              fontWeight: style.bold ? 700 : 400,
            }}
          >
            示例：按钮 Button 123
          </div>
          <p className="muted small">
            点一下立即生效；字号填 0 表示跟随系统 UI 字体。粗体也可以在下面的「字号」行勾选。
          </p>
        </div>
      ) : null}
      <Row label="字号">
        <input
          className="input"
          type="number"
          value={style.size ?? 10}
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
      <Row label={node.type === "led" ? "亮灯颜色" : node.type === "meter" ? "主色" : "文字色"}>
        <input
          className="input color"
          type="color"
          value={style.fg ?? (node.type === "led" ? "#1b6e33" : node.type === "meter" ? "#6750a4" : "#1d1b20")}
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
          {node.type === "led" ? "恢复缺省绿" : "跟随系统"}
        </button>
      </Row>
      {transparentBg ? (
        <>
          <Row label="背景">
            <label className="check-inline">
              <input
                type="checkbox"
                checked={!style.bg}
                onChange={(e) =>
                  patch((n) => {
                    if (e.target.checked) {
                      /* 透明 = 不写 bg，运行时不画底色，透出窗口底色 */
                      if (n.style) delete n.style.bg;
                    } else if (!n.style?.bg) {
                      n.style = { ...(n.style ?? {}), bg: "#ffffff" };
                    }
                  })
                }
              />
              透明（不画底色）
            </label>
          </Row>
          {style.bg ? (
            <Row label="背景色">
              <input
                className="input color"
                type="color"
                value={style.bg}
                onChange={(e) =>
                  patch((n) => {
                    n.style = { ...(n.style ?? {}), bg: e.target.value };
                  })
                }
              />
            </Row>
          ) : null}
        </>
      ) : (
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
      )}
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
      <Row label="圆角" hint="自绘控件的圆角半径（像素）：按钮 / 复选框 / 开关 / 输入框 / 卡片 / 外形框等用得上；填 0 或清空 = 不写该字段、用控件缺省">
        <input
          className="input narrow"
          type="number"
          min={0}
          value={style.radius ?? 0}
          onChange={(e) =>
            patch((n) => {
              const v = Math.round(Number(e.target.value) || 0);
              n.style = { ...(n.style ?? {}), radius: v > 0 ? v : undefined };
            })
          }
        />
        <span className="muted small">px，0 = 缺省</span>
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
      {node.type === "splitter" ? (
        <>
          <Row label="方向">
            <select
              className="input"
              value={node.format ?? "col"}
              onChange={(e) => patch((n) => { n.format = e.target.value; })}
            >
              <option value="col">左右分栏（竖条，6px 宽）</option>
              <option value="row">上下分行（横条，6px 高）</option>
            </select>
          </Row>
          <p className="muted small">
            拖它 = 撑开它<b>前面那一块</b>，它之后的控件整体让位，同一容器里<b>最后一块</b>吃掉余量
            （反过来缩水）。所以「日志区 / 分隔条 / 发送区」这种两段布局，拖中间那根就是调比例 ——
            不需要布局引擎。两侧最小 16px，拖完发一次 <code>release</code> 事件（适合存布局）。
          </p>
          <p className="muted small">
            摆放要求：<b>前一块 → 分隔条 → 后一块</b> 顺序相邻，且最后一根之后必须还有一块；
            竖条把高度铺满、横条把宽度铺满（坐标是绝对像素）。
          </p>
        </>
      ) : null}
      {node.type === "terminal" ? (
        <>
          <Row label="显示">
            <select
              className="input"
              value={node.format ?? "ascii"}
              onChange={(e) => patch((n) => { n.format = e.target.value; })}
            >
              <option value="ascii">文本（ASCII / UTF-8）</option>
              <option value="hex">十六进制（HEX）</option>
            </select>
          </Row>
          <Row label="时间戳">
            <label className="check-inline">
              <input
                type="checkbox"
                checked={node.checked === true}
                onChange={(e) => patch((n) => { n.checked = e.target.checked; })}
              />
              每行前面带 [时:分:秒]
            </label>
          </Row>
          <Row label="最多行数">
            <input
              className="input narrow"
              type="number"
              min={0}
              step={100}
              value={node.value ?? 500}
              onChange={(e) => patch((n) => { n.value = Math.max(0, Number(e.target.value) || 0); })}
            />
            <span className="muted small">超了从最老的丢；0 = 用默认 500</span>
          </Row>
          <p className="muted small">
            只读的日志视图：业务用 <code>cg_term_write(w, CG_TERM_RX/TX/SYS, buf, len)</code> 灌内容，
            <b>一行一次调用</b>（数据里带 <code>\r\n</code> 会自动切成多行，<code>\r</code> 丢掉）。
            RX 绿 / TX 蓝 / 自己记的灰；可以选中复制，也能 <code>cg_term_save()</code> 存成文件。
            内容由代码灌，所以这里填的「条目」只是初始内容。
          </p>
        </>
      ) : null}
      {node.type === "chart" ? (
        <>
          <Row label="绘图类型">
            <select
              className="input"
              value={node.format ?? "line"}
              onChange={(e) => patch((n) => { n.format = e.target.value; })}
            >
              <option value="line">折线</option>
              <option value="bar">柱状</option>
            </select>
          </Row>
          <Row label="窗口点数">
            <input
              className="input narrow"
              type="number"
              min={2}
              step={10}
              value={node.value ?? 120}
              onChange={(e) => patch((n) => { n.value = Math.max(2, Math.round(Number(e.target.value) || 0)); })}
            />
            <span className="muted small">每系列保留多少个点，满了丢最老的</span>
          </Row>
          <Row label="Y 轴范围">
            <input
              className="input narrow"
              type="number"
              value={node.min ?? 0}
              onChange={(e) => patch((n) => { n.min = Math.round(Number(e.target.value) || 0); })}
            />
            <span className="muted small">到</span>
            <input
              className="input narrow"
              type="number"
              value={node.max ?? 100}
              onChange={(e) => patch((n) => { n.max = Math.round(Number(e.target.value) || 0); })}
            />
          </Row>
          <p className="muted small">
            业务按采样节奏推点：<code>cg_chart_push(w, 系列号, 值)</code> 或
            <code>cg_chart_push_all(w, 数组, 个数)</code>（建议放在 <code>cg_set_timer</code> 里，<b>别开线程</b>）。
            系列在下面的「系列名」里一行一个，颜色按调色板自动分配。
            超出 Y 范围的值按边界裁剪后画，不会画到框外；点数超过绘图区像素宽度时按像素列取 min/max
            画竖线 —— <b>不抽点</b>，所以尖峰不会被抽掉。
          </p>
        </>
      ) : null}
      {node.type === "led" ? (
        <>
          <Row label="状态">
            <select
              className="input"
              value={node.value ?? 0}
              onChange={(e) => patch((n) => { n.value = Number(e.target.value); })}
            >
              <option value={0}>灭</option>
              <option value={1}>亮</option>
              <option value={2}>闪</option>
            </select>
          </Row>
          <Row label="形状">
            <select
              className="input"
              value={node.format ?? "round"}
              onChange={(e) => patch((n) => { n.format = e.target.value; })}
            >
              <option value="round">圆</option>
              <option value="square">方</option>
            </select>
          </Row>
          {/* 周期只在「闪」时才有意义：别的状态显示出来只会让人以为它也在起作用 */}
          {(node.value ?? 0) === 2 ? (
            <Row label="闪烁周期">
              <input
                className="input narrow"
                type="number"
                min={100}
                step={100}
                value={node.period ?? 500}
                onChange={(e) =>
                  patch((n) => {
                    /* 口径跟运行时走：`cg_led_lit()` / LedProc 都是 "period >= 100 用 period，
                       否则按 500 算"。画布这边也写成 500，同一个字段才不会出现两条口径 ——
                       手写进 .cgui 的 <100 值不在这里改写（coerce 宽容原则，运行时照样按 500 闪） */
                    const v = Math.round(Number(e.target.value) || 500);
                    n.period = v < 100 ? 500 : v;
                  })
                }
              />
              <span className="muted small">毫秒；小于 100 按 500 算（再快看不出在闪）</span>
            </Row>
          ) : null}
          <p className="muted small">
            <code>cg_led_set(w, 0/1/2)</code> 改状态：0 灭 / 1 亮 / 2 闪（越界夹到 0..2）；
            <code>cg_led_set_color(w, rgb)</code> 改灯色，对应上面的「亮灯颜色」。
            「闪」的节奏由 <code>period</code> 毫秒决定。<b>纯显示控件，没有事件</b>。
            文字留空时灯自己占满整个矩形。
          </p>
        </>
      ) : null}
      {node.type === "meter" ? (
        <>
          <Row label="样式">
            <select
              className="input"
              value={node.format ?? "arc"}
              onChange={(e) => patch((n) => { n.format = e.target.value; })}
            >
              <option value="arc">圆弧 270°（缺口朝下）</option>
              <option value="half">半圆 180°</option>
            </select>
          </Row>
          <p className="muted small">
            <code>cg_meter_set(w, value)</code> 改当前值、<code>cg_meter_set_range(w, min, max)</code> 改量程；
            <b>超量程的值按边界夹取</b>（和滑块同一套 min/max 语义，上面「值 / 范围」三行就是它）。
            圆弧与指针的颜色取上面的「主色」，缺省 <code>#6750A4</code>。
            纯显示控件，没有事件。
          </p>
        </>
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

      <Row label="绑定">
        <select
          className="input"
          value={node.bind?.src ?? ""}
          onChange={(e) =>
            patch((n) => {
              const src = e.target.value;
              if (!src) {
                n.bind = undefined;
                return;
              }
              n.bind = { ...(n.bind ?? { src }), src };
            })
          }
        >
          <option value="">不绑定</option>
          {bindIds.map((id) => (
            <option key={id} value={id}>
              {id}
            </option>
          ))}
        </select>
      </Row>
      {node.bind?.src ? (
        <>
          <Row label="">
            <label className="check-inline">
              <input
                type="checkbox"
                checked={node.bind.two === true}
                onChange={(e) =>
                  patch((n) => {
                    if (n.bind) n.bind = { ...n.bind, two: e.target.checked || undefined };
                  })
                }
              />
              双向（本控件也能改源）
            </label>
          </Row>
          <p className="muted small">
            本控件的{info?.checklist ? "勾选状态" : info?.ranged || (info?.listLike && node.type !== "statusbar") ? "值" : "显示内容"}
            跟着 <code>{node.bind.src}</code> 走。
            {info?.checklist || info?.ranged || (info?.listLike && node.type !== "statusbar")
              ? ""
              : "源是输入框时显示它的文字，否则显示它的数值。"}
            {node.bind.two ? "双向：用户改这里也会推回源。" : "单向：只有源变化时才同步。"}
            {" 同步不会触发 change 回调。"}
          </p>
        </>
      ) : null}

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

      {node.type === "tabs" || node.type === "listbox" || node.type === "combo" ? (
        <Row
          label={node.type === "tabs" ? "当前页" : "初始选中"}
          hint={node.type === "tabs" ? undefined : "运行时启动后默认选中第几项（0 起）；选项来自下面的「条目」"}
        >
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
                {i + 1}. {t || (node.type === "tabs" ? `页面 ${i + 1}` : `第 ${i + 1} 项`)}
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
            node.type === "statusbar" ? "分段文字"
              : node.type === "tabs" ? "页签文字"
              : node.type === "toolbar" ? "按钮文字"
              : node.type === "chart" ? "系列名"
              : "条目"
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
