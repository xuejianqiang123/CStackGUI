/* Inspector/EditSection.tsx -- 输入框（edit）专属的输入行为
 *
 * 预设（单行/多行/数字框）、最大长度、允许多行、滚动条、边框、输入方式、调节器，
 * 以及数值框特有的「值 / 底限 / 上限」三行和转换方式。
 * 只有 edit 有这一段，其它类型直接返回 null。
 */
import { isNumEdit, type CgNode } from "../../model";
import { Row, type Patch } from "./PanelUi";

export type EditSectionProps = {
  node: CgNode;
  patch: Patch;
};

export default function EditSection({ node, patch }: EditSectionProps) {
  if (node.type !== "edit") return null;
  return (
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
  );
}
