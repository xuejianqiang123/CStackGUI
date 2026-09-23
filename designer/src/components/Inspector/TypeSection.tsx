/* Inspector/TypeSection.tsx -- 类型专属属性
 *
 * 只有某一种控件才有的字段：时钟周期、颜色选择、网页地址、外形框形状、分隔条方向、
 * 终端显示与最多行数、图表绘图类型与点数、状态灯状态、仪表盘样式、表格多选与勾选列。
 * 每个分支自带 node.type 判定，不属于自己的类型直接不渲染。
 */
import type { CgNode } from "../../model";
import { Row, type Patch } from "./PanelUi";

export type TypeSectionProps = {
  node: CgNode;
  patch: Patch;
};

export default function TypeSection({ node, patch }: TypeSectionProps) {
  return (
    <>
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
    </>
  );
}
