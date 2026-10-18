# 入门 01 · 基础与容器：能点的、能显示的、能装东西的

> 目录 `examples\lessons\lesson01_basics\`　编译 `build-app.bat lesson01_basics`　运行 `examples\lessons\lesson01_basics\lesson01_basics.exe`　自检 `lesson01_basics.exe --selftest examples\lessons\lesson01_basics\lesson01_basics.cgui` → 控件 30 个 / 回调 10 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 这一课讲什么
把"搭界面"最基础的那批控件一次摆齐：能点的（按钮 / 图标按钮 / 链接 / 开关）、能显示的（标签 / 徽章 / 标签片 / 分割线）、能装东西的（分组框 / 容器块 / 卡片）。左边一列逐个演示"类型名 + 一个控件"，点一下看状态栏写什么；右边用三种容器演示"**子控件的坐标相对容器左上角**"。

## 怎么跑
1. 在项目根编译：`build-app.bat lesson01_basics`，双击 `examples\lessons\lesson01_basics\lesson01_basics.exe`。
2. 逐个点左边那一列控件，看底部状态栏 `sb_main` 的回应；禁用态那个按钮点不动。
3. 改容器里控件的坐标，重开程序看它们是不是"跟着容器走"。

## 关键控件一览（本课 30 个控件 / 10 个回调）
| 控件 id | 类型 | 看什么 |
| --- | --- | --- |
| `btn_primary` / `btn_disabled` | `button` | 单击写状态栏；禁用态点不动、也不发事件 |
| `icb_run` | `iconbutton` | `src` 放图，点击同样是 `click` |
| `lab_hello` | `label` | 纯显示，`cg_widget_set_text()` 改它 |
| `lnk_docs` | `link` | 蓝色下划线文字，只有 `click` / `dblclick` |
| `sw_notify` | `switch` | 开 / 关两态，读 `cg_widget_checked()` |
| `bad_count` / `tg_demo` / `sep_line` | `badge` / `tag` / `separator` | 徽章无事件，标签片有 `click`，分割线纯分隔 |
| `grp_form` / `box_panel` / `card_note` | `group` / `box` / `card` | 三种容器，子控件 `rect` 相对容器 |
| `sb_main` | `statusbar` | 本课所有回调都往它写一句 |

## 重点看哪几处
- **容器里的坐标是相对容器的**：`grp_form` 里的按钮写 `{x:8,y:36}`，不是窗口坐标。
- **禁用态只能用皮肤画得出的控件演示**（`button` / `checkbox` / `radio` / `switch`），别的控件 `enabled: false` 后和启用态一样。
- 回调签名固定 `void f(cg_widget_t *self, cg_event_t ev, void *user)`；事件名写错不报编译错，`--selftest` 会揪出来。

## 详细讲解
见 `docs\articles\01-基础与容器.md`（含逐控件说明与"动手改一改"），以及 `examples\教学使用说明.md` 的「入门 01」小节。
