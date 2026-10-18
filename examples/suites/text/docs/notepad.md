> **本文档是合并前独立工程 `examples\notepad\` 的原文**（2026-10-18 该工程已并入
> [`examples\suites\text\`](../text)：本工具的完整逻辑在 `modules\np.c`，
> 编译 `build-app.bat suites\text`，五个工具各占一个页签）。
>
> 文中「目录 / 编译 / 运行 / 自检」那几行命令是**合并前的口径，现已失效**（原工程目录已删）；
> 其余内容 —— 它演示什么、关键接口、容易踩的坑 —— 仍然逐条成立，对应 `modules\np.c`。
# 记事本 notepad：菜单分发、脏标记、打开 / 另存、编码识别

> 目录 `examples\notepad\`　编译 `build-app.bat notepad`　运行 `examples\notepad\notepad.exe`　自检 `notepad.exe --selftest examples\notepad\notepad.cgui` → 控件 3 个 / 回调 2 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
一个"真能用的工具"的业务怎么组织：菜单分发、脏标记、状态栏刷新、文件读写、编码转换、INI 持久化、关闭前拦截与日志。
「文件」菜单的四条命令（新建 / 打开 / 保存 / 另存为）用**动作（`actions`）**实现：`.cgui` 里一处写清「文字 + 快捷键 + 回调名」，菜单项只写 `action` 引用；快捷键由加速键表处理，所以焦点在输入框里按 `Ctrl+S` 也能保存。
它也是基础能力库的活例子：读文件、写文件、INI、路径拼接、剪贴板、GBK 转换全都用到，且不 include 任何 Windows 头。

## 二、怎么用
1. 在项目根编译：`build-app.bat notepad`，双击 `examples\notepad\notepad.exe`。
2. 「文件 → 打开...」读一个文本文件；字节不像 UTF-8 的老文件会被识别并按 GBK 转换后显示。在正文里改字，标题末尾出现 ` *` 脏标记，状态栏的字数 / 行数跟着变。
3. `Ctrl+N` 新建、`Ctrl+S` 保存、`Ctrl+Shift+S` 另存为；「编辑」菜单里复制全部 / 粘贴 / 清空。
4. 关窗口时若有未保存改动会先拦一次并提示；窗口尺寸与上次打开的文件路径记进用户数据目录的 `notepad.ini`，下次打开自动接上。
5. 命令行：`notepad.exe`（自动找同名 `.cgui`）、`notepad.exe xxx.cgui`、`--selftest`。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `menu_main` | `menu` | 文件 / 编辑 / 帮助；文件菜单四条走动作，其余靠 `id` 分发 |
| `edt_body` | `edit`（多行） | 正文编辑区，`change` 一改就置脏 |
| `sb_main` | `statusbar` | 三段：路径 / 字数 / 行数 |

`app.events` 里还挂了 `close` 与 `size`；`actions` 里定义了 `new` / `open` / `save` / `save_as` 四个动作。

## 四、关键接口
- 文件与路径：`cg_file_read`、`cg_file_write`、`cg_path_join`、`cg_path_name`
- 编码：`cg_gbk_to_utf8`
- 配置：`cg_ini_read`、`cg_ini_write`
- 剪贴板：`cg_clip_get_text`、`cg_clip_set_text`
- 对话框与标题：`cg_open_file_dialog`、`cg_save_file_dialog`、`cg_set_title`
- 堆串：`cg_str_format`、`cg_free`

## 五、容易踩的坑
- `cg_str_format` 返回堆内存，而 `cg_widget_set_text` 只是拷贝一份、**不接管所有权** —— 必须先落到变量再 `cg_free`（粘贴、标题、关于都这么做）。
- 老文件字节不像 UTF-8 才走 `cg_gbk_to_utf8()` 转换，否则中文全乱码。
- 菜单项要写 `id`，用 `cg_menu_last_id(self)` 认；动作回调里 `self` 恒为主窗口伪控件、`ev` 恒为 `click`，一个 handler 挂多个动作用 `cg_action_name(self)` 区分。
- 窗口尺寸在 `on_close` 才写盘（别在拖动中每帧写）；关窗拦截靠 `cg_cancel_close(app)`。

## 六、相关文档
- `docs\articles\07-实战-记事本.md`：本示例的专文（业务组织、菜单动作、编码识别都在里面）。
- `examples\教学使用说明.md` → 「七、实用工具详解 · notepad」。
- `docs\CGUI-FORMAT.md`：`menu`、`actions` 的字段写法。
