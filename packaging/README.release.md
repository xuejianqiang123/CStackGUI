# CStackGUI 使用说明

**拖拽画界面 → 存成 `.cgui` → C 写业务 → 编译出一个独立 exe。**

界面的长相在画布里拖出来，存成 `.cgui` 文件；**程序运行的时候才读它，不编译进去**。
业务逻辑是普通 C 文件，只写回调函数。最终产物是**一个 exe，零依赖**。

---

## 一、这个目录里有什么

```
CStackGUI.exe          画布：拖拽设计界面、导出 C 骨架
build-app.bat          编译某个工程 -> exe（成功后自动刷新 clangd 配置）
compile_commands.json  clangd 语言服务配置（编译一次后自动生成，给编辑器用）
README.md              本文件
tools\
  gen-cc.mjs           生成 compile_commands.json（build-app.bat 会自动调，也可手动跑）
  check-format.bat     格式契约回归：画布写出的 .cgui 运行时一定能读
  format-roundtrip.mjs 上一条的实际测试脚本（用预编译的画布模型改写 .cgui 再让运行时读）
  shot.ps1             跑一个界面并截图（调外观时肉眼验证用）
runtime\
  include\cstackgui.h  业务代码唯一需要 include 的头
  lib\libcstackgui.a   编译好的运行时库（不用自己编）
examples\
  hello\               示例：基础控件（13 个控件、8 个回调）
  gallery\             示例：控件陈列室（29 个控件，把每种控件都用上了）
apps\                  你自己导出的工程会放在这里（现在还没有）
build\                 本包的中间产物（预编译的画布模型、回归测试输出，可随时删）
.codebuddy\
  skills\              AI 编程助手（CodeBuddy）的项目级技能：cstackgui-dev（写 C 回调）、
                       cgui-edit（手改 .cgui），用 CodeBuddy 打开本目录时会自动加载
```

需要什么：

| 你想干什么 | 需要装什么 |
| --- | --- |
| 只画界面、导出 `.cgui` | **什么都不用装**，双击 `CStackGUI.exe` |
| 写出能跑的 exe | MinGW-w64 的 gcc（见下面第二步）|
| 写 C 时要补全 / 跳转 / 实时报错 | Node.js + clangd（见第五节）|

---

## 二、只想画界面（不用装任何东西）

1. 双击 **`CStackGUI.exe`**。
2. 左侧「控件」面板里点一下（或按住拖到画布上）加控件；拖动手柄改大小。
3. 右侧「属性」里改 `id`、文字、位置尺寸、样式、初始状态。
4. 右侧「事件」里给控件填 **C 函数名**（点「自动」会按 `on_<id>_<事件>` 生成规范名字）。
   只有这个控件真正支持的事件才会列出来。
5. `Ctrl+S` 存成 `.cgui`。

界面随时可以拿回来改：`Ctrl+O` 打开 `.cgui`，改完再存即可。
**改界面不需要重新编译 exe**，因为 `.cgui` 是运行时读的。

---

## 三、要做出能跑的 exe

### 第一步：装 gcc

去 <https://winlibs.com> 或 MSYS2 装一个 MinGW-w64，然后确认命令行里能用：

```bat
gcc --version
```

如果提示"不是内部或外部命令"，把它的 `bin` 目录加进 PATH，或者设一下环境变量：

```bat
set MINGW=D:\mingw64\bin
```

### 第二步：设计界面并导出 C 骨架

在画布里画好界面（记得在「事件」页签填好 C 函数名），然后点工具条的 **「导出 C 骨架」**，
选一个目录。推荐导到本发布包的 `apps\` 下，画布会自动建 `apps\<界面标题>\`。

导出会生成这些文件：

| 文件 | 是谁的 | 要不要改 | 作用 |
| --- | --- | --- | --- |
| `<名字>.cgui` | 画布产物 | 别手改，回画布改 | 界面定义，运行时读它 |
| `<名字>_ui_events.h` | 生成，勿改 | 不要 | 回调原型 |
| `<名字>_ui_events.c` | **你的代码** | **主要就改它** | 每个事件一个空函数，等你填逻辑 |
| `main.c` | 生成 | 一般不动 | `main()` + 绑定表 |

**重新导出不会覆盖你已经写好的 `<名字>_ui_events.c`**，放心改界面再导出。

### 第三步：填业务逻辑

打开 `apps\<名字>\<名字>_ui_events.c`，把空函数填上：

```c
void on_ok_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);                    /* 从控件拿到应用 */
    cg_widget_t *log = cg_find(app, "lst_log");         /* 用 id 找别的控件 */

    cg_widget_list_addf(log, "按钮被点了");              /* 往列表框写一条日志 */
    cg_widget_set_text(cg_find(app, "lab_status"), "已处理");
}
```

### 第四步：编译

在本目录（有 `build-app.bat` 的那一层）执行：

```bat
build-app.bat <名字>
```

例如 `build-app.bat hello`。产物就是 `apps\<名字>\<名字>.exe`。

### 第五步：运行

**双击 `apps\<名字>\<名字>.exe` 就行** —— 程序会自己去找旁边的 `.cgui`。
调试想看到 `printf` 输出的话，在命令行里跑：

```bat
apps\<名字>\<名字>.exe
```

只想校验界面和事件有没有对上（不开窗、适合放进 CI）：

```bat
apps\<名字>\<名字>.exe --selftest
```

它会解析 `.cgui`、打印控件树、把每个绑了回调的事件都**真的调一遍**你的函数，
并检查回调名有没有在 C 侧登记、事件是不是这个控件支持的。

---

## 四、有哪些控件、各支持哪些事件

事件是**按控件特性**设计的，不是每个控件都有全套：

| 控件 | 支持的事件 | 备注 |
| --- | --- | --- |
| 按钮 / 图标按钮 / 链接 | `click`、`dblclick` | 图标按钮用 `src` 指定图片 |
| 开关 / 复选框 / 单选钮 | `change` | 开关是自绘的药丸形 |
| 单行输入 | `change`、`submit` | `submit` 是回车 |
| 多行输入 / 富文本 / 数字微调 / 日期时间 | `change` | |
| 列表框 | `select`、`dblclick` | |
| 下拉框 | `select` | |
| 滑块 | `change`、`release` | 拖动中给 `change`，松手给 `release` |
| 标签页 | `select` | 子控件用 `page` 指定属于第几页 |
| 树 | `select`、`dblclick`、`expand`、`collapse` | 展开/折叠时能拿到是哪个节点 |
| 表格 | `select`、`dblclick` | |
| 月历 | `select`、`dblclick` | |
| 菜单栏 / 菜单按钮 | `click` | 用 `cg_menu_last_id()` 区分点的是哪一项 |
| 工具栏 | `click` | 用 `cg_widget_value()` 拿是第几个按钮 |
| 标签 / 进度条 / 圆环进度 / 状态栏 / 分割线 / 容器 | 无 | 只用来显示，或被程序写入 |

**窗口**本身有一个 `close` 事件（点关闭按钮前触发），在画布右侧「文档」页签里绑。

绑了某个控件不支持的事件时，运行时启动会打印警告、`--selftest` 会以非 0 退出。

---

## 五、进阶：在 CodeBuddy / VSCode 里用 clangd 写 C

配置好之后，写业务代码就有**补全、跳转、括号里实时报错**（比如 `cg_find` 拼错了立刻标红），
不用等编译才发现。

### 第 1 步：装 clangd 插件

用 CodeBuddy（或 VSCode）**打开本目录**（就是有 `build-app.bat` 的这一层），打开扩展面板
（`Ctrl+Shift+X`），搜索 **clangd**，安装 `clangd` 插件（作者 llvm-vscode-extensions，
VSCode 插件市场与 CodeBuddy 通用）。

### 第 2 步：装 clangd 本体

插件**不自带** clangd 程序，装完插件它会弹提示"clangd executable was not found"，点 **Install**
让它自动下载即可。也可以手动装，任选其一：

- 去官方发布页 <https://github.com/clangd/clangd/releases> 下载 `clangd-windows-x64.zip`，解压备用；
- 或装完整 LLVM（<https://llvm.org>，里面带 `clangd.exe`）；
- 或 `scoop install clangd`。

验证：命令行里 `clangd --version` 有输出就 OK。记住 `clangd.exe` 的完整路径，下一步要用。

### 第 3 步：生成 compile_commands.json

clangd 靠 `compile_commands.json` 知道每个 `.c` 文件用什么参数编译（本项目它在根目录，
相对路径、换机器不用改）。生成方式二选一：

```bat
build-app.bat hello     :: 编译成功后自动生成（推荐，顺便验证环境）
node tools\gen-cc.mjs   :: 或者只生成不编译
```

Node.js 去 <https://nodejs.org> 装（LTS 即可，tools\check-format.bat 那条回归测试需要 **22+**）。

### 第 4 步：告诉插件 clangd 在哪、怎么找系统头文件

打开设置（`Ctrl+,`），搜 `clangd`，点"在 settings.json 中编辑"，填（路径按本机改）：

```json
{
  "clangd.path": "C:\\clangd_19\\bin\\clangd.exe",
  "clangd.arguments": [
    "--query-driver=C:/mingw64/bin/gcc.exe",
    "--compile-commands-dir=."
  ]
}
```

两个参数的作用：

- `--query-driver`：`cstackgui.h` 会包含 `windows.h`，clangd 默认按 MSVC 找系统头，
  在 MinGW 环境会一片标红。这个参数让它去**问 gcc 系统头文件在哪**，路径填你装 MinGW 的
  `bin\gcc.exe`。
- `--compile-commands-dir=.`：配置文件就在根目录（默认也会找到，写上保险）。

改完保存，**重启窗口**（`Ctrl+Shift+P` → Reload Window）生效。

### 第 5 步：验证

打开 `examples\hello\main.c`：

- `#include "cstackgui.h"` 不标红；
- 在函数体里输入 `cg_` 出现 API 补全列表；
- `Ctrl+点击` `cg_app_run` 能跳进 `cstackgui.h` 的注释文档。

三个都过了就配置好了，写 `apps\<名字>\` 里的代码同样享受。

**让 AI 帮你写**：本包自带 AI 项目技能（`.codebuddy\skills\`）—— 用 CodeBuddy 打开本目录，
说"给确认按钮加个点击逻辑"，AI 会自动按 `cstackgui-dev` 技能里的 API 与验证流程写代码；
要手改或批量生成 `.cgui`，它会按 `cgui-edit` 技能里的字段契约来。

**Q：`windows.h` 还是标红？** `--query-driver` 路径不对，或 clangd 没重启。
**Q：提示 compile_commands.json not found？** 先跑一次 `build-app.bat hello`。

---

## 六、常见问题

**Q：改完界面，exe 里没变化？**
`.cgui` 是运行时读的，确认程序读的是你改的那个文件（启动时 stderr 会打印 `[cstackgui] 界面文件: ...`）。

**Q：启动说"找不到界面文件"？**
它会依次找：命令行给的路径 → exe 旁边 → `exe目录\..\<apps|examples>\<exe名>\<exe名>.cgui`，
最后把试过的路径都打印出来。按提示放文件，或者显式传路径。

**Q：事件名写错了，为什么编译不报错？**
绑定是"运行期按名字查表"，这样画布和 C 代码才能互不依赖。兜底有两个：
运行时会在 stderr 打印 `警告: .cgui 引用了回调 "on_xxx"，但 C 代码里没有 CG_BIND 它`；
或者跑 `--selftest`，它会以非 0 退出码报出来。

**Q：按钮圆角、自定义背景色设了没用？**
v1 用的是 Win32 原生控件，系统控件不给画圆角。开关、圆环进度、图片是自绘的，能吃配色。
要整套 M3 外观得等自绘后端。

**Q：`build-app.bat` 说找不到 gcc？**
见第三节第一步，装 MinGW-w64 或设 `MINGW` 环境变量。

---

## 七、示例

```bat
build-app.bat hello
examples\hello\hello.exe
```

`examples\hello\` 是最小闭环（分组框、按钮、输入、勾选、滑块、进度条、列表框）。
`examples\gallery\` 把每种控件都用了一遍，想找"某个控件怎么用"看它最快。

两个示例的 `main.c` / `main.c` 里都有注释说明每个回调在演示什么。
