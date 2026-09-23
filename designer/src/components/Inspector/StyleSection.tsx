/* Inspector/StyleSection.tsx -- 「字体与颜色」整段（style.* 字段族）
 *
 * 字体（含展开的字体选择器）、字号与粗体、文字色 / 背景色（含「透明」开关）、
 * 对齐、圆角。背景那一块按控件类型分两种画法：标签/链接/分割线默认透明。
 * 字体选择器的展开状态留在 Inspector.tsx，用 fontOpen + onToggleFont 传进来。
 */
import type { CgNode } from "../../model";
import { Row, type Patch } from "./PanelUi";

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

export type StyleSectionProps = {
  node: CgNode;
  patch: Patch;
  /** 字体选择器是否展开（状态在 Inspector.tsx） */
  fontOpen: boolean;
  onToggleFont: () => void;
};

export default function StyleSection({ node, patch, fontOpen, onToggleFont }: StyleSectionProps) {
  const style = node.style ?? {};
  /* 标签 / 链接 / 分割线默认不画底色（透明）；这里给个显式开关，用户一眼能看清 */
  const transparentBg = node.type === "label" || node.type === "link" || node.type === "separator";

  return (
    <>
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
          onClick={onToggleFont}
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
      <Row
        label={node.type === "led" ? "亮灯颜色" : node.type === "meter" ? "主色" : "文字色"}
        hint={FIXED_M3_COLOR.has(node.type)
          ? "开关 / 复选框 / 单选钮 的配色由控件自绘固定（M3），运行时不读 style.fg，改了不生效"
          : undefined}
      >
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
        <Row
          label="背景色"
          hint={FIXED_M3_COLOR.has(node.type)
            ? "开关 / 复选框 / 单选钮 的底色由控件自绘固定（M3），运行时不读 style.bg，改了不生效"
            : undefined}
        >
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
      <Row label="对齐" hint="只对「标签」和「输入框」生效（运行时按这个字段给控件设对齐样式）；其它控件的文字对齐由控件自身决定，改了不生效">
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
      <Row label="圆角" hint="自绘控件的圆角半径（像素）：当前由运行时消费的只有 按钮 / 复选框 / 单选钮 / 输入框 / 卡片 / 外形框；开关、列表、表格等改了不生效。填 0 或清空 = 不写该字段、用控件缺省">
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
    </>
  );
}
