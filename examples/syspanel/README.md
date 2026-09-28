# 系统信息面板：只读系统信息 + INI 读写 + 剪贴板

> 目录 `examples\syspanel\`　编译 `build-app.bat syspanel`　运行 `examples\syspanel\syspanel.exe`　自检 `syspanel.exe --selftest` → 控件 40 个 / 回调 8 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
两个页签各展示一类"界面之外"的基础能力。系统信息页：是否 64 位、系统版本、磁盘容量与占用、环境变量、进程存在性、串口枚举，**全部只读**。配置页：读写用户数据目录下的 `syspanel.ini`，用表格显示「节 / 键 / 值」，支持读一项 / 写一项 / 全部重载，还能把表格内容送进剪贴板。
它补的是"程序要读环境、要落配置"这一块能力面，不接任何硬件。

## 二、怎么用
1. 系统信息页：点「刷新」重算 系统位数与版本、磁盘目录与占用率、环境变量、进程存在性、串口列表，并把刷新时间写进状态栏。
2. 进程存在性：在 `edt_proc` 填进程名（默认 `explorer.exe`，大小写不敏感），回车或点「检查」；`led_proc` 与 `lab_proc_res` 给结论。
3. 串口列表只枚举、不打开（`lst_port`）；没有端口返回 0 是正常情况。
4. 切到「配置」页签：`tbl_cfg` 显示每个配置项的「节 / 键 / 值」。
5. 在 `edt_sec` / `edt_key` / `edt_val` 填好后：「读一项」把 `[节] 键` 的值填进「值」输入框；「写一项」写 `[节] 键 = 值`（写成功后重载表格）；「全部重载」按内置清单逐条读回并写日志。
6. 「复制到剪贴板」把表格内容按每行一条送进剪贴板。
7. 配置文件路径是 `<用户数据目录>\syspanel.ini`，首启会把内置默认值写进去。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `tab_main` | tabs | 系统信息 / 配置 两个页签 |
| `lab_is64_v` / `lab_ver_v` | label | 位数 / 系统版本 |
| `prg_disk` / `lab_disk_num` | progress / label | 磁盘已用百分比与容量数字 |
| `lst_env` | listbox | 环境变量（路径截前 60 字符） |
| `edt_proc` / `btn_proc` / `led_proc` | edit/button/led | 进程存在性检查 |
| `lst_port` | listbox | 串口枚举 |
| `tbl_cfg` | table | 配置「节 / 键 / 值」 |
| `edt_sec` / `edt_key` / `edt_val` | edit | 读 / 写哪一项 |
| `btn_cfg_read` / `btn_cfg_write` / `btn_cfg_reload` / `btn_cfg_copy` | button | 读一项 / 写一项 / 全部重载 / 复制到剪贴板 |
| `lst_log` | listbox | 逐行操作日志（超 400 条自动清一次） |

## 四、关键接口
- `cg_sys_is64` / `cg_sys_version`：是否 64 位、可读版本串。
- `cg_disk_space`：问用户数据目录所在盘的剩余 / 总量。
- `cg_env` / `cg_env_int`：按名取环境变量（字符串 / 整数，如 CPU 个数）。
- `cg_process_running`：判进程是否存在。
- `cg_serial_list`：枚举串口（没有返回 0，不是错误）。
- `cg_ini_read` / `cg_ini_write`：按「节 + 键」读写 INI。
- `cg_clip_set_text`：把表格送进剪贴板。
- `cg_app_data_dir`：定位 `syspanel.ini` 的落点。

## 五、容易踩的坑
- 两条自检红线：就绪钩子先问有没有窗口，不开窗就不写 INI、不枚举串口，只写一行日志说明跳过；凡有副作用（写盘 / 写剪贴板）的回调也先问，免得自检留下垃圾数据。
- 读 / 写时"节"或"键"为空会被直接拒绝。
- `cg_sys_version` 返回的是堆串，用完要 `cg_free`。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · syspanel」。
- `docs\CGUI-FORMAT.md` → 第 4 节（控件类型表：`tabs` / `table` / `listbox`）。
