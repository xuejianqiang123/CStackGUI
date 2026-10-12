# 入门 02 · 输入与选择：一个表单要用的全部输入控件

> 目录 `examples\lesson02_inputs\`　编译 `build-app.bat lesson02_inputs`　运行 `examples\lesson02_inputs\lesson02_inputs.exe`　自检 `lesson02_inputs.exe --selftest examples\lesson02_inputs\lesson02_inputs.cgui` → 控件 55 个 / 回调 29 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 这一课讲什么
把「填表」会用到的输入控件一次摆出来，每一种都配回调，结果只落在状态栏那一句：单行 / 多行 / 数值框三种形态的 `edit`、六种边框、多种输入档位，以及复选框、单选钮、下拉框、列表框、富文本、日期、月历。

## 怎么跑
1. 在项目根编译：`build-app.bat lesson02_inputs`，双击 `examples\lesson02_inputs\lesson02_inputs.exe`。
2. 依次点 / 填每种输入控件，看底部状态栏 `sb_main` 显示的"读到了什么"。
3. 故意在数值框 / 带输入档位的输入框里敲非法字符、或输完不写就切走焦点，看运行时怎么过滤与校验。

## 关键控件一览（本课 55 个控件 / 29 个回调）
| 控件 id | 类型 | 看什么 |
| --- | --- | --- |
| `edt_single` / `edt_multi` | `edit` | 单行与多行（`multiline` + `scroll`） |
| `edt_spin_auto` / `edt_spin_manual` | `edit`（`spin`） | 数值框：`value` 是真相，用 `cg_widget_value()` 读写 |
| `edt_byte` / `edt_int` / `edt_dt` / `edt_short` | `edit`（`input`） | 输入档位：非法字符不发事件，失焦夹到范围 |
| `edt_upper` / `edt_b1`..`edt_b6` | `edit` | 大小写转换、六种边框 |
| `chk_vip` / `chk_locked` | `checkbox` | 勾选取 `cg_widget_checked()`；`chk_locked` 是禁用态 |
| `rad_stu` / `rad_emp` / `rad_other` | `radio` | 同组互斥，共用一个回调 |
| `cmb_city` | `combo` | 下拉，值是下标 |
| `lst_skill` | `listbox` | 列表：下标用 `cg_widget_value()`、文字用 `cg_widget_list_item()` |
| `rt_note` / `dtp_date` / `dtp_time` / `cal_pick` | `richtext` / `datepicker` / `monthcal` | 富文本、日期 / 时间、月历 |
| `grp_user` | `group` | 容器，子控件坐标相对容器 |

## 重点看哪几处
- **三种输入框其实是同一个 `edit`**：多行 = `multiline` + `scroll`；带 `spin` 的就是数值框，读值用 `cg_widget_value()`。
- **多个控件共用一个回调 + `cg_widget_id(self)` 分流**是常用姿势（六个边框输入框 + 容器里的控件就共用 `on_form_change`）。
- 同组单选钮共用一个回调，直接读 `cg_widget_checked()` 判断选中谁。

## 详细讲解
见 `docs\articles\02-输入与选择.md`，以及 `examples\教学使用说明.md` 的「入门 02」小节。
