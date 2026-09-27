/* model/style.ts -- 命名样式（顶层 styles{}）与控件 style 三种形态的公共工具
 *
 * 契约：.codebuddy/plans/named-styles-dark.md §1/§2。三件事：
 *   styles{}   : 样式名 -> { font / size / bold / fg / bg / radius / align }
 *                （名字非空、后写覆盖前写；一条样式可以一个字段都没有，只要还被引用着）
 *   控件 style : 内联对象 / "名字" / { "use": "名字", ...局部覆盖 }  —— 三种形态都要支持
 *   合并规则   : 先命名样式打底，再叠本控件**写了的**字段（内联优先）；
 *                use 指向不存在的名字 -> 当没有 use（只用内联字段），不报错
 *
 * 这里只放"与读/写流程无关"的纯函数：
 *   - 读入校验（coerceStyle*）在 parse.ts，写出清理（cleanStyle*）在 serialize.ts，两者成对维护；
 *   - 本文件被 parse/serialize/StyleSection/StyleDialog/WidgetView 共用。
 *
 * 依赖：只依赖 model/types.ts（零运行时依赖，见该文件顶部说明）。
 */
import { isSafeActionName, type CgDoc, type CgNode, type CgStyle, type CgStyleValue } from "./types.js";

/** 样式字段的规范顺序（读入与写出都用它 —— 往返稳定、文件可读）。 */
export const STYLE_KEYS = ["font", "size", "bold", "fg", "bg", "align", "radius"] as const;
export type CgStyleKey = (typeof STYLE_KEYS)[number];

/** 能不能当样式名（styles{} 的键）：和动作名一个口径（空 / __proto__ 这类一律非法）。 */
export const isSafeStyleName = isSafeActionName;

/** 取 style 引用的命名样式名：纯字符串形态 / 对象带 use；否则 ""。 */
export function styleUseName(style: CgStyleValue | undefined): string {
  if (typeof style === "string") return style.trim();
  if (style && typeof style === "object" && typeof style.use === "string") return style.use.trim();
  return "";
}

/** 本控件**自己写了**的字段（对象形态里除 use 外的键，按规范顺序）。 */
export function styleOverrideKeys(style: CgStyleValue | undefined): CgStyleKey[] {
  if (!style || typeof style !== "object") return [];
  return STYLE_KEYS.filter((k) => style[k] !== undefined);
}

/** 只取内联字段（画布预览 / 合并用）：字符串形态返回 {}。
 *  画布**不解析**命名样式做预览（契约 §4），所以预览只看这几个字段。 */
export function inlineStyle(style: CgStyleValue | undefined): CgStyle {
  if (!style || typeof style !== "object") return {};
  const out = {} as Record<string, unknown>;
  for (const k of STYLE_KEYS) {
    const v = style[k];
    if (v !== undefined) out[k] = v;
  }
  return out as CgStyle;
}

/** 解析结果：最终生效的字段 + 引用状态（属性面板 / 探针用）。 */
export type ResolvedStyle = {
  /** 命名样式打底 + 本控件覆盖后的有效字段 */
  effective: CgStyle;
  /** 引用的命名样式名（"" = 没引用） */
  use: string;
  /** 确实引用了名字、但 styles{} 里没有它（运行时当"没有 use"处理，不报错） */
  missing: boolean;
  /** 本控件自己覆盖的字段名 */
  overrides: CgStyleKey[];
};

/** 按契约 §2 的合并规则算一遍：先命名样式，后内联字段（内联优先）。
 *  与运行时 resolve_named_styles() 是同一套语义（以字段是否**写了**为准，不看值是否等于缺省）。 */
export function resolveStyle(doc: CgDoc | undefined, style: CgStyleValue | undefined): ResolvedStyle {
  const use = styleUseName(style);
  const overrides = styleOverrideKeys(style);
  const named =
    use && doc?.styles && Object.prototype.hasOwnProperty.call(doc.styles, use)
      ? doc.styles[use]
      : undefined;

  const effective = {} as Record<string, unknown>;
  if (named) {
    for (const k of STYLE_KEYS) {
      const v = named[k];
      if (v !== undefined) effective[k] = v;
    }
  }
  if (style && typeof style === "object") {
    for (const k of overrides) {
      const v = style[k];
      if (v !== undefined) effective[k] = v;
    }
  }
  return { effective: effective as CgStyle, use, missing: !!use && !named, overrides };
}

/** 结构收敛成三种形态之一：只有引用 -> 纯命名字符串；只有字段 -> 内联对象；都没有 -> undefined。
 *  编辑路径（属性面板 / 样式对话框）用它保持 doc 干净（"缺省不写"）；
 *  **读/写 .cgui 时不走这里** —— 那边要原样保留手写的形态（往返恒等，见 serialize.ts）。 */
export function normalizeStyle(style: CgStyleValue | undefined): CgStyleValue | undefined {
  if (style === undefined) return undefined;
  const use = styleUseName(style);
  const fields = inlineStyle(style);
  const hasFields = Object.keys(fields).length > 0;
  if (!use) return hasFields ? fields : undefined;
  if (!hasFields) return use;
  return { use, ...fields };
}

/** 便捷判断：这个节点有没有写自己的 style（任何形态）。 */
export const hasOwnStyle = (node: CgNode): boolean =>
  typeof node.style === "string" ? !!node.style.trim() : !!node.style && Object.keys(node.style).length > 0;
