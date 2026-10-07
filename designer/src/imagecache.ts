/* imagecache.ts -- 画布预览图片的那点缓存
 *
 * 画布要"所见即所得"地画出 button / iconbutton / image 引用的真图。图片由主进程读出来、
 * 以 dataURL 交给渲染端（dev 模式页面是 http://localhost，读不了 file:// 的图）。
 *
 * 三件事：
 *   ① 同一个 src 只读一次；
 *   ② "读不到"也要记住，别每次渲染都重试（但换文件后要允许重试）；
 *   ③ 换 .cgui 文件后**同名相对路径要重新读** —— 所以缓存键是「当前文件 + src」而不是光 src。
 *
 * 状态口径（WidgetView 的 useImage 用）：
 *   undefined = 还没读到（显示文件名占位）
 *   null      = 明确读不到 / 太大（显示"读不到"）
 *   string    = dataURL
 */
import { useEffect, useState } from "react";
import { getBridge } from "./bridge";

/** 当前 .cgui 的路径（App 在 filePath 变化时同步过来）：相对路径的基准目录就是它的目录 */
let base = "";
/** 键 = `${base}\u0000${src}`；值 = dataURL 或 null（null = 读不到/太大，不再重试） */
const cache = new Map<string, string | null>();
const inflight = new Set<string>();
const listeners = new Set<() => void>();

const keyOf = (src: string) => `${base}\u0000${src}`;

/** 看起来是绝对路径 / UNC：这种不依赖基准目录 */
const isAbsoluteish = (src: string) => /^[a-zA-Z]:[\\/]/.test(src) || src.startsWith("\\\\") || src.startsWith("/");

/** 换文件时调一次。缓存键里带 base，所以老条目自然失效；这里只负责通知重渲染。 */
export function setImageBase(filePath: string) {
  const next = filePath ?? "";
  if (next === base) return;
  base = next;
  for (const fn of listeners) fn();
}

export function subscribeImages(fn: () => void) {
  listeners.add(fn);
  return () => {
    listeners.delete(fn);
  };
}

/** 已经读到的结果（undefined = 还没读过） */
export function peekImage(src: string | undefined): string | null | undefined {
  if (!src) return undefined;
  return cache.get(keyOf(src));
}

/** 预览图的三态：undefined = 还没读到（显示文件名占位）/ null = 读不到或太大 / string = dataURL */
export type Preview = string | null | undefined;

/** React 侧取预览图的钩子（WidgetView 的三种控件与 Canvas 的标题栏图标共用）。
 *  读到图后缓存会广播，这里靠 bump 触发重渲染；没读到的（含还没读过）都返回 undefined。 */
export function useImage(src: string | undefined): Preview {
  const [version, bump] = useState(0);
  useEffect(() => {
    if (!src) return;
    return subscribeImages(() => bump((n) => n + 1));
  }, [src]);
  /* 依赖里必须带 `version`：**子组件的 effect 先于父组件跑**，首次渲染时 App 还没来得及把
     基准目录交给缓存，requestImage 会带着空 base 直接返回；等 App 那个 effect 补上 base 时，
     `src` 并没有变 —— 只依赖 src 的话就再也不会重试，图片永远停在"没读到"。
     带上 version 后：base 变了 → 广播 → version+1 → 这里重跑一次 → 这次带上正确的 base。
     （已读完的 src 会命中缓存直接返回，所以不会变成循环。） */
  useEffect(() => {
    if (src) requestImage(src);
  }, [src, version]);
  return peekImage(src);
}

/** 请求读图。重复调用只读一次；读到后通知所有订阅者重渲染。 */
export function requestImage(src: string) {
  const k = keyOf(src);
  if (cache.has(k) || inflight.has(k)) return;
  /* 文档还没保存过 = 没有基准目录：相对路径没法解析（主进程也会失败），
     干脆不发请求 —— 画布上落在"显示文件名"那个中性状态，等保存后 base 变了自然会重读。
     绝对路径不受影响。 */
  if (!base && !isAbsoluteish(src)) return;
  inflight.add(k);
  void getBridge()
    .readImage({ src })
    .then((r) => cache.set(k, r.ok && r.dataUrl ? r.dataUrl : null))
    .catch(() => cache.set(k, null))
    .finally(() => {
      inflight.delete(k);
      for (const fn of listeners) fn();
    });
}
