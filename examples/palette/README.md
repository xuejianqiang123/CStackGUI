# 取色与配色 palette：颜色换算、对比度、8 格调色板

> 目录 `examples\palette\`　编译 `build-app.bat palette`　运行 `examples\palette\palette.exe`　自检 `palette.exe --selftest examples\palette\palette.cgui` → 控件 19 个 / 回调 14 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
选出颜色后立刻换算成 `#RRGGBB` / `rgb()` / `hsl()` / 相对亮度 / 与白与黑的对比度，并按 WCAG 阈值回答"这块底色上该配白字还是黑字"。
8 个 `colorpicker` 当调色板色格；「加入调色板」把当前色填进第一个空位，「清空」复位；改任何一格都会把它设成当前色。「导出」把 8 格拼成 CSS 变量与纯 HEX 两份文本写进用户数据目录，另给一个「复制 HEX」按钮。

## 二、怎么用
1. 在项目根编译：`build-app.bat palette`，双击 `examples\palette\palette.exe`。
2. 点「取色」按钮（或直接点当前色块）弹系统取色器，确定后设为当前色。
3. 在上方只读框里读换算结果：HEX / rgb() / hsl() / 相对亮度 / 对白对黑对比度 / 建议字色。
4. 点「加入调色板」把当前色填进色格；点任一色格把它设为当前色；「清空」复位。
5. 点「导出到文件」写 `palette.css` 与 `palette.txt`；点「复制 HEX」把纯 HEX 列表送进剪贴板。底部终端记录每一步。
6. 命令行：`palette.exe`（自动找同名 `.cgui`）、`palette.exe xxx.cgui`、`--selftest`。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `cp_cur` | `colorpicker` | 当前色块，点它弹系统取色器 |
| `btn_pick` | `button` | 弹系统取色器，确定后设为当前色 |
| `edt_info` | `edit`（多行只读） | 显示 HEX / rgb() / hsl() / 亮度 / 对比度 / 建议字色 |
| `cp_1`..`cp_8` | `colorpicker` | 8 个调色板色格，改任一格即设为当前色 |
| `btn_add` / `btn_clear` | `button` | 把当前色加入调色板 / 复位全部色格 |
| `btn_export` | `button` | 导出 `palette.css` 与 `palette.txt` |
| `btn_copy` | `button` | 把调色板拼成纯 HEX 列表送进剪贴板 |
| `term_out` | `terminal` | 操作记录 |
| `sb_main` | `statusbar` | 状态提示 |

## 四、关键接口
`cg_choose_color` 弹系统取色器；`cg_widget_set_value` / `cg_widget_value` 读写颜色值；`cg_clip_set_text` 写剪贴板；`cg_path_join` + `cg_file_write` 导出两份文本；`cg_term_write` / `cg_status_set` 记录；`cg_free` 还框架堆串。

## 五、容易踩的坑
- 改 `colorpicker` 的颜色**直接用 `cg_widget_set_value()`**：运行时对颜色值是特判的、**不按 `min` / `max` 夹取**。
- `colorpicker` 只有 `change` 一个事件，而且是"在系统取色器里点了确定"之后才发 —— 没有单独的"被点击"事件，所以色格只能靠 `change` 来设当前色。
- 对比度是**算出来的**（分量线性化后 `(L亮 + 0.05) / (L暗 + 0.05)`），不是按色块名猜的。
- 自检不开窗：弹取色器、写盘、写剪贴板之前都先问有没有窗口。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · palette」。
- `docs\CGUI-FORMAT.md` → `colorpicker` 的字段与事件。
