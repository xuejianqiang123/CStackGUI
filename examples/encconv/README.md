# 编码与换行转换器：GBK/UTF-8 互转

> 目录 `examples\encconv\`　编译 `build-app.bat encconv`　运行 `examples\encconv\encconv.exe`　自检 `encconv.exe --selftest examples\encconv\encconv.cgui` → 控件 22 个 / 回调 7 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
处理「别人给的一批 GBK 老文件」—— 枚举目录、判定编码、预览前几行、GBK ↔ UTF-8 互转 + 换行统一（保持 / CRLF / LF）+ 可选写 BOM，最后统计「转换 / 跳过 / 失败」。补的能力面是目录枚举、编码互转、整读整写，以及 **INI 配置文件导入**（`cg_ini_read` 逐键读一份 `[task]` 任务单、`cg_ini_write` 造示例任务单）—— 车间/脚本把"转什么、怎么转"写成 INI 交给工具，参数一键导入，也可以命令行 `encconv.exe task.ini` 直接按单跑。
**开箱可用**：「造示例文件」一键造 4 个示例（3 个真 GBK 中文文件 + 1 个纯 ASCII 对照）+ 1 份示例任务单 `task.ini` 并自动枚举；点「导入 INI 配置」参数即进界面；预览疑似 GBK 的文件时把**同一份字节的两种解读并排贴出来**（正常中文 vs 乱码），"价值"一眼看得见。

## 二、怎么用
1. 在「目录」框填要扫描的目录，或点「用用户数据目录」；没素材就点「造示例文件」（造 4 个文件 + 1 份任务单并自动枚举）。
2. 点「导入 INI 配置」读「任务单」框里的 INI（`[task]` 节：`dir` / `direction` / `newline` / `bom` / `overwrite`）；任务单框**空着**就默认找「目录」下的 `task.ini`。缺项保持界面当前值，非法值点名报出 —— 导入只填参数，不替你转换。`direction` 收 `gbk2utf8` / `utf82gbk`，`newline` 收 `keep` / `crlf` / `lf`，`bom` / `overwrite` 收 `0` / `1`。
3. 点「枚举文件」按文本类扩展名列出候选，表格显示「文件名 / 大小 / 判定 / 结论」。
4. 点「预览选中」（或双击表格）看前 20 行；疑似 GBK 的会并排贴（A 按 GBK 正确解 / B 按 UTF-8 强行解 = 现在双击文件的乱码）。
5. 选「方向」「换行」，按需勾「写 UTF-8 BOM」，点「开始转换」：逐个候选转换并落盘，报「原名 → 新名 / 字节变化 / 结论」。默认**另存新文件**；勾「覆盖原文件（危险）」后需再点一次「开始转换」才真覆盖（任务单把 `overwrite` 填 1 也一样，导入不绕过两步确认）。
6. 命令行：`encconv.exe`（自动找同名 `.cgui`）、`encconv.exe xxx.cgui [目录]`、`encconv.exe <目录>`、`encconv.exe task.ini`（导入 `[task]` 参数后枚举并**另存**转换一遍；只另存、绝不覆盖）。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `edt_dir` | edit | 要扫描的目录（没有「选目录」对话框，手填） |
| `btn_datadir` / `btn_samples` / `btn_scan` | button | 填用户数据目录 / 造示例文件 / 枚举文件 |
| `edt_ini` / `btn_import` | edit / button | 任务单 INI 路径 / 导入 INI 配置（空着默认找目录下 `task.ini`） |
| `btn_preview` | button | 预览选中（疑似 GBK 贴两种解读） |
| `cbo_dir` / `cbo_nl` | combo | 方向 / 换行风格 |
| `chk_bom` / `chk_overwrite` | checkbox | 写 UTF-8 BOM / 覆盖原文件（危险） |
| `btn_convert` | button | 开始转换 |
| `tbl_files` | table | 文件名 / 大小 / 判定 / 结论 |
| `term_out` | terminal | 说明 / 预览 |
| `prg_main` | progress | 处理到第几个文件 |

## 四、关键接口
`cg_dir_list` 逐条枚举目录；`cg_path_join` / `cg_path_ext` 拼路径 / 取扩展名；`cg_file_size` 量大小；`cg_file_read_bytes` 整读、`cg_file_write_bytes` 整写；`cg_gbk_to_utf8` / `cg_utf8_to_gbk` 互转（返回堆串、失败返回 NULL，用完 `cg_free`）；`cg_ini_read` / `cg_ini_write` 按节+键读/写 INI（导入任务单 / 造示例任务单）；`cg_file_exists` 判存在；`cg_str_format` 拼日志。

## 五、容易踩的坑
- **判定只是猜**：先按严格 UTF-8 校验，不合法才判「疑似 GBK」；纯 ASCII 谁都合法、GBK 内容偶尔恰好也是合法 UTF-8。界面 / 日志 / 表格结论列都照实说。
- 文件接口只有整读：单文件超 8 MB **只跳过不截断**（截一半两边都不合法）。
- 没有改名接口，「覆盖原文件」要两步确认；另存名是**换掉原扩展名**再加后缀：`参数表.txt` → `参数表.utf8.txt`。
- 转码入参必须是 `'\0'` 结尾的串；「这段字节是不是合法 UTF-8」的校验是本文件自写；`cg_file_read` 会自动去 BOM，要判 BOM 得用 `cg_file_read_bytes`（一个字节都不动）。
- 「造示例文件」**不覆盖同名文件**（撞上可能是用户的真文件），已存在就跳过并报出来；顺带造的 `task.ini` 把 `dir` 留空 = 导入时保持目录框现状。
- **`cg_ini_*` 没有枚举节/键的接口**，所以任务单是键名固定的"表单"（逐键 `cg_ini_read`），不是任意 INI 的全量解析；要新增可导入项，读侧写侧要成对加。
- `task.ini` 自己也是 `.ini` 文本，会被「枚举文件」列出来（内容纯 ASCII，判「无需转换」，GBK→UTF-8 方向不会被动到），无害。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · encconv」。
- `docs\CGUI-FORMAT.md` → `edit` / `combo` / `table` 的字段与事件。
