# HTML 报告器：把 CSV 生成离线可看的 HTML 报告

> 目录 `examples\htmlreport\`　编译 `build-app.bat htmlreport`　运行 `examples\htmlreport\htmlreport.exe`　自检 `htmlreport.exe --selftest examples\htmlreport\htmlreport.cgui` → 控件 7 个 / 回调 4 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
把 CSV 变成一份**离线可看的 HTML 报告**，再用网页控件预览、用系统默认浏览器兜底打开。三件事：读数据（选 CSV、首行表头，超 8 MB 只取前段、数据行封顶 5000 行）、生成 HTML（写进用户数据目录，内嵌 CSS、**不引外部资源、不含 JavaScript**）、打开（网页控件加载 + 系统浏览器兜底）。
教的是「自己拼 HTML 时最容易出的那个安全问题」：**单元格必须做 HTML 转义**。

## 二、怎么用
1. 点「打开 CSV...」选一个首行是表头的 CSV（超过 8 MB 只取前一段，数据行封顶 5000 行；不处理引号转义）。
2. 点「生成并预览」：把 HTML 写进用户数据目录（首块覆盖写、其后追加），然后让网页控件 `web_view` 加载它。
3. 网页控件是实验性的、起不来会降级成空占位 —— 那时点「用系统默认浏览器打开」。
4. 点「复制 HTML 源码」把完整 HTML 放进剪贴板（上限 256 KB，超了截断）。
5. 日志区 `term_log` 记下读数据 / 生成 / 打开的过程；状态栏报「列 × 行」与报告状态。
6. 命令行：`htmlreport.exe`（自动找同名 `.cgui`）、`htmlreport.exe xxx.cgui [要处理的.csv]`、`htmlreport.exe 某个.csv`（界面自动找，启动后读它、生成报告并预览）。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `btn_open` | button | 打开 CSV（超 8 MB 截断、5000 行封顶） |
| `btn_gen` | button | 生成并预览 |
| `btn_sys` | button | 用系统默认浏览器打开（兜底） |
| `btn_copy` | button | 复制 HTML 源码 |
| `web_view` | browser | 网页控件预览（实验性） |
| `term_log` | terminal | 读数据 / 生成 / 打开日志 |
| `sb_main` | statusbar | 等待数据 / 列×行 / 报告状态 |

## 四、关键接口
`cg_open_file_dialog` 选 CSV；`cg_file_size` 量大小、`cg_file_read_bytes` 整读；`cg_app_data_dir` + `cg_path_join` 定位输出文件；`cg_file_write` 首块覆盖写、`cg_file_append` 其后追加；`cg_browser_navigate` 让网页控件加载报告；`cg_clip_set_text` 复制源码。

## 五、容易踩的坑
- **单元格必须做 HTML 转义**（`&` `<` `>` `"` 四个）—— 少一个，用户数据里的 `<` 就能把页面结构搞坏。
- 网页控件是实验性的、起不来会降级成空占位，而且运行时**没有**「控件是否真起来了」的查询接口，所以「交给系统默认浏览器」这条兜底必须留着。
- CSV 不处理引号转义、不处理字段内换行、不转码。
- 报告不引外部资源、不含 JavaScript，拷到别的机器也能看。
- 内存全是文件级 static 数组、不 malloc/free（唯一堆内存来自整读接口，拷完立刻还回去）。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · htmlreport」。
- `docs\CGUI-FORMAT.md` → `browser` / `terminal` 的字段与事件（`browser` 是实验性控件，能力边界见那节）。
