/* model.ts -- .cgui 文档模型（对外统一入口 / re-export 桶）
 *
 * 模型层已按职责拆到 model/ 目录：
 *   model/types.ts      文档类型总集（最底层，零运行时依赖）
 *   model/events.ts     事件名总表 + 中文标签 + 每类控件支持的事件
 *   model/kinds.ts      控件类型登记表（默认尺寸 / 容器判定）
 *   model/style.ts      命名样式（styles{}）与控件 style 三种形态的公共工具
 *   model/textcodec.ts  结构化内容 ↔ 多行文本 的互转
 *   model/factory.ts    造节点与新工程模板
 *   model/tree.ts       控件树的遍历 / 增删 / 命中测试
 *   model/parse.ts      读入 .cgui（含脏数据容错）+ 克隆
 *   model/serialize.ts  写出 .cgui
 *   model/math.ts       夹取 / 吸附
 *   model/store.ts      文档 store（doc 唯一真相 + 快照栈撤销；React 无关，可 headless 测试）
 *   model/ops.ts        文档命令层（可序列化命令对象 + 确定性 apply；协同的架构前置）
 * 本文件只把上面 11 个文件的导出面原样转出，外部一律照旧 `from "./model"`。
 *
 * ⚠ 下面这些相对说明符**必须带 `.js` 后缀**（`model/` 里各文件之间同理）：
 *   本目录会被 tools\check-format.bat 用 `tsc --module es2022` 编到 build\modeljs\ 再交给
 *   Node 跑往返门禁，而 **tsc 不会改写 import 说明符、Node ESM 也不会解析无后缀路径** ——
 *   少了 `.js` 就是 ERR_MODULE_NOT_FOUND（Vite 能自己解析无后缀，所以画布构建看不见这个问题）。
 *   带 `.js` 是 TS-ESM 的标准写法：tsc 与 Vite 仍解析到 .ts 源文件，Node 也能解析到编译产物。
 *
 * 这里的类型必须和 runtime/src/core/cg_doc.c 的解析逻辑一一对应：
 * 改这里就要同步改那边，否则「画布能存、运行时读不了」。
 */

export * from "./model/events.js";
export * from "./model/types.js";
export * from "./model/kinds.js";
export * from "./model/style.js";
export * from "./model/refs.js";
export * from "./model/textcodec.js";
export * from "./model/factory.js";
export * from "./model/tree.js";
export * from "./model/align.js";
export * from "./model/parse.js";
export * from "./model/serialize.js";
export * from "./model/math.js";
export * from "./model/store.js";
export * from "./model/ops.js";
export * from "./model/layoutsim.js";
export * from "./model/tableimport.js";
