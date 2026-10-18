# 画布长期主义路线图（2026-10-18，用户声明"以长远目光决策"后重排）

> 决策原则：复利优先于功能增量；地基先于装修；每批带回归判据；验证先于动刀。

## G0 格式版本化与高版本拒绝 —— **已存在，无需做**（2026-10-18 验证）

- 版本字段 `"cgui": 1`：画布写（serialize.ts:319）/ 读（parse.ts:513 高版本拒绝）；
- runtime 同口径拒绝（cg_doc.c:890-892 "runtime speaks 1"）。
- 迁移框架：等 v2（布局）真实出现时把 cg_doc.c 里散装的"老写法自动升级"收编为
  迁移链——**为不存在的未来造抽象是反模式**，现在不做。

## G1 画布状态内核收口（下一个开工项）

**现状（已验证）**：edit(mutator) 单一入口 + 快照栈 undo/redo 形态正确（App.tsx:164-206）；
缺口 = 文档操作逻辑（addNode/deleteNodes/…）散在 App.tsx 组件体内，与 React 耦合，
不可 headless 测试。

**施工图（2026-10-18 按"协同是长期方向"升级——用户纠正：团队型开发是真实场景）**：
1. 新建 `designer/src/model/store.ts`：doc/past/future/dirty 的纯逻辑 store
   （不依赖 React；订阅用极简 listener 列表），edit/undo/redo/快照上限 200 全量平移；
2. 新建 `designer/src/model/ops.ts`：**操作 = 可序列化的命令对象** `{ type, payload }`，
   `apply(doc, cmd)` 纯函数——不是任意闭包。这一条是协同的架构前置：
   - 命令对象序列化后天然就是**操作日志（journal）**：远程重放、录制宏、
     "上次会话改了什么"全挂在同一形态上；
   - 未来 CRDT 化（操作可交换/可逆）的迁移路径现成，不用重写操作层；
   - 本地 undo 仍用快照栈（单机体验零变化），per-user undo 属协同基础设施阶段。
3. **id 生成策略同步升级**（现在改零成本，协同后改=全量迁移）：本地序号 →
   可读前缀 + 跨机器唯一后缀（时间戳+随机，ULID 形态），如 `btn_01K3…`；
   保持前缀可读（教学/绑定场景的辨识度不降）；
4. App.tsx 用一个 `useDocStore()` 薄 hook 接回（useSyncExternalStore），
   组件体只留 UI 胶水——1030 行 → ~700；
5. 配套：`tests` 侧用 tsc 编译 store/ops 的 headless 单测（增删改/undo/redo/命令
   序列化往返/边界），挂进 tools/ 门禁 —— 这是 G2"画布自动化验证"的第一块
   （文档操作层完全不需要 DOM）。

**验收**：tsc --noEmit 绿；store/ops 单测绿；.cgui roundtrip 门禁绿；画布行为零变化
（快照语义、200 步上限、dirty 标记时机逐条不变）。

**明确不动**：UI 组件树拆分（Toolbar/LayerTree 等已是独立文件，够了）；快照→Op 流派
（文档规模几十 KB，快照无痛点；协同需求复活前不议）。

## G2 画布自动化验证扩展（G1 附带起步，后续补）

- 已有：typecheck 门禁（2026-10-17）、.cgui roundtrip、kinds 门禁。
- 下一块：headless .cgui → parse → store → serialize 的性质测试（不依赖 Electron/DOM）；
- 再下一块（需要时）：Electron e2e 骨架（ Playwright ），判据 = 出现"只能人肉点出来"的 bug 时才投入。

## G2 收口（2026-10-18：行为冒烟落地，e2e 判据被"既成事实"提前满足）

**为什么不等到"出现人肉 bug"才做**：到 G5 收口时，UI 接线层已积压 **5 批改动零运行时验证**
（G1 状态内核 / G3' 预演 / G4 导入 / G4' 高亮 / G5 原语）—— tsc 与 vite build 只证明
"类型对、能打包"，证明不了"行为对"。这就是判据里"只能人肉点出来"的**存量**已经够大。

**交付**（`designer/electron/main.mjs` 的 `--smoke` 分支 + `tools/check-canvas-e2e.mjs`，
挂进 check-format）：
- 起**真 Electron 窗口**（先 vite build 保证测当前源码），在渲染层驱动**真实事件**：
  指针序列点调色板（调色板是 onPointerUp 触发 onAdd，合成 click 到不了；range 滑杆必须走
  原型原生 setter，否则 React 吞 onChange）、真 KeyboardEvent 敲 Ctrl+Z、切页签；
- 43 项行为断言：启动渲染 / 空文档 / 加控件 + 自动选中 / 撤销 / 预演开关 + 双滑杆 /
  拖滑杆画布窗口真的变宽 / 读数 / 支持库关键词预置 / **点表导入对话框全链路**
  （弹出 / live 预览报数 / 坏行逐行报错带行号 / 导入生成控件 / 一步撤销回基线）/
  **拖拽移动**（pointerdown 设拖拽 → move 过阈值压一次历史 → pointerup 提交进 doc，
  含"重渲后位置不变"—— 拖动中间是直写 DOM 的预览，这一步证明真的提交了而不是
  只活在预览里）/ **缩放手柄**（同协议但提交路径不同：逐帧 onApply）/
  **框选**（起点是客户区背景；多选视觉是主 .selected + 其余 .selected-2，
  断言数的时候两类都要算）/ **属性编辑链**（Inspector 输入框 → store → 画布控件文字，
  一步撤销）/ **拖放打开 .cgui**（合成带 File 的 drop 事件：换文档 / 窗口尺寸用文件里的
  值 / 控件id与文字渲染 / replaceDoc 后历史干净可继续编辑）
  —— 空间编辑三分支（move / resize / marquee）至此齐；
- 探针只碰 DOM 与真实事件、不 import 内部实现 —— 是行为测试不是白盒测试；
- **变异测试验过"会红"**：临时破坏 Canvas 的 sim 尺寸接线，冒烟当场 12/13 红 + 退出码 1
  （只能绿的冒烟是绿灯装饰，这条纪律与 check-canvas-ops 首跑抓真缺陷同源）。
- 已知边界：探针用语义化选择器（.app-window / .widget[data-node-id] / .palette-item /
  .tab / .sim-slider / .insp-row / .client-area / .handle-e），改这些类名会红着提醒同步；
  **仍未覆盖**（就剩两项）：**保存/导出**（走原生对话框，合成不了；打开路径已由拖放
  覆盖 —— 处理器是纯浏览器 API）、运行 F5 链（要真编译，属 serial_verify 范畴）。
- **不引 Playwright**：判据不变（规模不匹配 + 零新依赖纪律）；electron.exe 直接起。

## G3 布局体系 —— **取消，被既有设计覆盖**（2026-10-18 校准）

- 先验证再动刀的红利：`.codebuddy/plans/grow-layout.md` 是逐字契约级设计（带分配算法、
  反例推演、真机验证记录），用"余量分配"解决了"窗口拉伸不重排"——比 stack/grid 布局引擎
  更贴合本工程（绝对坐标仍是主体、纯函数可单测、splitter 同一心智模型）。
  引入布局引擎 = 重复建设 + 推翻已验证契约，无更强论据 ⇒ 取消。
- 契约 §7 的 v1 取舍②（一权重两轴同吃的悬边）：规避法（规则 3"给边界到带尾的那块"）已足够，
  第二字段（growx/growy）的价值/复杂度比不成立 ⇒ 维持 v1。**复议触发**：真实用户报悬边
  且规避法解释不清时。
- 市面调研 P2（等距分布）**已存在**：align.ts 12 种模式含 hdist/vdist/samew/sameh/samesize
  （上轮 grep 中文"分布"零命中是关键词假阴性）⇒ 从清单划掉。

## G3' grow 预演（取代原 G3 的画布侧价值，2026-10-18 立项）

- 真缺口：grow 是核心机制，但**设计时看不见效果**——只能 F5 编译运行（30s 反馈回路）。
- 边界内延伸：不违反"画布不做实时预览"（那条指设计时重排毁所见即所得；预演是**显式开关
  的只读模拟**，设计坐标不动）。
- 分两步：① model 层 `layoutsim.ts` 移植 cg_layout.c 的带分配算法（纯函数 + headless 测试，
  与 check-canvas-ops 同一挂点）；② UI：Toolbar"预演"开关 + 拉伸滑杆，画布按模拟结果渲染。
- **①②均已交付**（2026-10-18）：① commit 92d6d23；② 同日——预演开关 + 宽/高双滑杆
  （50%~200%，读数显示模拟窗口 px）、app-window 用模拟尺寸、WidgetView 按 simRects
  递归渲染、只读守卫（拖动/缩放/框选/调色板拖入/方向键/删除/复制全停；属性与层级树
  的编辑仍生效并实时重算模拟 = 改 grow 立刻见效）。
  只读边界的设计判据：预演锁的是**空间编辑**（对模拟坐标做编辑无意义），
  不锁**属性编辑**（属性驱动模拟，正是 0 秒反馈回路的一部分）。

## G4 工作流深度（市面调研结论，坐在 G3 地基上）

- 点表 CSV 批量导入（组态王"无法批量"的软肋 / MCGS"通道连接方便"的强项；
  `.cgui` 文本化是现成差异化地基）；**施工契约（2026-10-18 钉死，下一轮照此动刀）**：
  - CSV 契约：每行 `text,type[,值]`（text=控件文字，type=k_kinds 合法类型名，值可选）
    —— 列少而稳，坏行**逐行报错不停导入**（`第 N 行：<原因>`，好行照进）；
  - 解析器进 model 层（`model/tableimport.ts`，纯函数）→ 解析结果 = 批量
    `makeInsertCmd` 的 items（复用 G1 命令层：**一步撤销**、journal 天然记录）；
    自动排版 = 8 列网格、24px 行距（与 addNode 的 offset 启发式同口径）；
  - headless 断言进 `check-canvas-ops.mjs`（坏行容错/类型校验/批量命令 JSON 往返）；
  - UI 复用 ActionsDialog 的弹窗范式（先读它再写，别自创对话框骨架）。
- **点表 CSV 批量导入已交付**（2026-10-18，commit `f28936b`）：`model/tableimport.ts`
  纯函数解析 + `makeImportCmd` 批量命令；`TableImportDialog.tsx` 复用 ActionsDialog 弹窗
  范式；断言 34→44 项（`check-canvas-ops.mjs`），全链 + vite 绿。
- 选中控件 → 事件/支持库双向高亮（HMI"按了没反应"差评的画布侧解药）——**盘点已完成，
  施工方案见下面 G4' 节**。

## G5 Inspector 数据驱动（依赖 G1 的 ops 分层经验）

**现状盘点（2026-10-18，file:line 见 inspector 盘点记录）**：
- 11 个 Section 由 `Inspector.tsx:110-142` 无条件全渲染，各 Section 内 `return null` 自筛
  （EditSection:16、MediaSection:17、ActionSection:58、HintSection:19、GeometrySection:30）
  + KIND_MAP 标志（StateSection:47/63、ContentSection:154、BindSection:60）。
- **重复面（数据化直接收益）**：数字+夹取 18 处、下拉/勾选 40 处，三种数字形态
  （`Number||0` / `Math.round` / `±夹取+delete 归零`）与两种勾选形态（写 true / false 删字段）
  散在 GeometrySection:62、EditSection:73/189/204/214、StateSection:72/84/95、
  TypeSection:28/229/260/269/276/325、ContentSection:100、StyleSection:287/410。
- **字段描述表雏形已存在**：`model/style.ts:19 STYLE_KEYS` + `StyleSection.tsx:51-59
  FIELD_LABEL`（style.* 七字段可整族数据化）；`events.ts:131 KIND_EVENTS` 是同款
  "类型→能力"表范式。其余散表：RECT_LABEL(GeometrySection:14)、ITEMS_DRIVEN(IdentitySection:12)。
- **对抗性收缩（2026-10-18 开工时改判：不建 fieldSpecs 描述表）**：
  原计划新增 `fieldSpecs.ts`（46 类型 × 字段 → 编辑器/label/夹取/清空策略，250-350 行）。
  **判为不做**：那会造出**第二张手工镜像表**——本工程已经有一张并证明了代价
  （supportlib.ts 129KB + check-support 白名单防腐剂全套机制，架构师原话"同一事实两份"）。
  字段描述表会与 serialize.ts / cg_doc.c 漂移，而它带来的只是"少写 JSX"；
  且下面 8 类字段本来就禁止数据化，纯收益面进一步缩小。
  **替代做法**：把重复消灭在**渲染原语层** —— PanelUi 补 `NumField`/`SelectField`/`CheckField`
  （统一解析/夹取/取整/0 就删字段这套机械口径），各 section 用原语改写，字段语义仍留在
  各 section（那是真差异，不是重复）。这既是可验证的收益，**也是未来若真要数据化时的
  必要地基**（spec 表也是靠这几个原语渲染）。**复议触发**：出现"新控件加属性必须不改 React"
  的硬需求、且接受再维护一张镜像表时。
- **已交付（2026-10-18）**：PanelUi 三原语落地；EditSection（5 同形下拉 + 3 数值 + 1 勾选）、
  StateSection（3 勾选 + 3 数值）、GeometrySection（grow）改用原语，选项文案集中为常量。
  两处原语义**逐字保住**并写进注释：StateSection 的 value/min/max 不取整（`round={false}`）、
  max 兜底是 1 不是 0（`fallback={1}`）；CheckField 的 label 给 ReactNode 而非 string，
  免得原语替调用方决定观感（StateSection 是裸文字、EditSection 是 muted small）。
  tsc / vite / canvas 契约 / roundtrip / kinds / layers / electron / comments 全绿。
- **第二批已交付（2026-10-18）**：TypeSection 的数值与勾选改用原语（timer 周期 / LED 状态 /
  串口波特率-数据位-停止位-校验 / 终端最多行数 / 图表点数与 Y 轴范围 / 串口自动启动 /
  终端时间戳 / 表格多选与勾选列），NumField 补 step；**四类保留手写并写进注释**
  （字符串 format 下拉只集中 option 文案、colorpicker hex↔int、dialog 居中反语义、
  LED 周期 <100→500）。ContentSection 的「所在页」「当前页/初始选中」改用原语，
  datepicker 格式选项集中为常量；items / nodes / columns / rows 的 Area 序列化
  **保留手写**（各有一套专用 codec，不是通用字段）。
- **G5 机械重复部分至此收口**：6 个 Section 已改（State / Geometry / Edit / Type / Content
  + PanelUi 原语）。剩余 IdentitySection（id 查重改绑）、StyleSection（style 继承语义）、
  ActionSection、BindSection、HintSection、MediaSection —— 这几个按上面的判据
  **本来就该手写**（id 改绑、style 继承、动作引用、绑定、文本域、媒体路径都是真语义），
  不再动。G5 的"新控件加属性不改 React"目标**主动放弃**，理由见上（不造第二张镜像表）。
- **必须保留手写（不可数据化）**：colorpicker 整数↔hex 往返（TypeSection:139-143）、
  serial 六参数+无界面说明（:54-127）、chart 三值且禁标 ranged（kinds.ts:63、TypeSection:253-277）、
  id 查重改绑（IdentitySection:46-78）、style 继承 setField/clearField/setBold/setAlign
  （StyleSection:104-140）、Area 序列化 items/nodes/columns/rows（ContentSection:154-251）、
  EditSection 预设按钮组（:22-64）、LED 闪 period<100→500（TypeSection:325）。

## G4' 选中控件 → 事件/支持库双向高亮（HMI"按了没反应"差评的画布侧解药）

**现状盘点（2026-10-18）**：
- EventPanel 已有**绑定级检查**（跨控件共用回调 collectCallbacks:62-73、控件不支持事件警告
  :77-79/:107-119、click 被动作接管禁用 :158/:187/:202-212）；缺的是"回调在导出 C 骨架/
  impl 里是否存在"——头注释 :5 承诺了但没实现，且 `uniqueCallbackNames` 全由 doc 推出
  （codegen.ts:223/258/261 导出时给所有名字生成空函数），doc 内永远自洽，真正的"绑错"要
  对着**已导出的 impl 文件**解析（现无此工具函数）。
- SupportPanel：搜索 `filterSupportLibs`（supportlib.ts:1694-1700）匹配键 = fn/name(中文)/brief/decl
  四选一；命中高亮 `.sup-cmd.on`（SupportPanel.tsx:85、styles.css:2522-2533）已存在；
  **无 ref/scrollIntoView**（全仓零命中），滚动容器 `.sup-tree`（:132），搜索时折叠失效
  命中必可见（:71）。**无"控件类型→API"关联字段**。SupportPanel 目前只收 onSay（App.tsx:790），
  拿不到 doc/选中控件。
- tab 全是条件渲染（App.tsx:766-791），切走即卸载，SupportPanel 的 kw/selected 状态会丢
  （SupportPanel.tsx:40-43）。
- codegen 可复用：`collectCallbacks(doc)` → `CallbackUse{name,widgetId,widgetType,event}`
  （codegen.ts:34-48，含窗口伪 id `__window`）；回调签名固定 `void name(cg_widget_t*,
  cg_event_t, void*)`（:258）。

**交付（2026-10-18，(b)(c) 落地；(a) 判为不做）**：
- **(a) 事件存在性检查：不做**。两个判据：① bridge 无"按路径读 C 文件"能力
  （`bridge.ts` 只有 `readImage`），要做得加 electron 主进程 IPC，跨层成本高；
  ② EventPanel 头注释那句"C 侧是否存在"其实已用 doc 派生在做（`collectCallbacks`
  :62-73 的跨控件共用检查）。真·impl 存在性检查的投入产出比不成立 ⇒ 入档不做，
  **复议触发**：真实用户报"导出后编译 undefined reference 且画布没提示"。
- **(b)(c) 已交付**（同日）：新增 `model/supportfocus.ts`（`supportKeywordsFor` 控件类型
  →关键词、`callbacksNamed` 反查用到某函数的控件；**零 codegen 依赖**，`FocusHit` 本地
  类型避免 barrel 重名）。SupportPanel 接 `doc`/`focusKeywords`/`onFindWidgets`：选中控件
  →预置搜索词（正向，effect 只在切页签/换选中那一下，不覆盖用户手输）；帮助卡片新增
  "用到它的控件"块（反向，命中可一键"定位"画布控件，复用 App 的 `selectMany` :146）。
  两向都纯 doc 派生，**不新增跨组件状态**。tsc / vite / model barrel 编译全绿。
- **设计判据**：映射表用**关键词**不是函数名——支持库真相是 `cstackgui.h`，函数会增减，
  按"用途词"（表格/树/定时器…）过滤更稳；关键词表放独立小文件，不改 129KB 数据表。

## 长期否决与中间态（2026-10-18 修正版）

- **协同多用户：不是伪需求**（用户纠正：团队型开发是真实场景 —— 上位机项目多人分工、
  教学机房、甲方审阅都是协同形态）。定位为**中间态：架构按协同形态设计，基础设施不建**：
  - G1 的 ops 命令对象化 + apply 纯函数 + id 跨机唯一 = 协同的三个架构前置，本轮就做；
  - 不引入 CRDT 库、不做同步服务、不做多人光标 UI —— 这些是**协同基础设施阶段**的内容，
    启动判据：出现第二个真实团队用户或明确的多人分工需求；
  - .cgui 的文本 JSON 形态是资产：git 三方合并天然可用（穷人版协同），文件形态永不放弃
    （离线优先，协同是文件之上的同步层而非替代）。
- WebGL/WASM/信号库渲染：规模差三个量级，画布几十控件 + 已有"拖动直写 DOM 绕 React"
  的同哲学优化（协同不改变渲染规模判断）；
- 组件模板库：等 G3 布局落地后复议（模板本质是子文档，与布局语义耦合）。
