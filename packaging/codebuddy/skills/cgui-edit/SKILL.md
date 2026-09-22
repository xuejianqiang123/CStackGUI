---
name: cgui-edit
description: 手工编辑 .cgui 界面文件（JSON）。触发条件：用户直接改 .cgui、问字段含义/格式、要脚本或 AI 批量生成/修改界面 JSON 时自动触发。
---

# 手改 .cgui 界面文件

**任务**：$ARGUMENTS

## 核心原则

1. `.cgui` 是 UTF-8 JSON，**运行时才读它**（不编进 exe），改完不用重编译。
2. 语义真相在 C 侧解析器；写出去的字段是「非默认值才写」，照抄下面契约。
3. **改完必须验证**：`<exe> --selftest <文件>.cgui` 返回 0，或 `tools\check-format.bat` 整体回归。

## 顶层结构

```json
{
  "cgui": 1,
  "app": { "title": "main", "width": 700, "height": 500, "bg": "#F3EDF7",
           "events": { "close": "on_close" } },
  "widgets": [ { 控件... }, ... ]
}
```

`cgui` 必须是 1（>1 会被拒绝）；`events.close` 是窗口关闭前的回调（可选）。

## 控件字段（widgets 数组 / 容器的 children 里）

- 必填：`id`（唯一，`[0-9a-zA-Z_]`）、`type`、`rect: {x,y,w,h}`（整数；容器内**相对父控件**）
- 文字/样式：`text`、`style: {font,size,bold,fg,bg,radius,align}`（`align`: left/center/right）
- 事件：`events: { "<事件名>": "<C 函数名>" }`；函数名写错编译不报错，selftest 会揪出来
- 状态（等于默认值就**不要写**）：`visible`(true)、`enabled`(true)、`checked`(false)；
  `value/min/max` 只给数值型控件（slider/progress/progressring、带 `spin` 的 `edit`）
- 容器只有 `group`/`box`/`card`/`tabs` 能带 `children`（非容器带了会被挪到同级）；tabs 的子控件用 `page` 标归属第几页（0 起）

## 按类型的专有字段

| type（42 种全表） | 专有字段 |
| --- | --- |
| button / iconbutton / label / link / edit / checkbox / radio / richtext / monthcal / separator / group / box | `text`（checkbox/radio/switch 另有 `checked`） |
| switch | `text`、`checked` |
| edit（多行 / 数值框也用它） | `multiline` / `scroll` / `maxlen` / `conv` / `input` / `border` / `spin` / `min` / `max`。多行 = `multiline:true`（原来叫 `multiedit`）；带 `spin` = 数值框（原来是 `spinner`，此时用 `value` 而不是 `text`） |
| slider / progress / progressring | `value` / `min` / `max` |
| listbox / combo / toolbar | `items: ["一条", ...]`（toolbar 的当前选中存 `value`，-1 表示无） |
| tabs | `items: ["页签文字", ...]`（页签文字），当前页 = `value` |
| statusbar | `items: ["分段1", "分段2", ...]` |
| tree | `nodes: [{text, expanded, children:[...]}, ...]` |
| menu / menubutton | `nodes`（menu 没有矩形、挂在窗口上，一根窗口只放一个；项可带 `id`、`checked`，`text:"-"` 是分隔线） |
| table | `columns: [{title, w}]`、`rows: [["a","b"], ...]` |
| image / iconbutton | `src`（相对 .cgui 所在目录，png/jpg/bmp/gif） |
| datepicker | `format`: date / time / datetime（date 可省略），当前值在 `text`（2026-09-19 / 14:30） |
| colorpicker | 当前颜色在 `value`（`0xRRGGBB`），点击弹系统取色器 |
| timer | `period`（毫秒一跳，触发 `tick`） |
| rate / pagination | `value` / `min` / `max`（rate 星数 = `max-min`，上限 10；pagination 页数 = `max-min+1`） |
| card | 容器，可带 `children`（坐标相对卡片客户区） |
| canvas / browser / shape / badge / tag | 无专有字段（`text` 为主；canvas 用 `cg_canvas_*` 运行时画） |

## 读入容错（写错不会崩，但别指望生效）

- `type` 不认识 → 按 `label` 处理；rect 缺字段按默认尺寸补；`state{}` 旧写法兼容
- 事件名不在该控件支持集里 → 运行时警告、`--selftest` 退出码非 0
- 手改后最好用画布打开一次再保存（画布会做规范化清理）

## 验证命令

```bat
examples\gallery\gallery.exe --selftest 某文件.cgui  :: 单文件校验（不开窗）
tools\check-format.bat                            :: 整体格式契约回归
```
