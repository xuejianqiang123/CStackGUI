/* Inspector/TypeSection.tsx -- 类型专属属性
 *
 * 只有某一种控件才有的字段：时钟周期、颜色选择、网页地址、外形框形状、分隔条方向、
 * 终端显示与最多行数、图表绘图类型与点数、状态灯状态、仪表盘样式、表格多选与勾选列。
 * 每个分支自带 node.type 判定，不属于自己的类型直接不渲染。
 *
 * 2026-10-18（G5）：数值与勾选字段改用 PanelUi 的 NumField / SelectField / CheckField。
 * **四类刻意保留手写**（改了会丢语义或改坏口径，见 canvas-longterm-roadmap.md §G5）：
 *   1. **字符串 format 下拉**（shape/splitter/terminal/chart/led/meter）：SelectField 是
 *      数值口径（Number 解析 + omitOnZero），这里的 format 是 "roundrect"/"col"/"line" 这类
 *      字符串，且赋值体只有一行 —— 没有"解析"重复可消，套原语反而要绕。**只把 option 文案
 *      集中成常量**（改标签只动一处），元素本身照旧。
 *   2. **colorpicker**：整数 ↔ #hex 往返是它独有的，没有通用形态。
 *   3. **dialog 居中**：反语义（勾着 = 写 undefined，不勾才写 false），不是普通布尔。
 *   4. **LED 闪烁周期**：<100 按 500 算（跟运行时的 cg_led_lit 同一口径），不是简单夹取。
 */
import type { CgNode } from "../../model";
import { CheckField, NumField, Row, SelectField, type Opt, type Patch } from "./PanelUi";

export type TypeSectionProps = {
  node: CgNode;
  patch: Patch;
};

/* 字符串 format 的选项集中放（改文案只动这一处；元素仍用原生 select，见文件头第 1 条） */
const SHAPE_OPTS: readonly Opt[] = [
  { value: "rect", label: "矩形" },
  { value: "roundrect", label: "圆角矩形" },
  { value: "ellipse", label: "椭圆" },
];
const SPLITTER_OPTS: readonly Opt[] = [
  { value: "col", label: "左右分栏（竖条，6px 宽）" },
  { value: "row", label: "上下分行（横条，6px 高）" },
];
const TERM_FORMAT_OPTS: readonly Opt[] = [
  { value: "ascii", label: "文本（ASCII / UTF-8）" },
  { value: "hex", label: "十六进制（HEX）" },
];
const CHART_TYPE_OPTS: readonly Opt[] = [
  { value: "line", label: "折线" },
  { value: "bar", label: "柱状" },
];
const LED_SHAPE_OPTS: readonly Opt[] = [
  { value: "round", label: "圆" },
  { value: "square", label: "方" },
];
const METER_OPTS: readonly Opt[] = [
  { value: "arc", label: "圆弧 270°（缺口朝下）" },
  { value: "half", label: "半圆 180°" },
];
/* 数值下拉（SelectField 口径）：串口参数永远是数字、且一定写入（没有"0 就删"这回事） */
const BAUD_OPTS: readonly Opt[] = [
  { value: "9600", label: "9600" },
  { value: "19200", label: "19200" },
  { value: "38400", label: "38400" },
  { value: "57600", label: "57600" },
  { value: "115200", label: "115200" },
  { value: "230400", label: "230400" },
  { value: "460800", label: "460800" },
  { value: "921600", label: "921600" },
];
const BITS_OPTS: readonly Opt[] = [
  { value: "5", label: "5" },
  { value: "6", label: "6" },
  { value: "7", label: "7" },
  { value: "8", label: "8" },
];
const STOP_OPTS: readonly Opt[] = [
  { value: "1", label: "1" },
  { value: "2", label: "2" },
];
const PARITY_OPTS: readonly Opt[] = [
  { value: "0", label: "无" },
  { value: "1", label: "奇" },
  { value: "2", label: "偶" },
];
const LED_STATE_OPTS: readonly Opt[] = [
  { value: "0", label: "灭" },
  { value: "1", label: "亮" },
  { value: "2", label: "闪" },
];

export default function TypeSection({ node, patch }: TypeSectionProps) {
  return (
    <>
      {node.type === "timer" ? (
        <Row label="周期">
          <NumField
            className="input"
            value={node.period}
            min={0}
            step={100}
            round={false}
            onChange={(v) =>
              patch((n) => {
                n.period = v ?? 0;
              })
            }
          />
          <span className="muted small">毫秒，0 = 停</span>
        </Row>
      ) : null}
      {node.type === "dialog" ? (
        /* 多窗体：对话框是独立窗口，"打开时居中"是它为数不多的窗口级属性。
           缺省居中（勾着）；不勾则运行时按 rect.x/y 摆 —— 屏幕坐标，与窗口属性的 x/y 同一口径，
           所以画布上拖到哪儿，运行时就出现在哪儿。
           （手写：勾着 = undefined、不勾 = false，不是普通布尔） */
        <Row
          label="打开时居中"
          hint="模态打开时居中于主窗口。不勾选则用 左边/顶边（屏幕坐标，与窗口属性的 x/y 同一口径）"
        >
          <input
            type="checkbox"
            checked={node.center !== false}
            onChange={(e) =>
              patch((n) => {
                n.center = e.target.checked ? undefined : false;
              })
            }
          />
        </Row>
      ) : null}
      {node.type === "serial" ? (
        <>
          <Row label="端口名">
            <input
              className="input mono"
              value={node.port ?? "COM1"}
              placeholder="COM1"
              spellCheck={false}
              onChange={(e) =>
                patch((n) => {
                  n.port = e.target.value || undefined;
                })
              }
            />
          </Row>
          <Row label="波特率">
            <SelectField
              value={node.baud ?? 9600}
              options={BAUD_OPTS}
              onChange={(v) =>
                patch((n) => {
                  n.baud = v ?? 9600;
                })
              }
            />
          </Row>
          <Row label="数据位">
            <SelectField
              value={node.bits ?? 8}
              options={BITS_OPTS}
              onChange={(v) =>
                patch((n) => {
                  n.bits = v ?? 8;
                })
              }
            />
          </Row>
          <Row label="停止位">
            <SelectField
              value={node.stop ?? 1}
              options={STOP_OPTS}
              onChange={(v) =>
                patch((n) => {
                  n.stop = v ?? 1;
                })
              }
            />
          </Row>
          <Row label="校验">
            <SelectField
              value={node.parity ?? 0}
              options={PARITY_OPTS}
              onChange={(v) =>
                patch((n) => {
                  n.parity = v ?? 0;
                })
              }
            />
          </Row>
          <Row label="自动启动">
            <CheckField
              checked={node.auto === true}
              mode="omitFalse"
              label="启动时自动打开串口"
              onChange={(v) =>
                patch((n) => {
                  n.auto = v || undefined;
                })
              }
            />
          </Row>
          <p className="muted small">
            无界面控件：运行时不建窗口，收到数据就派发 <code>arrive</code> 事件，
            数据落到哪个控件由你的代码决定（用 <code>cg_serial_read</code> 等 API 取走）。
          </p>
        </>
      ) : null}
      {node.type === "colorpicker" ? (
        <Row label="颜色">
          {/* 手写：整数 ↔ #hex 往返，没有通用形态 */}
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
            {SHAPE_OPTS.map((o) => (
              <option key={o.value} value={o.value}>
                {o.label}
              </option>
            ))}
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
              {SPLITTER_OPTS.map((o) => (
                <option key={o.value} value={o.value}>
                  {o.label}
                </option>
              ))}
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
              {TERM_FORMAT_OPTS.map((o) => (
                <option key={o.value} value={o.value}>
                  {o.label}
                </option>
              ))}
            </select>
          </Row>
          <Row label="时间戳">
            <CheckField
              checked={node.checked === true}
              label="每行前面带 [时:分:秒]"
              onChange={(v) =>
                patch((n) => {
                  n.checked = v ?? false;
                })
              }
            />
          </Row>
          <Row label="最多行数">
            <NumField
              value={node.value ?? 500}
              min={0}
              step={100}
              round={false}
              onChange={(v) =>
                patch((n) => {
                  n.value = v ?? 0;
                })
              }
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
              {CHART_TYPE_OPTS.map((o) => (
                <option key={o.value} value={o.value}>
                  {o.label}
                </option>
              ))}
            </select>
          </Row>
          <Row label="窗口点数">
            <NumField
              value={node.value ?? 120}
              min={2}
              step={10}
              onChange={(v) =>
                patch((n) => {
                  n.value = v ?? 120;
                })
              }
            />
            <span className="muted small">每系列保留多少个点，满了丢最老的</span>
          </Row>
          <Row label="Y 轴范围">
            <NumField
              value={node.min}
              onChange={(v) =>
                patch((n) => {
                  n.min = v ?? 0;
                })
              }
            />
            <span className="muted small">到</span>
            <NumField
              value={node.max ?? 100}
              onChange={(v) =>
                patch((n) => {
                  n.max = v ?? 0;
                })
              }
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
            <SelectField
              value={node.value ?? 0}
              options={LED_STATE_OPTS}
              onChange={(v) =>
                patch((n) => {
                  n.value = v ?? 0;
                })
              }
            />
          </Row>
          <Row label="形状">
            <select
              className="input"
              value={node.format ?? "round"}
              onChange={(e) => patch((n) => { n.format = e.target.value; })}
            >
              {LED_SHAPE_OPTS.map((o) => (
                <option key={o.value} value={o.value}>
                  {o.label}
                </option>
              ))}
            </select>
          </Row>
          {/* 周期只在「闪」时才有意义：别的状态显示出来只会让人以为它也在起作用
              （手写：<100 按 500 算，跟运行时的 cg_led_lit 同一口径，不是简单夹取） */}
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
              {METER_OPTS.map((o) => (
                <option key={o.value} value={o.value}>
                  {o.label}
                </option>
              ))}
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
            <CheckField
              checked={!!node.multi}
              mode="omitFalse"
              label="允许同时选中多行"
              onChange={(v) =>
                patch((n) => {
                  n.multi = v || undefined;
                })
              }
            />
          </Row>
          <Row label="勾选列">
            <CheckField
              checked={!!node.check}
              mode="omitFalse"
              label="显示行首复选框（check 事件）"
              onChange={(v) =>
                patch((n) => {
                  n.check = v || undefined;
                })
              }
            />
          </Row>
        </>
      ) : null}
    </>
  );
}
