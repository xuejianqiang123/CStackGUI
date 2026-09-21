/* EchartsView.tsx -- 画布上用真实的 echarts 渲染图表（WYSIWYG 预览）
 *
 * 设计期直接在 Electron（Chromium）里用 echarts 包渲染，和运行时 WebView2 里
 * 看到的是同一套 ECharts，所见即所得。option 是 model.ts 里的 ECharts 配置对象。
 */
import { useEffect, useRef } from "react";
import * as echarts from "echarts";

type Props = {
  option?: unknown;
  theme?: string;
};

/** 把任意值规整成 echarts 能吃的 option（容错：不是对象就给空配置） */
function asOption(raw: unknown): echarts.EChartsOption | Record<string, unknown> {
  if (raw && typeof raw === "object") return raw as echarts.EChartsOption;
  return {};
}

export default function EchartsView({ option, theme }: Props) {
  const hostRef = useRef<HTMLDivElement>(null);
  const chartRef = useRef<echarts.ECharts | null>(null);

  /* 初始化 / 销毁：一个控件只建一个实例 */
  useEffect(() => {
    if (!hostRef.current) return;
    const chart = echarts.init(hostRef.current, theme || undefined, { renderer: "canvas" });
    chartRef.current = chart;
    const ro = new ResizeObserver(() => chart.resize());
    ro.observe(hostRef.current);
    return () => {
      ro.disconnect();
      chart.dispose();
      chartRef.current = null;
    };
  }, [theme]);

  /* option 变化就重设 */
  useEffect(() => {
    const chart = chartRef.current;
    if (!chart) return;
    try {
      chart.setOption(asOption(option), true);
    } catch {
      /* 配置非法：清成空，不崩画布 */
      chart.clear();
    }
  }, [option]);

  return <div className="ctl-echarts" ref={hostRef} style={{ width: "100%", height: "100%" }} />;
}
