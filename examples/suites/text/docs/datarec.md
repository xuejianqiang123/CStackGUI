> **本文档是合并前独立工程 `examples\datarec\` 的原文**（2026-10-18 该工程已并入
> [`examples\suites\text\`](../text)：本工具的完整逻辑在 `modules\dr.c`，
> 编译 `build-app.bat suites\text`，五个工具各占一个页签）。
>
> 文中「目录 / 编译 / 运行 / 自检」那几行命令是**合并前的口径，现已失效**（原工程目录已删）；
> 其余内容 —— 它演示什么、关键接口、容易踩的坑 —— 仍然逐条成立，对应 `modules\dr.c`。
# 采集记录器：模拟采集 + 双曲线 + CSV 落盘 + 回放

> 目录 `examples\datarec\`　编译 `build-app.bat datarec`　运行 `examples\datarec\datarec.exe`　自检 `datarec.exe --selftest examples\datarec\datarec.cgui` → 控件 8 个 / 回调 4 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
不接任何硬件的采集记录器：数据来自定周期定时器产生的"整数正弦 + 噪声"（设备A / 设备B 两条）。一条数据流三个去向 —— 实时喂图表两条曲线、在内存固定数组里累计、点「保存 CSV」时一次落盘；另有回放与上限保护。
补的能力面是「把一次性定时器当周期定时器用」「图表实时推点」「CSV 分块追加」串成完整链路。工控里就是「设备喂数 → 实时看曲线 → 存盘留档」的骨架。

## 二、怎么用
1. 点「开始采集」：挂一颗 200ms 定时器，两条曲线开始动，右侧日志逐条追加。
2. 采集期间状态栏报「已采集多少点 · 时长」；点「停止」杀掉定时器，内存缓冲留着。
3. 点「保存 CSV」：先覆盖写表头，再按 4KB 分块追加；路径填进只读的「最近一次路径」框。
4. 点「回放」：读回那份 CSV → 逐行解析 → 清空图表后重放一遍。
5. 缓冲满 10 万点会自动停采集并明确提示（绝不静默丢数据）。
6. 命令行：`datarec.exe`（自动找同名 `.cgui`）、`datarec.exe xxx.cgui`、`--selftest`。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `cht_wave` | chart | 双曲线「设备A / 设备B」，一次推一个点 |
| `edt_path` | edit | 最近一次保存 / 回放的 CSV 路径（只读） |
| `btn_start` / `btn_stop` | button | 开始采集（挂 200ms 定时器）/ 停止 |
| `btn_save` | button | 保存 CSV（覆盖表头 + 分块追加） |
| `btn_replay` | button | 读回 CSV 重放曲线 |
| `lst_log` | listbox | 采集 / 落盘日志 |
| `sb_main` | statusbar | 状态 / 点数与时长 / CSV 名 |

## 四、关键接口
`cg_set_timer` 起一次性定时器（回调末尾自己重挂，形成周期）；`cg_kill_timer` 停止；`cg_chart_push` 推点；`cg_chart_clear` 回放前清曲线；`cg_file_write` 写表头、`cg_file_append` 分块追加；`cg_file_read_bytes` 回放读回；`cg_file_exists` 回放前确认文件在；`cg_widget_list_addf` + `cg_widget_list_scroll_bottom` 追加日志并滚到底。

## 五、容易踩的坑
- **定时器是一次性的**：定周期靠回调末尾再挂一颗，别指望它自己重复。
- 采集期间只在内存累计（固定数组，不用 malloc），保存时才分块落盘 —— 每拍都写文件就是几万次系统调用。
- 缓冲固定 10 万点，满了「停采集 + 明确提示」，绝不静默丢数据。
- 自检不开窗：就绪钩子不启动采集，回调里先判「在不在采」，弹框 / 文件对话框前先问有没有窗口。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · datarec」。
- `docs\CGUI-FORMAT.md` → `chart` / `timer` 的字段与事件。
