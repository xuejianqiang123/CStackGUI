> **本文档是合并前独立工程 `examples\logfilter\` 的原文**（2026-10-18 该工程已并入
> [`examples\suites\studio\`](../)：本工具的完整逻辑在 `modules\lf.c`，
> 编译 `build-app.bat suites\studio`，16 个工具各占一个页签）。
>
> 文中「目录 / 编译 / 运行 / 自检」那几行命令是**合并前的口径，现已失效**（原工程目录已删）；
> 其余内容 —— 它演示什么、关键接口、容易踩的坑 —— 仍然逐条成立，对应 `modules\lf.c`。
# 日志过滤器：打开文本日志 → 关键字过滤 → 终端 + 行号表 → 导出 / 复制

> 目录 `examples\logfilter\`　编译 `build-app.bat logfilter`　运行 `examples\logfilter\logfilter.exe`　自检 `logfilter.exe --selftest examples\logfilter\logfilter.cgui` → 控件 21 个 / 回调 10 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
专门把「文件怎么读」这条能力边界说清楚：打开文本日志 → 关键字过滤 → 终端 + 行号表 → 导出 / 复制；两个关键字配「与 / 或」与反向开关。补的能力面是「整读 + 大小上限」「终端 + 表格的双视图」「导出攒块与剪贴板」。工控里就是 —— 在几百 MB 的日志里挑出想看的那几行。

## 二、怎么用
1. 点「打开日志文件...」选一个文本日志；超过 32 MB 只取前 32 MB 并提示「请先切分」。
2. 填「关键字 A」「关键字 B」（留空 = 这一条不参与），勾「A 或 B」（不勾 = 与）、「区分大小写」、「反向」。
3. 点「开始过滤」：结果进行号表 `tbl_hits` 与终端 `term_out`；进度条 `prg_work` 只表示「读取 / 过滤 / 完成」三步。
4. 点「命中内容复制到剪贴板」（上限 256 KB）或「导出结果...」写成 UTF-8 文本（攒块 + 追加）。
5. 点「清空视图」只清终端与表，文件还留着，改条件可直接重滤。
6. 命令行：`logfilter.exe`（自动找同名 `.cgui`）、`logfilter.exe xxx.cgui`、`--selftest`。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `btn_open` | button | 打开日志（超 32 MB 只取前段） |
| `lab_size` | label | 当前文件大小 / 载入情况 |
| `prg_work` | progress | 0 未开始 / 1 已读取 / 2 过滤中 / 3 完成 |
| `edt_kw_a` / `edt_kw_b` | edit | 关键字 A / B（子串匹配，不是正则） |
| `chk_case` / `chk_or` / `chk_invert` | checkbox | 区分大小写 / A 或 B / 反向 |
| `btn_filter` | button | 按当前条件重扫 |
| `btn_copy` / `btn_export` | button | 命中内容进剪贴板 / 导出文件 |
| `tbl_hits` | table | 行号 + 内容（前 80 字节） |
| `term_out` | terminal | 带行号回显命中（最多 2000 行） |
| `sb_main` | statusbar | 文件 / 条件 / 命中统计 |

## 四、关键接口
`cg_file_size` 先量一遍判断超限；`cg_file_read_bytes` 整读（唯一堆内存，拷进 static 工作窗口后立刻还回）；`cg_open_file_dialog` / `cg_save_file_dialog` 选输入 / 输出文件；`cg_clip_set_text` 写剪贴板；`cg_file_write` / `cg_file_append` 攒块导出；`cg_term_write` 回显命中；`cg_table_add_row` 填行号表。

## 五、容易踩的坑
- 运行时的文件接口只有**整读**，**没有**「按偏移读一块 / 流式读」—— 所以走整读 + 32 MB 上限，超了只取前段并提示，宁可少看一段也不悄悄读进几百 MB。
- **没有正则、也没有子串查找接口**：两个关键字只有「与 / 或」两种组合；子串匹配是自写的，忽略大小写只对 ASCII 字母折叠（中文不受影响）。
- 大缓冲是文件级 static 数组：exe 数据段大一块，且一次只能开一份日志。
- 自检不开窗：就绪钩子不读文件，读盘 / 弹框前先问有没有窗口。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · logfilter」。
- `docs\CGUI-FORMAT.md` → `edit` / `checkbox` / `table` / `terminal` 的字段与事件。
