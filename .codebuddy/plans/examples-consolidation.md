# 示例工程合并方案（物理合并 + 分层模块化）

> 2026-10-18 立项。用户拍板的两条硬决策：① **物理合并，但要分层模块化**（main 只做入口，
> 功能拆子模块，模块化编程）；② 教学六课**一起归入统一结构**（整个 examples/ 只有一种布局）。
> 本方案是执行计划的单一真相；改这里必须同步改实施。

## 一、为什么合并（第一性前提，防止走偏）

- **冗余是幻觉，不是合并的理由**：21 个工具间仅 34 个重名函数，其中 90% 是事件回调的
  约定命名（`on_close`/`on_destroy`，由控件 id 派生，本就该同名）与 3~5 行的极薄辅助。
  通用逻辑（CRC/编码/文件/字符串）早已全在 `cg_kit_*`，各 main.c 写的是各自的业务编排。
- **真实痛点**：无统一入口（30 个 exe 平铺）、无索引（找工具靠翻目录）、**两种布局混着**
  （`base/` 二级 vs 平铺一级）、29 次重复那四行 README 流水。
- **合并的正当收益**：一个工程一个入口（多页签）、模块化保住"按功能查找"、布局统一。
- **合并的代价（用户已知悉并接受）**：352 个控件 id 与上百个回调要改名；22 份 .cgui 要合成
  4 份；教学说明 399 行与十几处门禁路径要同步。

## 二、目标布局（统一成"组/工程"两级）

```
examples/
  lessons/                教学六课（6 个子目录，顺序即内容，不合并）
    lesson01_basics/ …
  suites/                 工具套件（4 个，每个 = 一工程 + modules/）
    comm/  text/  sys/  dev/
  gallery/                控件陈列室（已是合并形态，保留独立）
  toolbox/                系统工具箱启动器（60+ Windows 工具，与本合并无关，保留）
  cppdemo/                C++ 示例（保留独立）
```

**布局唯一性**：所有条目都是 `examples/<组或工程>/…`，不再有"base 二级 + 平铺一级"两种。
`gallery/`、`toolbox/`、`cppdemo/` 是一级工程名（组即工程），与 `lessons/`、`suites/` 同级。

## 三、套件划分映射（21 工具 → 4 套件）

| 套件 | 合入的工具（原目录 → 原行数） | 合计行数 |
| --- | --- | --- |
| `suites/comm` 通信 | serialterm(1740) netterm(995) darkterm(325) modbus(522) framedec(659) | 4241 |
| `suites/text` 文本数据 | notepad(425) logfilter(710) filecheck(720) dataview(1106) htmlreport(775) datarec(431) | 4167 |
| `suites/sys` 系统实用 | shottool(1265) traytool(367) syspanel(512) envsnap(1430) settings(481) | 4055 |
| `suites/dev` 开发工具 | hextool(576) encconv(1039) calculator(1203) drawpad(511) palette(281) | 3610 |

（`base/` 里的 darkterm/datarec/dataview/drawpad/encconv/envsnap 六个工具随之迁出，归入上表。）

## 四、套件的模块化结构

```
suites/dev/
  dev.cgui               全部界面：一个 tabs，每页 = 原某工具的界面
  main.c                 仅入口：建窗口、页签调度、公共底栏、日志、主题
  modules/
    palette.c  palette.h         原 palette 的界面逻辑与其 k_bindings
    drawpad.c  drawpad.h
    hextool.c  hextool.h
    encconv.c  encconv.h
    calculator.c  calculator.h
```

**main.c 只做**：`cg_app_run` 前的准备、页签切换、公共组件（底栏/日志/主题/关于）。
**每个模块做**：自己的控件逻辑 + 导出 `k_bindings` 数组 + 导出 `<tool>_init(app)` 与
`<tool>_on_tab(app)`（页签切入时做初始化/刷新）。

main.c 汇总绑定：`static const cg_binding_t k_bindings[] = { …所有模块的绑定拼在一起… }`；
各模块用 `extern const cg_binding_t <tool>_bindings[]` 暴露，main 里逐个展开。

## 五、id 与事件名的命名契约（合并的技术核心）

实测 21 个工具共 352 个控件 id、**32 个撞名**（`sb_main` 撞 14 次、`btn_clear` 7 次、
`term_log` 7 次、`btn_copy` 4 次…）。撞名不改 = 合并后互相覆盖。

- **控件 id 加工具前缀**：`btn_clear` → `ht_btn_clear`（hextool）、`lc_btn_clear`（logfilter）。
  前缀用**两位小写缩写**（palette=`pl` drawpad=`dp` hextool=`ht` encconv=`ec` calculator=`cl`
  serialterm=`st` netterm=`nt` darkterm=`dt` modbus=`mb` framedec=`fd` notepad=`np`
  logfilter=`lf` filecheck=`fc` dataview=`dv` htmlreport=`hr` datarec=`dr`
  shottool=`sh` traytool=`tr` syspanel=`sp` envsnap=`en` settings=`se`）。
  **一律全量加前缀，不只改撞名的**——规则单一才可机械核对（"只改撞名的"会让 id 规则分裂，
  日后无人记得谁该有前缀）。
- **事件名同样加前缀**：`on_connect_click` → `st_on_connect_click`。`.cgui` 的 `events`
  值与 C 侧 `CG_BIND` 名一一对应，两边同改，且由门禁核对（见 §六）。
- **模块内可省前缀的私有函数**：模块自己的 static 辅助函数不必改名（各自 .c 编译单元隔离），
  但**非 static 的全局符号**（如各工具都有的 `log_line`/`set_status`）必须加前缀，否则链接冲突。

## 六、门禁策略（合并期间不能断）

| 门禁 | 合并前 | 合并后 |
| --- | --- | --- |
| roundtrip（30 示例） | 每工程一份 | 按**套件**（4）+ gallery + lessons×6 + toolbox + cppdemo |
| kind 覆盖（42 类） | 六课 + gallery | 不变（六课与 gallery 保留，仍覆盖 42 类；套件只增不减） |
| check-examples `--lessons` | 扫 `lessonNN_*` | 路径改 `lessons/lessonNN_*` |
| check-examples `--docs` | 回校 122 条声明 | 声明表随新结构重生成 |
| `--selftest`（每示例） | 30 条 | 按新工程数（约 14） |
| 事件绑定闭环 | 每示例自检 | 套件自检覆盖全模块（一个套件 exe 自检即验全部页签） |

**新增一道专用门禁**（合并引入的新风险，必须有判据）：`check-suite.mjs` —— 核对套件内
id 前缀唯一、`.cgui` 的每个事件名在对应模块的 `*_bindings[]` 里有登记（否则自检才报错，
而合并期最容易在这里漏）。

## 七、分批实施（每批门禁绿才提交）

1. **P0 试点：`suites/dev`**（hextool encconv calculator drawpad palette，3610 行）
   —— 打通完整模式：.cgui 合并 + 模块拆分 + id/事件前缀 + main 入口 + 门禁重建。
   **已交付（2026-10-18，全量门禁绿）**：
   - `tools/suite-merge.mjs`：.cgui 合并的单一真相（读 N 份 → id/事件/样式/角色/动作全量
     加前缀 → 拼成一个 tabs）。dry-run 先报再写。
   - `examples/suites/dev/`：`dev.cgui`（tabs 五页 47 顶层控件）+ `main.c`（仅入口：拼绑定表
     + 逐模块 init）+ `modules/{pl,dp,ht,ec,cl}.{c,h}`（5 个原工程完整逻辑）。
   - `tools/check-suite.mjs`（挂进 check-format）：事件双向一致 / 死绑定 / id 撞名 /
     前缀对得上模块；`sv_` 骨架前缀豁免。
   - **构建内核扩展**：cgbuild 的 `listSrcDeep` —— 工程源码改递归收集（排除 `lib\` 产物目录）。
     平铺工程行为不变（notepad/gallery/serialterm 实测编译自检照旧）。
   - **两个关键发现**（后续批次照此省力）：
     ① 各工具的回调**全是 static**（实测 5/5 零个非 static 函数、零个顶层全局变量）——
        **C 函数名不必改**，编译单元隔离；只需改 CG_BIND 字符串加前缀、导出绑定表。
     ② 运行时要**单个连续绑定表**，main 用 `append_bindings` 运行时拼装（各模块表以
        CG_BIND_END 结尾，遍历复制）。
   - 自检闭环：122 控件 / 68 次回调触发 / 未登记·不支持·绑定出错·动作出错·样式失效全 0。
2. **P1：`suites/text`**（6 工具，同模式）
3. **P2：`suites/comm`**（5 工具，含 serialterm 旗舰）
4. **P3：`suites/sys`**（5 工具）
5. **P4：六课迁 `lessons/` + 22 个原目录删除 + 教学说明 399 行路径同步 + 门禁收口**
6. **P5：索引与文档**（`examples/README.md` 自动索引：按套件列工具、点即跑命令）

P0 未验证模式前不动 P1~P5。每批结束跑 `tools\run-tests.bat --full` + `make-release.bat`。

## 八、验收标准（完成的定义）

- `examples/` 只有一种布局（组/工程两级），无平铺残留；
- 4 个套件各能 `build-app.bat <套件>` 编译、`--selftest` 回 0（全页签回调闭环）；
- 六课路径与 `check-examples --lessons/--docs` 全绿；
- 30→约 14 个 roundtrip 全绿；
- 教学使用说明与各文档路径与现状一致（122 条声明口径仍成立）；
- 每个工具的**照抄价值**保住：`modules/<tool>.c` 内是该工具的完整逻辑（不散落 main.c）。
