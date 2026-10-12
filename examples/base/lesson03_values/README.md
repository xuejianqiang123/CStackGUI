# 入门 03 · 数值与指示：把数字显示出来

> 目录 `examples\lesson03_values\`　编译 `build-app.bat lesson03_values`　运行 `examples\lesson03_values\lesson03_values.exe`　自检 `lesson03_values.exe --selftest examples\lesson03_values\lesson03_values.cgui` → 控件 27 个 / 回调 6 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 这一课讲什么
只讲一件事：**值只有一份，显示方式可以有很多种**。界面里那个滑块是"值"的入口，进度条 / 圆环 / 仪表盘 / 数字标签都只是它的显示器 —— 看同一个数被画成多少种样子。

## 怎么跑
1. 在项目根编译：`build-app.bat lesson03_values`，双击 `examples\lesson03_values\lesson03_values.exe`。
2. 拖滑块，看进度条 / 圆环 / 两种仪表盘 / 数字标签一起跟着变。
3. 点「程序改值」按钮与状态灯，观察"程序改值"和"用户改值"两条路的差别；注意 `change` 与 `release` 各在什么时候发。

## 关键控件一览（本课 27 个控件 / 6 个回调）
| 控件 id | 类型 | 看什么 |
| --- | --- | --- |
| `sld_level` | `slider` | 值入口：`change` 拖动中连续发，`release` 松手才发一次 |
| `prg_level` / `ring_level` | `progress` / `progressring` | 只被程序写，`cg_widget_set_value()` 就是刷新 |
| `mtr_arc` / `mtr_half` | `meter` | 270° 弧与半圆两种外观 |
| `led_state` | `led` | 状态灯 |
| `rate_star` / `pg_page` | `rate` / `pagination` | 评分与分页，都能点 |
| `lab_level` / `lab_sld_mirror` | `label` | C 拼串 vs `.cgui` 里 `bind` 到滑块 |
| `sb_main` | `statusbar` | 所有回调都往它写一句 |

## 重点看哪几处
- **`change` 与 `release` 是两件事**：滑块拖动中 `change` 一直发，松手才发一次 `release`。
- **控件绑定的活例子**：镜像标签一行 C 都没有，跟着滑块走；要显示 `60 / 100` 这种拼出来的串只能写 C —— 绑定只整份抄值，要加工还得写 C。
- **程序用 `cg_widget_set_value()` 改值不会触发 `change`**。

## 详细讲解
见 `docs\articles\03-数值与指示.md`，以及 `examples\教学使用说明.md` 的「入门 03」小节。
