/* supportlib/types.ts -- 支持库表的结构定义与简写构造器
 *
 * 与数据文件分开：新增一个库只加数据文件，类型不跟着动。
 * 注意 c() 在原单文件里是模块私有的，拆开后三个数据文件都要用 —— 已补 export。
 */
export interface SupCmd {
  /** C 函数名（也是树的 key 与搜索主键） */
  fn: string;
  /** 中文短名，列表里当副标题 */
  name: string;
  /** 原型或宏定义，帮助卡片里展示 */
  decl: string;
  /** 一句话说明（新手看这个就够了） */
  brief: string;
  /** 参数逐条说明（可省） */
  params?: string[];
  /** 返回值说明（可省） */
  ret?: string;
  /** 复制用的调用代码（可省，缺省用 decl） */
  sample?: string;
}

export interface SupGroup {
  label: string;
  cmds: SupCmd[];
  /** 二级分类（如表格的「基本 / 行状态」、对话框的「信息框 / 打开文件」） */
  children?: SupGroup[];
}

export interface SupLib {
  key: string;
  label: string;
  hint: string;
  groups: SupGroup[];
}

/** 一行一条命令的简写构造器（保持数据表可读） */
export const c = (fn: string, name: string, decl: string, brief: string, extra: Partial<SupCmd> = {}): SupCmd => ({
  fn,
  name,
  decl,
  brief,
  ...extra,
});
