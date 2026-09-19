# CStackGUI

**拖拽画界面 → 存成 `.cgui` → C 写业务 → 编译出一个独立 exe。**

- 界面的长相在画布里拖出来，存成 `.cgui` 文件；**程序运行的时候才读它，不编译进去**。
- 业务逻辑是普通 C 文件，只写回调函数；**事件按控件特性分配**（标签没有交互、只有树有
  展开/折叠），画布只列出该控件真正支持的事件。
- 最终产物是**一个 exe，零依赖**：换台电脑复制过去就能跑。

```
designer\    画布（Electron + React） ──打包──>  release\CStackGUI.exe          ← 你用来画界面
runtime\     C 运行时库（Win32 后端） ──编译──>  build\lib\libcstackgui.a
apps\<名字>\  业务程序（.cgui + C 文件）──链接──>  apps\<名字>\<名字>.exe          ← 你交付给别人的
```

**你只是想用，不想看开发细节？** 直接看 [release\README.md](packaging/README.release.md) ——
那是发布包里的使用者说明，从双击 exe 到出 exe 全流程，不带开发内容。

设计细节见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)，界面文件字段见 [docs/CGUI-FORMAT.md](docs/CGUI-FORMAT.md)。

---

## 一、先弄清你要做哪种人

| | 只想画界面 | 要做出能跑的 exe |
| --- | --- | --- |
| 需要的东西 | **只要 `CStackGUI.exe`** | CStackGUI 项目全套 + MinGW + 画布 |
| 要装环境吗 | 不用，双击就跑 | 要装 MinGW-w64（见下）|

画布本身**不依赖** C 编译器：它只负责生成 `.cgui` 和 C 骨架文件。
只有"把骨架编译成 exe"这一步需要 gcc。

### 环境准备（只在要编译时才需要）

| 需要 | 版本 | 说明 |
| --- | --- | --- |
| MinGW-w64 gcc | 8+（实测 16.2） | 默认找 `C:\mingw64\bin`，装在别处就设 `set MINGW=D:\你的\mingw64\bin` |
| Node.js | 22.12+ / 24+ | **只有想自己改画布、重新打包画布时才需要** |

先用 `gcc --version` 确认一下；如果提示"不是内部或外部命令"，把 MinGW 的 `bin` 加进 PATH，或者直接设 `MINGW` 环境变量。

---

## 二、从双击 CStackGUI.exe 到跑起自己的程序

下面这 8 步就是完整流程。**第一次做大概 5 分钟**，之后改界面只要重复第 2、3、6 步（不用重新编译）。

### 第 1 步：打开画布

双击 `designer\release\CStackGUI.exe`。
（还没打包过？见第四节的打包命令；开发态也可以用 `cd designer && npm run dev`。）

### 第 2 步：摆界面

1. 工具条里先定**窗口标题、宽、高、底色**。
2. 左边「控件」面板：**点一下**把控件加到画布，或者**按住拖到想要的位置**。
3. 拖动控件改位置；选中后拖 8 个手柄改尺寸。开着「吸附」时会咬到 4px 栅格，并显示邻近控件的对齐参考线。
4. 把控件拖进「分组框」里，它会变成子控件（坐标相对分组框）。

想删就选中按 `Delete`；想复制按 `Ctrl+D`；撤销 `Ctrl+Z`。

### 第 3 步：定属性

选中控件，右边「属性」页签里改：

- **`id`**：控件的唯一名字。C 代码靠它找控件（`cg_find(app, "btn_ok")`），**起个好认的名字**。
- **文字**：按钮上显示什么。
- **位置尺寸 / 字体字号 / 文字色 / 背景色 / 对齐**。
- **状态**：初始是否显示、是否可点、复选框是否勾上、滑块进度值。

> 注意：目前运行时用的是 Win32 原生控件，所以**圆角（radius）和自定义背景色在按钮上不会生效**，按钮长系统原生样。文字色/背景色在标签类控件上是生效的。

### 第 4 步：绑事件（这一步是画布和 C 的连接点）

切到右边「**事件**」页签，给这个控件填 **C 函数名**。

**事件面板只列出这个控件真正支持的事件**——事件是按控件特性设计的，不是每个控件发一套全套：

| 事件 | 什么时候触发 | 谁支持 | 举例填什么 |
| --- | --- | --- | --- |
| 单击 | 点一下 | 按钮/图标按钮/链接/图片/菜单类 | `on_ok_click` |
| 双击 | 双击间隔内点第二下 | 同上 + 列表/树/表格/月历 | `on_ok_dblclick` |
| 内容/值变化 | 输入框改字、勾选框切换、滑块拖动 | 输入类、开关复选、滑块 | `on_name_change` |
| 回车提交 | 在单行输入框按回车 | 只有单行输入 | `on_name_submit` |
| 选择变化 | 选中项变了 | 列表/下拉/标签页/树/表格/月历 | `on_list_select` |
| 拖动结束 | 滑块松手（拖动中只有内容变化） | 只有滑块 | `on_level_release` |
| 展开/折叠 | 树节点展开/折叠 | 只有树 | `on_tree_expand` |
| 关闭前 | 用户点关闭按钮 | 只有窗口（「文档」页签里绑）| `on_close` |

嫌麻烦就点每行后面的「**自动**」按钮，它会按 `on_<id>_<事件>` 生成规范名字。

**一个函数名可以被多个控件共用**（画布会标「共用」）。
绑了某控件不支持的事件时，运行时启动会警告、`--selftest` 会报错。

### 第 5 步：保存

`Ctrl+S` 存成 `.cgui`，比如 `apps\myapp\myapp.cgui`。

这一步存的是**界面本身**，和 C 代码无关。以后改界面只要重存这个文件。

### 第 6 步：导出 C 骨架

点工具条上的「**导出 C 骨架**」，选 `apps\` 目录（画布会自己建 `apps\<你的标题>\`）。

它生成 5 个文件，**每个都有明确分工**：

| 文件 | 是谁的 | 要不要改 | 作用 |
| --- | --- | --- | --- |
| `<名字>.cgui` | 画布产物 | 别手改，回画布改 | 界面定义 |
| `<名字>_ui_events.h` | 生成，勿改 | 不要 | 回调原型 + 绑定表宏 `CG_BIND(...)` |
| `<名字>_ui_events.c` | **你的代码** | **主要就改它** | 每个事件一个空函数，等你填逻辑 |
| `main.c` | 生成 | 一般不动 | `main()` + 命名绑定表，一般不用动 |
| `build.bat` | 生成 | 一般不动 | 目录不在 `apps\` 下时用的编译脚本 |

**重新导出不会覆盖你已经写好的 `<名字>_ui_events.c`**，放心改界面再导出。

> 画布右下角会一直显示「下一步」：文件放哪了、接着敲什么命令，照着做就行。
> 如果工具条显示没找到项目根，点一下「定位项目根」，选中 CStackGUI 项目根目录（含 `runtime\`、`apps\` 的那一层），画布会记住它。

### 第 7 步：填业务逻辑，然后编译

打开 `apps\<名字>\<名字>_ui_events.c`，把里面的空函数填上：

```c
void on_ok_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);                    /* 从控件拿到应用 */
    cg_widget_t *log = cg_find(app, "lst_log");         /* 用 id 找别的控件 */

    cg_widget_list_addf(log, "按钮被点了");              /* 往列表框写日志 */
    cg_widget_set_text(cg_find(app, "lab_status"), "已处理");
}
```

然后在 **CStackGUI 项目根目录**编译：

```bat
build-app.bat myapp
```

它会先把运行时库刷新一遍，再把产物落到 demo 自己的目录里：

```
apps\myapp\
  myapp.exe           <- 双击就能跑（界面文件就在旁边）
  lib\libcstackgui.a  <- 运行时库副本，整个目录拷走也能重编译
  build-app.bat       <- 双击就能重编译这一个 demo
  myapp.cgui / main.c / assets\...
```

### 第 8 步：运行

```bat
apps\myapp\myapp.exe
```

**双击 `apps\myapp\myapp.exe` 也一样**：程序会自己去找界面文件（先找 exe 旁边，再找 `..\apps\<名字>\<名字>.cgui`）。
也可以显式指定：

```bat
apps\myapp\myapp.exe apps\myapp\myapp.cgui
```

想只校验不开窗（比如放进 CI）：

```bat
apps\myapp\myapp.exe --selftest apps\myapp\myapp.cgui
```

它会解析 `.cgui`、打印出控件树、把每个绑了回调的事件都**真的调一遍**你的函数。
如果有事件名在 C 侧没登记，它会列出来并以非 0 退出——这是唯一能自动发现"名字写错"的办法。

### 之后改东西

| 你改了什么 | 要做什么 |
| --- | --- |
| 界面（位置、文字、加控件、改事件名） | 画布里改 → `Ctrl+S` 覆盖 `.cgui` → **直接重跑 exe，不用重新编译** |
| 业务逻辑（回调函数体） | 改 `_ui_events.c` → `build-app.bat <名字>` → 重跑 |
| 动了 `runtime\` | 直接重新 `build-app.bat <名字>` 就行（它会先刷新运行时库）|

**第一行是这个项目最大的省事之处**：调个按钮位置不用重编译 C。

---

## 三、有哪些控件

29 种，全部是**真能用的原生控件**（点得动、能输入、能改状态、能绑事件），不是画着好看的图。

| 分类 | 控件 | 说明 |
| --- | --- | --- |
| 基础 | 按钮 / 图标按钮 / 标签 / 链接 / 开关 | 开关是自绘的药丸形；图标按钮用 `src` 指图片；链接带手型光标和下划线 |
| 输入 | 单行输入 / 多行输入 / 富文本 / 数字微调 / 日期时间 / 月历 / 复选框 / 单选钮 / 列表框 / 下拉框 | 微调框带上下箭头；日期控件支持日期/时间/两者；月历选中日期写回 `text` |
| 数值 | 滑块 / 进度条 / 圆环进度 | 滑块拖动中发 `change`、松手发 `release`；圆环进度是自绘的 |
| 数据 | 标签页 / 树 / 表格 / 状态栏 | 标签页的子控件按 `page` 分页，切页自动显隐；树支持 `expand`/`collapse` 事件 |
| 菜单 | 菜单栏 / 菜单按钮 / 工具栏 | 一套菜单结构，两种用法 |
| 容器 | 分组框 / 容器块 | 里面的子控件坐标相对容器，可嵌套 |
| 其它 | 分割线 / 图片 | 图片支持 png/jpg/bmp/gif，按控件大小缩放 |

每种控件支持哪些事件见 [docs/CGUI-FORMAT.md](docs/CGUI-FORMAT.md) 第 3 节的对照表。

用法要点：

- **菜单栏**：没有矩形，运行时挂在窗口上（一根窗口只能有一根）。在「属性」→「菜单项」里按缩进写：

  ```
  文件
    新建 | m_new
    打开 | m_open
    -
    退出 | m_quit
  视图
    状态栏 | m_status | v
  ```

  规则：两空格一层；`文本 | 菜单id`；`-` 单独一行是分隔线；末尾再加 `| v` 表示勾选。
  点中任意一项都会给菜单控件派发 `click`，在回调里用 `cg_menu_last_id(self)` 区分是哪一项。
- **菜单按钮**：同样的写法，点按钮会在下方弹出菜单。适合"更多操作"这类次要入口。
- **工具栏**：纯文字按钮（没有图标），按钮文字一行一个；点第几个用 `cg_widget_value(w)` 取（0 起）。
- **标签页**：把别的控件拖进标签页，它会自动归到当前显示的那一页；「属性」→「所在页」改归属。
- **树**：缩进写（两个空格一层），也可以在 C 里用 `cg_tree_add()` 动态加。
- **表格**：「列」逗号分隔，可写 `标题:宽度`；「行」一行一条，单元格逗号分隔。
- **富文本**：能放多行大段文字；系统没有富文本组件时自动退回普通多行输入，不会启动失败。
- **日期时间**：`format` 选日期 / 时间 / 两者（两者是两个控件并排拼的）。值就是 `text`，
  写法 `2026-09-19`、`14:30`、`2026-09-19 14:30`；回调里用 `cg_widget_text(self)` 读。
- **月历**：同样把选中的日期放在 `text` 里（和日期控件同一种写法，两边可以直接互通）。
- **图标按钮**：`src` 指定图片，等比缩放到按钮内；换图用 `cg_widget_set_image()`。
- **图片**：填 `src` 路径，**相对 `.cgui` 所在目录**（`.cgui` 和 `assets\` 一起搬走就能跑）。
  画布上只显示文件名，运行时才画真图。
- **状态栏**：分段文字一行一个，用 `cg_status_set(w, 段号, 文字)` 改任一段。

`apps\gallery\` 是这些控件的陈列室，可以直接编译运行看效果。

## 四、画布操作速查

| 操作 | 说明 |
| --- | --- |
| 左侧控件面板 | 点一下加到画布；按住拖到鼠标位置 |
| 拖动 / 8 向手柄 | 改位置和尺寸；「吸附」开着时对齐 4px 栅格 + 邻近控件参考线 |
| 拖进分组框 | 变成该容器的子控件，坐标转为相对容器 |
| 右侧「属性」 | id、文字、位置尺寸、字体、颜色、对齐、初始状态 |
| 右侧「事件」 | 给控件填 C 函数名；「自动」按 `on_<id>_<事件>` 生成 |
| 右侧「文档」 | 概览、当前选区信息、C API 速查 |
| 左侧「层级」 | 数组顺序 = 层叠顺序（后面的盖前面的），`↑`/`↓` 调前后 |
| **▶ 运行（F5）** | 保存 → 调项目根 `build-app.bat` 编译 → 启动 exe；编译输出显示在左下面板 |
| 右下角「下一步」 | 导出后告诉你接着改哪个文件、敲哪条命令 |

快捷键：`Ctrl+S` 保存 · `Ctrl+O` 打开 · `Ctrl+E` 导出 C · `F5` 一键运行 · `Ctrl+Z`/`Ctrl+Shift+Z` 撤销重做 ·
`Ctrl+D` 复制控件 · `Delete` 删除 · 方向键微调（`Shift` 8px）· 鼠标滚轮缩放画布视图。

---

## 五、命令速查

### 编译 C（项目根目录执行）

```bat
build-app.bat myapp              :: 刷新运行时库 + 编译 apps\myapp\ -> apps\myapp\myapp.exe（带控制台）
build-app.bat myapp --release    :: 发布版：-mwindows，没有控制台窗口
build-all.bat                    :: 运行时 + examples\ 下所有示例，一条命令
build-runtime.bat                :: 只编译运行时库（平时不用单独跑，build-app 会先跑它）
```

在工程目录里双击 `apps\myapp\build-app.bat` 效果一样 —— 它只是带着工程名字去调项目根那个脚本
（这样编译逻辑只有一份，不会两个脚本各改各的）。

不用脚本、在 IDE 里手动配的话：

```
gcc -std=c11 -O2 -Iruntime\include -c apps\myapp\*.c -o main.o
gcc -o myapp.exe main.o apps\myapp\lib\libcstackgui.a -lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus
```

（中间产物 `.o`、运行时库源头都在 `build\` 下，那是纯构建目录，可以随时删。）

### 画布（designer 目录里执行）

```bat
npm install
npm run dev          :: 开发态：vite + electron（脚本自己等端口）
npm run build        :: 只构建前端 -> designer\dist
npm run dist         :: 打包成 designer\release\CStackGUI.exe
```

### 验证

```bat
build-app.bat gallery                                         :: 先有 gallery.exe
examples\gallery\gallery.exe --selftest examples\gallery\gallery.cgui  :: 不开窗跑一遍所有回调
tools\check-format.bat                                        :: 画布写出的 .cgui 必须能被 C 侧读懂
```

最后一条是**格式契约回归**：画布用 TypeScript 写 `.cgui`、运行时用 C 读它，
改了字段或控件类型后跑一下，防止出现"画布能存但程序起不来"。
改 `runtime\src\cg_doc.c` 的控件类型表或 `designer\src\model.ts` 时请务必跑。

---

## 六、两个可以直接跑的完整示例

### `examples\hello\` —— 基础控件

14 个控件、9 个回调：

- 分组框 + 4 个子控件（演示容器和相对坐标）
- 按钮绑 `click` + `dblclick`（双击会把滑块拉满，2 秒后自动回落——演示 `cg_set_timer`）
- 输入框 `change` + `submit`、复选框 `change`、滑块 `change` 联动进度条
- 列表框当日志用（`cg_widget_list_addf`）
- `on_ready` 钩子里挂业务数据（`cg_widget_set_user`）

### `examples\gallery\` —— 控件陈列室

35 个控件、24 个回调，把 29 种控件几乎全用上了：

- 菜单栏三项（文件/编辑/视图），带分隔线和勾选项；退出走 `m_quit`
- 工具栏文字按钮；「更多操作」是弹出菜单
- 日期 / 时间 / 日期+时间三种日期控件 + **月历**（点某天写回 `text` 并联动日期控件）
- **链接**（手型光标）与**图标按钮**（`src` 指图片）
- **圆环进度**（自绘）跟着数字微调框联动
- 富文本改了会置"未保存"标记，**关窗时 `close` 回调拦一次**（演示 `cg_cancel_close`）
- 树绑了 `select`/`dblclick`/`expand`/`collapse` 四种事件
- 表格在 `.cgui` 里声明列和行，点按钮追加行
- 状态栏第 1 段显示业务概况，第 2 段显示最近一次操作

```bat
build-app.bat hello   && examples\hello\hello.exe
build-app.bat gallery && examples\gallery\gallery.exe
```

---

## 七、常见问题

**Q：事件名写错了，为什么编译不报错？**
因为绑定是"运行期按名字查表"，这样画布和 C 代码才能互不依赖。兜底手段有两个：
运行时会在 stderr 打印 `[cstackgui] 警告: .cgui 引用了回调 "on_xxx"，但 C 代码里没有 CG_BIND 它`；
或者跑 `--selftest`，它会以非 0 退出码明确报出来。

**Q：程序启动说"找不到界面文件"？**
它会依次找：命令行给的路径 → exe 旁边 → `exe目录\..\apps\<exe名>\<exe名>.cgui` → 最后把试过的路径都打印出来。
按打印出来的位置放文件，或者显式传路径：`apps\myapp\myapp.exe apps\myapp\myapp.cgui`。

**Q：改了界面，为什么程序里没变？**
`.cgui` 是运行时读的，确认程序读的是你改的那个文件（看 stderr 里 `[cstackgui] 界面文件: ...` 那一行）。

**Q：按钮圆角/背景色设了没用？**
v1 用的是 Win32 原生控件，不给画自定义外观。要 M3 那种风格得加自绘后端（接口已留好，见 `docs/ARCHITECTURE.md` §8）。

**Q：打包出来的 CStackGUI.exe 说找不到 C 项目根？**
放对位置就能自动认出来：发布包里的 `CStackGUI.exe` 启动时会检查自己所在目录（便携版会带
`PORTABLE_EXECUTABLE_DIR`），`release\` 里恰好有 `runtime\include\cstackgui.h`，所以直接就认对了。
手动兜底：点工具条上的「定位项目根」选一次（会记住），或启动前
`set CSTACKGUI_HOME=D:\你的\CStackGUI`。

---

## 八、项目结构

**`examples\` 是仓库自带的示例，`apps\` 是你自己的工程**（画布「导出 C 骨架」的默认去处），
两者分开，别弄混。

```
CStackGUI\
  README.md                    本文件（开发者视角）
  docs\ARCHITECTURE.md         架构设计与取舍
  docs\CGUI-FORMAT.md          .cgui 字段规范 + C API 速查
  designer\                    画布源码（Electron + React + TS）
    src\model.ts               .cgui 数据模型 + KIND_EVENTS 事件表（与 cg_doc.c 对应）
    src\codegen.ts             导出 C 骨架
    electron\main.mjs          文件读写、导出、菜单、项目根记忆（便携版自动识别）
  runtime\                     C 运行时库
    include\cstackgui.h        业务代码唯一需要 include 的头
    src\cg_json.c              只读 JSON 解析
    src\cg_doc.c               .cgui -> 控件树，控件类型表（含每种控件支持的事件）
    src\cg_app.c               控件树、事件派发、公共 API、--selftest
    src\cg_win32.c             Win32 后端：窗口、原生控件、消息映射、自绘控件
    res\                       清单（v6 通用控件 + 高 DPI）与版本信息
  examples\                    仓库自带的示例
    hello\                     基础控件（hello.cgui / main.c / 产物 / 库副本 / 编译入口）
    gallery\                   控件陈列室（含 assets\）
  apps\                        你自己的工程（画布导出的默认位置）
    README.md                  这个目录是干嘛的
  build-runtime.bat            编译运行时库
  build-app.bat                编译某个工程 -> 落在它自己的目录里
  build-all.bat                运行时 + 所有示例，一条命令
  make-release.bat             一键打发布包 -> release\（内含自检）
  packaging\                   发布包里的物料模板（编译脚本、使用者说明）
  tools\                       开发期检查工具（格式契约回归测试等）
  build\                       纯构建目录：各 .o 和运行时库源头（可随时删）
  release\                     发布包（make-release.bat 的产物，gitignore）
```

---

## 九、打发布包（给没有这个仓库的人用）

```bat
make-release.bat
```

做四件事：全量构建 → electron-builder 打包画布 → 组装 `release\` → **在 `release\` 里
真编译真跑一遍示例自检**。产物自包含（画布 + 预编译运行时库 + 示例 + 编译脚本 + 使用者说明），
整个 `release\` 目录打包发人即可。使用说明是 `packaging\README.release.md`，改完重跑即可。

---

## 十、当前边界

- 运行时是 Win32 原生控件：稳、零依赖、输入法无障碍白送，但**圆角与自定义背景色在按钮等系统控件上不生效**
  （开关、圆环进度、图标按钮是自绘的所以吃配色；纯视觉的 M3 组件必须等自绘后端）。
- 窗口尺寸固定，没有布局引擎（绝对坐标，所见即所得，拉伸不重排）。
- 画布目前是**单选**，没有多选、组合、对齐工具条（有网格吸附和邻近参考线）。
- 画布不预览真实图片，只显示文件名。
- 菜单是**声明式**的：运行时能用 `cg_menu_last_id()` 读结果，但没有「运行时增删菜单项」的 API。
- 工具栏是纯文字按钮（没有图标支持）。
- 还没有「画布内一键运行」，得手动跑一次 `build-app.bat`（或双击 demo 里的 `build-app.bat`）。
