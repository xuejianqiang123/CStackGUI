/* Inspector/EditSection.tsx -- 输入框（edit）专属的输入行为
 *
 * 预设（单行/多行/数字框）、最大长度、允许多行、滚动条、边框、输入方式、调节器，
 * 以及数值框特有的「值 / 底限 / 上限」三行和转换方式。
 * 只有 edit 有这一段，其它类型直接返回 null。
 *
 * 2026-10-18（G5）：本段原本有 5 个逐字同形的 <select> 和多处数字输入，重复的是
 * 「解析 + 夹取 + 0 就删字段」这套机械口径 —— 现在统一走 PanelUi 的 SelectField /
 * NumField / CheckField。**预设按钮组保持手写**：它一次改四个字段且带条件判断，
 * 是语义不是重复（见 canvas-longterm-roadmap.md §G5 的"必须保留手写"清单）。
 */
import { isNumEdit, type CgNode } from "../../model";
import { CheckField, NumField, Row, SelectField, type Opt, type Patch } from "./PanelUi";

export type EditSectionProps = {
  node: CgNode;
  patch: Patch;
};

/* 下拉选项集中放：改文案只动这一处，各 <select> 不再各自复制一份 <option> */
const SCROLL_OPTS: readonly Opt[] = [
  { value: "0", label: "无" },
  { value: "1", label: "横向滚动条" },
  { value: "2", label: "纵向滚动条" },
  { value: "3", label: "横向及纵向滚动条" },
];
const BORDER_OPTS: readonly Opt[] = [
  { value: "0", label: "缺省（圆角描边）" },
  { value: "1", label: "无边框" },
  { value: "2", label: "凹入式" },
  { value: "3", label: "凸出式" },
  { value: "4", label: "浅凹入式" },
  { value: "5", label: "镜框式" },
  { value: "6", label: "单线边框式" },
];
const INPUT_OPTS: readonly Opt[] = [
  { value: "0", label: "通常方式" },
  { value: "1", label: "只读方式" },
  { value: "2", label: "密码输入" },
  { value: "3", label: "整数文本输入" },
  { value: "4", label: "小数文本输入" },
  { value: "5", label: "输入字节（0~255）" },
  { value: "6", label: "输入短整数（±32767）" },
  { value: "7", label: "输入整数（±21 亿）" },
  { value: "8", label: "输入长整数" },
  { value: "9", label: "输入小数" },
  { value: "10", label: "输入双精度小数" },
  { value: "11", label: "输入日期时间" },
];
const SPIN_OPTS: readonly Opt[] = [
  { value: "0", label: "无" },
  { value: "1", label: "自动" },
  { value: "2", label: "手动" },
];
const CONV_OPTS: readonly Opt[] = [
  { value: "0", label: "无" },
  { value: "1", label: "大写转小写" },
  { value: "2", label: "小写转大写" },
];

export default function EditSection({ node, patch }: EditSectionProps) {
  if (node.type !== "edit") return null;
  return (
    <>
      <div className="insp-section">输入框</div>
      {/* 预设：以前调色板里「单行输入 / 多行输入 / 数字微调」是三个类型，合并成 edit
          之后在这儿一键切回同样的组合，省得挨个改字段。（手写：一次改四字段 + 带条件） */}
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
        <NumField
          value={node.maxlen}
          min={0}
          omitOnZero
          onChange={(v) =>
            patch((n) => {
              if (v === undefined) delete n.maxlen;
              else n.maxlen = v;
            })
          }
        />
      </Row>
      <Row label="允许多行" hint="按 Enter 变换行（多行时通常还要配竖向滚动条）">
        <CheckField
          checked={!!node.multiline}
          mode="omitFalse"
          label={<span className="muted small">Enter 变换行</span>}
          onChange={(v) =>
            patch((n) => {
              if (v === undefined) delete n.multiline;
              else n.multiline = v;
            })
          }
        />
      </Row>
      <Row label="滚动条">
        <SelectField
          value={node.scroll}
          options={SCROLL_OPTS}
          omitOnZero
          onChange={(v) =>
            patch((n) => {
              if (v === undefined) delete n.scroll;
              else n.scroll = v;
            })
          }
        />
      </Row>
      <Row label="边框" hint="缺省 = 本工程的圆角描边；其余六种和属性面板的习惯叫法一致，聚焦时一律变紫">
        <SelectField
          value={node.border}
          options={BORDER_OPTS}
          omitOnZero
          onChange={(v) =>
            patch((n) => {
              if (v === undefined) delete n.border;
              else n.border = v;
            })
          }
        />
      </Row>
      <Row label="输入方式" hint="只读 = 能选中但改不了；密码 = 显示圆点；数值/日期档位会按键过滤 + 失焦时夹回或清空">
        <SelectField
          value={node.input}
          options={INPUT_OPTS}
          omitOnZero
          onChange={(v) =>
            patch((n) => {
              if (v === undefined) delete n.input;
              else n.input = v;
            })
          }
        />
      </Row>
      <Row
        label="调节器"
        hint="右侧上下箭头，开了它就是「数值框」（值是真相、文本由它派生）：自动 = 点一下按 ±1 改值并夹到下面的底限/上限；手动 = 只发「调节钮被按下」，值由业务在回调里自己改。多行输入框没有箭头"
      >
        <SelectField
          value={node.spin}
          options={SPIN_OPTS}
          disabled={!!node.multiline}
          omitOnZero
          onChange={(v) =>
            patch((n) => {
              if (v === undefined) delete n.spin;
              else n.spin = v;
            })
          }
        />
      </Row>
      {isNumEdit(node) ? (
        <Row label="值" hint="数值框的真相：界面上显示的就是它，箭头 ±1、cg_widget_value()、双向绑定全按它走">
          <NumField
            value={node.value}
            onChange={(v) =>
              patch((n) => {
                n.value = v ?? 0;
              })
            }
          />
        </Row>
      ) : null}
      {node.spin ? (
        <Row label="底限 / 上限" hint="数值框的取值范围：自动方式点箭头时夹取，手输也会夹（手动方式仅供业务参考）">
          <span className="pair">
            <NumField
              value={node.min}
              onChange={(v) =>
                patch((n) => {
                  n.min = v ?? 0;
                })
              }
            />
            <NumField
              value={node.max ?? 100}
              onChange={(v) =>
                patch((n) => {
                  n.max = v ?? 0;
                })
              }
            />
          </span>
        </Row>
      ) : null}
      <Row label="转换方式" hint="转成大写 / 小写：Windows 在输入时和写入文本时都会转（初始文字也会被转）">
        <SelectField
          value={node.conv}
          options={CONV_OPTS}
          omitOnZero
          onChange={(v) =>
            patch((n) => {
              if (v === undefined) delete n.conv;
              else n.conv = v;
            })
          }
        />
      </Row>
      <p className="muted small">
        对齐方式在下面「字体与颜色」区的「对齐」里（左 / 居中 / 右）。
      </p>
    </>
  );
}
