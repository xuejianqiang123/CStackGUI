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
| `suites/studio` 通信 | serialterm(1740) netterm(995) darkterm(325) modbus(522) framedec(659) | 4241 |
| `suites/studio` 文本数据 | notepad(425) logfilter(710) filecheck(720) dataview(1106) htmlreport(775) datarec(431) | 4167 |
| `suites/sys` 系统实用 | shottool(1265) traytool(367) syspanel(512) envsnap(1430) settings(481) | 4055 |
| `suites/studio` 开发工具 | hextool(576) encconv(1039) calculator(1203) drawpad(511) palette(281) | 3610 |

（`base/` 里的 darkterm/datarec/dataview/drawpad/encconv/envsnap 六个工具随之迁出，归入上表。）

## 四、套件的模块化结构

```
suites/studio/
  studio.cgui               全部界面：一个 tabs，每页 = 原某工具的界面
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

1. **P0 试点：`suites/studio`**（hextool encconv calculator drawpad palette，3610 行）
   —— 打通完整模式：.cgui 合并 + 模块拆分 + id/事件前缀 + main 入口 + 门禁重建。
   **已交付（2026-10-18，全量门禁绿）**：
   - `tools/suite-merge.mjs`：.cgui 合并的单一真相（读 N 份 → id/事件/样式/角色/动作全量
     加前缀 → 拼成一个 tabs）。dry-run 先报再写。
   - `examples/suites/studio/`：`studio.cgui`（tabs 五页 47 顶层控件）+ `main.c`（仅入口：拼绑定表
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
2. **P1：`suites/studio`**（6 工具，同模式）
   **已交付（2026-10-18，套件门禁 + 双自检 + check-format 全绿）**：notepad(3)/logfilter(4)/
   filecheck(4)/dataview(16)/htmlreport(7)/datarec(8) → `suites/studio`（tabs 六页、窗口 1080x740）。
   **P1 首跑抓到 P0 没暴露的三个合并盲区（都已修进转换器/门禁，并回改 dev 统一口径）**：
   - **`actions[].handler` 是回调名**：合并器只改了动作表的键、没改 handler（dev 5 个工具
     0 个 action 所以没暴露）。不修就是"动作出错"（自检 exit 4）。门禁同步把 action.handler
     纳入双向核对。
   - **窗口事件一个槽只容一个回调**（运行时把 `app.events.close` 存进 `root->ev[close]`）：
     6 个工具里 5 个都要 close —— 首胜会静默吞掉其余工具的窗口职责（notepad 的未保存拦截
     cg_cancel_close / filecheck 的写窗口尺寸 / lf·hr 的收尾记账）。**正解：分发器模式**
     （`.cgui` 的 app.events 一律写 `suite_<事件>`；main 的分发器按序调各模块的
     `<pre>_app_<event>` 直连转发；模块绑定表里那条 CG_BIND 由转换器删掉，不留死绑定）。
     dev 已回改同一口径（它只有 encconv 用窗口事件，但模式统一后加工具=加一行）。
   - **菜单树挂在 `nodes` 而非 `children`**：rename() 只递归 children，整个菜单树被跳过 ——
     菜单项引用的动作键没改名，运行时报"菜单引用了不存在的动作"（4 项）。rename 现在也
     递归 nodes；门禁加"动作引用必须存在于 actions 表"核对。
   roundtrip 门禁自动跟上：30 原工程 + 2 套件 = **32/32**。
3. **P2：`suites/studio`**（5 工具，含 serialterm 旗舰）
   **已交付（2026-10-18，套件门禁 + 自检 + check-format 全绿）**：serialterm(1740)/netterm(995)/
   darkterm(325)/modbus(522)/framedec(659) → `suites/studio`（tabs 五页、窗口 1000x860、
   控件 150 / 回调 59，各类错误全 0）。close 与 destroy 各一个分发器（4 个工具都要，
   里面的杀定时器/撤轮询/关端口是真清理）。
   **P2 抓到的第四个盲区（比前三个更普遍，修在转换器里）：剥 main() 不能一剥了之** ——
   通用扫描发现 17 个工具的 main() 里有非 desc 语句，分三类：
   - 启动日志（`cg_log("...启动，界面文件")`，11 处）：随入口丢，无功能损失；
   - **工具级初值**（darkterm 的 `s_biz.autoscroll = 1`）：丢了就与 .cgui 的初始勾选态
     不一致 —— 现在**分类搬进 init**（darkterm 没有 on_ready，由转换器生成 init）；
   - **命令行语句**：`--demo` 这类**自我识别的旗标**（serialterm/modbus）经
     `<pre>_parse_cli` 搬走并在套件 main 里转发（`studio.exe --demo` 两个模块同时进演示，
     真窗口实测 18 行 [demo]；`--selftest` 下原版也是 0 行，套件与原版逐行等价）；
     argv[1] 开文件类（dataview/encconv/htmlreport）**一个 exe 分不清该给谁**，
     不搬但**明确报丢弃**（静默丢=假绿）—— 这是套件的已知限制。
   分块器计入**圆括号**深度（只算大括号会把 `for (i=1;i<argc;i++)` 切成三块，首跑抓到）。
   一致性核验：用新工具重生成 dev/text **逐字节相同**（工具变更零回归）。
4. **P3：`suites/sys`**（5 工具）
   **已交付（2026-10-18，套件门禁 4 个 + 自检 + 真窗口 + check-format 全绿）**：
   shottool(1265)/traytool(367)/syspanel(512)/envsnap(1430)/settings(481) → `suites/sys`
   （tabs 五页、窗口 1000x640、控件 118 / 回调 36，错误全 0）。窗口事件 7 个
   （close/tray/keydown/size/move/show/hide）：只有 close 两个工具都用（真 wrapper），
   其余单消费者**直接把 suite_<事件> 绑到模块的直连转发函数**，少一层壳。
   **本批抓到的第五个盲区（最阴的一类，且 P1 已带病）：C 代码里按字符串消费动作名**
   ——`strcmp(name, "save_as")`、`cg_hotkey_set(app, hk, "shot")`。动作键加了前缀
   而 C 串不改 = **点菜单没反应 / 全局热键白注册的静默失效**（selftest 抓不到：
   回调照常触发，只是分支不匹配）。全仓 10 处（notepad/shottool/traytool），
   其中 notepad 那处意味着 P1 的"另存为"已悄悄退化成"保存"。
   修法两层：转换器 4b（只在 strcmp/cg_hotkey_set/cg_action 消费行上改，逐行报）+
   **门禁反向兜底**（C 源里不得残留旧动作名字面量）——门禁在修转换器**之前**先红，
   当场抓住 P1 存量隐患，随后修复转绿。
   **两处进程级单例的取舍（写进 sys/main.c 文件头）**：
   - 托盘图标每进程一张、`app.events.tray` 一个槽，而 shottool 与 traytool 语义冲突
     （左键截图 vs 显隐切换）→ **点击归 traytool**（窗口化套件的通用默认）；
     shottool 保留它的按钮与全局热键（文档里托盘本就是"兜底入口"），无能力损失；
     提示语由 init 顺序后者生效，与点击归属一致。
   - 加速键冲突（双方都用 Ctrl+Shift+Q 退出）→ 合并器**先到先得去重**
     （运行时原语义"后声明让位"会记 bad_actions → exit 4，提前拆掉）；
     两边退出职责相同（摘托盘 + cg_quit），保留哪条都能正确退出。
   **真窗口验证**（自检的"没窗口"守卫会跳过托盘与热键，必须真跑）：进程存活 4 秒、
   日志给出 `全局热键已注册：Ctrl+Alt+A -> 动作 sh_shot`（4b 修复在真路径生效）
   与 `托盘图标已建好`（单图标、第二次 setup 为 MODIFY）。
5. **P4：六课迁 `lessons/` + 22 个原目录删除 + 教学说明 399 行路径同步 + 门禁收口**
   **已交付（2026-10-18，门禁全绿）**：`examples/` 现在只有一种布局 ——
   `lessons/`（六课）· `suites/`（四套件）· `gallery` · `toolbox` · `cppdemo`，`base/` 已消除。
   - **删前先归档**：21 份原 README 附迁移横幅存进 `suites/<suite>/docs/<tool>.md`
     （坑位文档是真金白银调试出来的，横幅注明"文中的命令口径已失效、其余逐条成立"）；
     `modbus/mock-slave.mjs` 是功能性测试助手，搬进 `suites/studio/`。
   - **路径同步**：26 个文件约 180 处（分级替换：先文件级精确——exe/cgui 改名处，再目录级
     通用），通用替换造成的两类破损（文件名错、工具区分丢失 17 处）用专项扫描抓出并精确修复
     —— 同句两工具并成一个套件时，用「serialterm 页（`modules\st.c`）」恢复区分。
     两篇深度文章（07 记事本 / 08 serialterm）补迁移注记（数字取自合并前独立运行）。
   - **门禁**：check-examples 认 `lessons\` 并吐真实路径（事实表重生成回贴）；make-release
     的自检示例从 notepad 换成 **suites\studio**（多模块套件，连随包工具链的 modules\ 子目录
     编译一起验）；probe-hotreload 改指 studio.cgui；build-all 注释更新（容器目录逻辑本就通用，
     lessons\ 与 suites\ 直接受益）。
   - **验证**：build-all 新布局 13 工程全编、roundtrip 13/13、**docs 口径 122 条声明全过**
     （约 180 处路径改写后口径仍成立）、冒烟 43/43、套件契约 4 个；发布包内编译 text 套件
     + 自检全通（79 控件/39 回调/错误全 0）。kit 冒烟仍被本机剪贴板占用（同 P1~P3 判定）。
6. **P5：索引与文档**（`examples/README.md` 自动索引：按套件列工具、点即跑命令）
   **已交付（2026-10-18，门禁全绿）**：新布局的门面 —— `examples/README.md` 三段式
   （六课表 / 四套件表 / 独立工程表）。**页签名直接取自套件 .cgui 的 tabs.items**
   （合并器写进去的就是工具名，不另立名单），文档份数从 `docs\*.md` 数、编译命令按套件名
   生成 —— 由目录生成、`--docs` 门禁逐字回校（新增第 123 条声明）。变异测试验过会红：
   手改索引一行 → docs 门禁 exit 1 并点名 README。
   **整体验收（§八 全部达成）**：`examples/` 只有一种布局；4 套件编译 + 自检回 0；
   六课路径与 docs 门禁全绿（123 条声明）；roundtrip 13/13；各文档路径与现状一致；
   每个工具的照抄价值保住（`modules\<tool>.c` 内是该工具的完整逻辑）。发布包侧同样验过：
   包内编译 text 套件 + 自检全通（随包工具链对 modules\ 子目录的支持）。

P0 未验证模式前不动 P1~P5。每批结束跑 `tools\run-tests.bat --full` + `make-release.bat`。

## 八、验收标准（完成的定义）

- `examples/` 只有一种布局（组/工程两级），无平铺残留；
- 4 个套件各能 `build-app.bat <套件>` 编译、`--selftest` 回 0（全页签回调闭环）；
- 六课路径与 `check-examples --lessons/--docs` 全绿；
- 30→约 14 个 roundtrip 全绿；
- 教学使用说明与各文档路径与现状一致（122 条声明口径仍成立）；
- 每个工具的**照抄价值**保住：`modules/<tool>.c` 内是该工具的完整逻辑（不散落 main.c）。
