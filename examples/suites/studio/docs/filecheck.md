> **本文档是合并前独立工程 `examples\filecheck\` 的原文**（2026-10-18 该工程已并入
> [`examples\suites\studio\`](../)：本工具的完整逻辑在 `modules\fc.c`，
> 编译 `build-app.bat suites\studio`，16 个工具各占一个页签）。
>
> 文中「目录 / 编译 / 运行 / 自检」那几行命令是**合并前的口径，现已失效**（原工程目录已删）；
> 其余内容 —— 它演示什么、关键接口、容易踩的坑 —— 仍然逐条成立，对应 `modules\fc.c`。
# 文件核对工具：比两个文件是否一致，不一致就指出差在哪

> 目录 `examples\filecheck\`　编译 `build-app.bat filecheck`　运行 `examples\filecheck\filecheck.exe`　自检 `filecheck.exe --selftest examples\filecheck\filecheck.cgui` → 控件 21 个 / 回调 9 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
面向固件与镜像发布的**纯离线**工具 —— A / B 两个文件槽选中后立刻给出大小与 MD5 / SHA1 / SHA256 / SHA512 / CRC32；「比对」先比大小再比 SHA256；「找差异」逐字节比；报告可复制 / 导出；文件还能拖进窗口按 A、B 顺序落槽。
补的能力面是只读摘要计算（不必整读进内存）+ 逐字节差异定位 + 报告导出 + 拖放。工控里就是 —— 核对两个固件 / 镜像是不是同一个、不一样差在哪。

## 二、怎么用
1. 点两个「选择文件...」把文件填进 A、B；或直接把文件拖进窗口（按 A、B 顺序落槽）。
2. 选中后立刻在日志 `term_log` 里看到大小与五种摘要。
3. 点「开始比对」：给出「一致 / 不一致 / 无法比对」结论与状态灯（绿=一致 / 红=不一致 / 灭=无法比对）。
4. 点「找差异」：两文件都读得动且 ≤ 4 MB 时逐字节比，差异全部计数、表格 `tbl_diff` 只列前 200 条。
5. 点「复制报告」进剪贴板 / 「导出报告」写成 .txt；点「清空」清掉两槽、结论、差异表与日志。
6. 命令行：`filecheck.exe`（自动找同名 `.cgui`）、`filecheck.exe xxx.cgui`、`--selftest`。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `edt_a_path` / `edt_b_path` | edit | 文件 A / B 路径 |
| `btn_a_pick` / `btn_b_pick` | button | 选择文件 |
| `led_verdict` / `lab_verdict` | led / label | 结论灯与结论文字 |
| `btn_cmp` | button | 比大小与 SHA256 |
| `btn_diff` | button | 逐字节找差异 |
| `btn_copy` / `btn_export` | button | 复制报告 / 导出报告 |
| `btn_clear` | button | 清空 |
| `term_log` | terminal | 大小 / 五种摘要 / 结论 |
| `tbl_diff` | table | 偏移 / A 的字节 / B 的字节 |
| `sb_main` | statusbar | 状态 / A、B 槽 |

## 四、关键接口
`cg_file_size` 取大小；`cg_hash_file` 算 MD5 / SHA1 / SHA256 / SHA512；`cg_crc32_file` 算 CRC32；`cg_file_read_bytes` 只在逐字节比对时读；`cg_clip_set_text` 复制报告；`cg_file_write` 导出报告；`cg_ini_read` / `cg_ini_write` 记窗口尺寸与上次的 A / B 路径；`cg_app_data_dir` 定位配置文件。

## 五、容易踩的坑
- 逐字节比对只在两个文件都读得动且 ≤ 4 MB 时才做，超限改为比摘要、不硬读进内存。
- 差异全部计数，但表格只列前 200 条。
- **空文件与打不开必须分清**：前者能比大小，后者连摘要都算不出。
- 全程不 malloc/free，只用框架堆串配 `cg_free`。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · filecheck」。
- `docs\CGUI-FORMAT.md` → `led` / `table` / 窗口 `drop` 事件的写法。
- `docs\articles\09-校验与摘要.md` → 哈希与 CRC 接口语义。
