# envsnap：清单改为 INI 导入 + 动态增删改查

## Spec（要什么 / 为什么 / 边界）

**要什么**：envsnap 的环境变量采集清单不再硬编码在 `k_envs[]`，改为 INI 文件驱动：

- 格式：`[分组名]` 节 + `变量名=备注` 键值（UTF-8，默认 `exe 同目录\envsnap.ini`）。
- 启动自动载入；文件不存在则用内置清单生成一份（真窗口才做，自检不碰盘）。
- 「导入INI」：选任意 ini 重新载入，此后增删改都存回它。
- 添加 / 删除 / 修改：`cg_input_box` 三步输入（分组 / 变量名 / 备注），改完**立即存回**当前 ini。
- 查询：关键字过滤清单（变量名 / 备注 / 分组，ASCII 大小写不敏感），结果进右侧表格。
- 树仍是懒加载；目录那半边行为不动。

**为什么**：用户要求"节点固定死了、无法动态增删改查"；INI 是运行时现成能力
（`cg_file_read/write` + 打开对话框），不引新依赖、不加新控件类型。

**验收标准**（每条都能失败）：
1. 首次真窗口启动：exe 旁生成 `envsnap.ini`（28 个内置变量按组分节），日志可见。
2. 「导入INI」选一份改过的 ini → 树按它重建；取消选择 → 树不变。
3. 添加：三步输入 → 树出现、ini 追加该行；同组同名重复 → 拒绝（消息框）。
4. 删除：选中变量 → 树与文件同步消失；选中分组 → 整组连带删除；空选中 → 只提示。
5. 修改：三步输入预填现值 → 改名 / 挪组 / 改备注生效并落盘；改到重复名 → 拒绝。
6. 查询：关键字命中进表格；无命中给提示行；取消 / 空关键字不动表格。
7. 自检 `--selftest` 全绿且**零磁盘副作用**（不读不写 ini、不弹框）。
8. `build-app.bat envsnap` 零警告；`tools\check-format.bat` 全过。

**能改什么**：`examples/envsnap/` 三件（main.c / envsnap.cgui / README.md）。
**不能改**：`runtime/`、其它示例、构建脚本、表格列结构（仍是 3 列）。

## Plan（步骤，每步带验证）

1. **main.c**：模型层（`s_items` / `s_groups` / `s_ini_path`）+ INI 解析 / 保存 / 种子；
   `load_children` 环境侧改从模型取；fill_table / 快照相应改；
   五个新回调（import/add/del/edit/find）+ 绑定表。验证：编译过、selftest 绿。
2. **envsnap.cgui**：第二排 5 按钮 + 提示 label，grp_mid 下移（y 52→88，h 400→364），
   子控件 h 340→304。验证：check-format 往返探针过。
3. **README**：用法 / 控件表 / 接口 / 坑按实测 selftest 数字更新。验证：数字与实跑输出一致。
4. **验证**：build-app envsnap → selftest → check-format → shot.ps1 截图（ini 生成 + 树内容）。
5. **审查提交**：piv-review-changes（buffer_overflow_check + firmware_code_style 路由）→ 分批提交。

## 风险与异常清单

- 解析防御：BOM / `\r\n` / 注释（`;` `#`）/ 无 `=` 的裸键 / 节外的键 / 重复键（取先、计数提示）。
- 写坏文件防御：组名剥 `[ ]`、变量名剥 `=`；名空拒绝；组满 32 / 变量满 256 有提示。
- `cg_message_box` 只有确定钮 → 删除**无二次确认**（README 写明；文件即事实源，可手改恢复）。
- 自检路径（无窗口）：on_ready 用内置清单、不读不写盘；五个新回调开头 `cg_app_has_window` 拦截。
- s_nodes 扩到 1024（256 变量 + 32 组 + 200 文件 + 分类后留余量）。
- ini_save 借用 s_snap 缓冲拼文本（96KB，256 行 × 最长 ~212B ≈ 54KB，够）。

## 回滚

单功能、两文件为主：`git checkout -- examples/envsnap` 即整体回退。

## 进度

- [x] P1 计划落盘（2026-10-08）
- [x] P2 main.c
- [x] P2 envsnap.cgui
- [x] P3 编译 + selftest（控件 16 / 回调 10，零警告）
- [x] P3 check-format + 截图（首次生成 ini、改 ini 重启后树跟随，两张截图在 build\）
- [x] P4 审查报告（1 项修复：组满孤儿键；报告在 .codebuddy\code-reviews\）
- [x] P5 提交（三个批次：示例 / 文档 / 构建）

## 已完成
INI 清单格式落地：`[分组]` 节 + `变量名=备注`；启动自动载入、缺省按内置清单生成；
导入INI / 添加 / 删除 / 修改 / 查询五个回调，改动即存回当前 ini；自检零磁盘副作用。

## 遗留
- 模态输入框的键鼠探针实测未做（真机手检：加/删/改/查 + 挪组）。
- 运行时 `cg_win_modal.c` 有一个既有的 unused-variable 警告，不属于本任务改动面，未动。
