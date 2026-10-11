# 计划：examples/toolbox —— 工具箱启动器（学 mxx1-toolbox）

## spec（要什么、为什么）

参照 https://github.com/2604290100/mxx1-toolbox（萌新工具箱：多页签按钮墙、搜索、
一键启动外部程序、结果反馈、最近使用），用 CStackGUI 写一个**实用工具示例**：
`examples/toolbox/`。

调研结论（2026-10-11）：**画布无缺失项**。tabs / button / edit / listbox / statusbar
控件与 change/submit/click/dblclick/select 事件、`cg_run_process`/`cg_shell_open`
API 全部现成（`designer/src/model/kinds.ts` 42 种、`runtime/src/kit/cg_kit_sys.c`）。
本轮不动 designer/ 与 runtime/。

### 功能面（与 mxx1-toolbox 对应）
1. **多页签按钮墙**：tabs 五页（搜索 / 常用 / 系统工具 / 命令行 / 网络），每页按钮网格。
2. **配置驱动加按钮**：按钮在 .cgui 里定义，命令在 C 一张表里；注释写明
   "加一个工具 = .cgui 加一个按钮 + C 表加一行"（诚实的产品边界：本工程没有
   "纯 JSON 声明命令"的机制，不像 mxx1 的 tools.json）。
3. **搜索**：顶部输入框 change 过滤命令表 → 结果进"搜索"页 listbox 并自动切页；
   submit 启动第一个匹配。
4. **一键启动**：统一回调 `on_tool_click` 按控件 id 查表，`cg_run_process(cmd, 0)`；
   console 类用 `cmd /k` 保持窗口可见。
5. **结果反馈**：statusbar 成功/失败提示 + 底部日志 listbox（对应 mxx1 的绿/红结果条与日志）。
6. **最近使用**：内存环形 8 条，"常用"页 listbox 双击再次启动，可清空
   （selftest 不写盘，故不做持久化——照实写进 hint）。

## 改动清单与验证方法（每条都能失败）
| # | 改动 | 验证 |
|---|---|---|
| 1 | 新增 `examples/toolbox/main.c` | `build-app.bat toolbox` 零警告 |
| 2 | 新增 `examples/toolbox/toolbox.cgui` | `<exe> --selftest <cgui>` 报 未登记0/不支持0/绑定出错0/样式失效0；`--trace` 打印控件树与预期一致 |
| 3 | `README.md` 示例清单加一行 | `node tools/check-examples.mjs --docs` 过 |
| 4 | `examples/教学使用说明.md` §6 加一节 | 同上 |
| 5 | 全量门禁 | `build-all.bat` 全过；`tools\check-format.bat` 全过 |
| 6 | 真机截图 | `tools\shot.ps1 toolbox.exe toolbox.cgui build\toolbox.png` 人眼核对布局 |

## 边界 / 异常清单
- selftest 红线：`on_tool_click` / 双击启动里先 `cg_app_has_window()` 判定，
  无窗（自检）时只记内存状态不真启动。
- `cg_run_process` 失败（-1）要反馈到状态栏，不许静默。
- 搜索结果为空时 listbox 留空 + 状态栏提示"没有匹配"。
- 缓冲区：所有 snprintf 用定长局部缓冲；路径/命令长度按表内常量 cap。
- 就绪钩子不自动启动任何工具、不读盘。

## 回滚方式
全部改动是新目录 + 两处文档行，`git checkout -- README.md examples/教学使用说明.md`
+ 删 examples/toolbox 即可干净回退；不碰 runtime/、designer/。

## 禁止改动面
runtime/ 与 designer/ 一律不动（本轮结论：无缺失项）；不改 build-all.bat（自动扫目录）。
