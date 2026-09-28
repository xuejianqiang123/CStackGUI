# 设置持久化三后端对比：INI 文件 / 注册表 HKCU / 用户数据目录文件

> 目录 `examples\settings\`　编译 `build-app.bat settings`　运行 `examples\settings\settings.exe`　自检 `settings.exe --selftest` → 控件 20 个 / 回调 4 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
同一组设置（上次目录 / 字号 / 深色 / 最近文件）存进**三个后端**并排对比：INI 文件、注册表 HKCU、用户数据目录里的文本文件。用一张对比表把"三个后端各存各的"摊开 —— 换后端保存后，旧后端的值仍留在原处，一眼就能看出差别。
专讲"设置该存哪儿"，也顺带把注册表接口的能力边界（只能按名读写字符串）讲透。

## 二、怎么用
1. 在上面填设置：`edt_dir` 上次目录、`edt_font` 字号（8..28）、`chk_dark` 深色、`edt_recent1..3` 最近文件。
2. `cmb_backend` 选后端：INI 文件 / 注册表 HKCU / 用户数据目录文件；选哪个，右边的按钮就作用于哪个。
3. 点「保存」把上面这组写进当前后端；点「读取」从当前后端读回（缺字段 / 脏数据回退默认值）；点「清空」清掉当前后端 —— INI 与数据文件是删文件，注册表没有删值接口，只能把每个值改写空串（值空 = 相当于没设）。
4. 看 `tbl_back` 对比表：三个后端各存着什么（字号 / 深色 / 更新时间 / 不存在）。
5. 每次操作都会在 `term_log` 留一行日志。
6. 开窗时会先做一次**注册表往返自测**（往本示例专用子键写一个带时间戳的探针值再读回比对），把"这套注册表接口到底通不通"当场报在日志里。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `edt_dir` / `edt_font` | edit | 上次目录 / 字号设置项 |
| `chk_dark` | checkbox | 深色设置项 |
| `edt_recent1..3` | edit | 最近文件三项 |
| `cmb_backend` | combo | 选后端 |
| `btn_save` / `btn_load` / `btn_clear` | button | 保存 / 读取 / 清空 |
| `tbl_back` | table | 三个后端各自存着什么 |
| `term_log` | terminal | 操作日志 |

## 四、关键接口
- `cg_ini_read` / `cg_ini_write`：INI 后端，按「节 + 键」读写字符串。
- `cg_reg_read` / `cg_reg_write`：注册表后端，只能按名读写字符串值，子键相对 HKCU。
- `cg_file_read` / `cg_file_write` / `cg_file_delete`：数据目录文件后端，一次写整份 / 读整份。
- `cg_app_data_dir` / `cg_path_join`：拼出 INI 与数据文件的全路径。
- `cg_time_now`：写各项的"更新时间"，用来看哪个后端是新的。

## 五、容易踩的坑
- 注册表接口**只能按名读写字符串值**（子键相对 HKCU），**没有**枚举子键 / 列举值 / 删值 / 删键的接口 —— 所以"清空"只能把每个值写成空串，读取侧也按"空 = 没设"处理。
- 三个后端都**没有锁与事务**，两个进程同时写同一个后端会互相覆盖；真实项目要么只由主进程写，要么自己加临时文件。
- 本示例固定写 `HKCU\Software\CStackGUI\examples\settings`，**绝不动用户的真实设置**。
- 自检（`--selftest`）不开窗：**不读也不写任何后端**，"保存 / 读取 / 清空"这些有副作用的回调一律先问 `cg_app_has_window`。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · settings」。
- `docs\CGUI-FORMAT.md` → 第 2.6 节（表格 `columns`）、第 4 节（控件类型表）。
