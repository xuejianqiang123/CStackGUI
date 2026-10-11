# 实现计划：toolbox 右键菜单管理 —— 对齐参考实现（parity）

> 基准：用户 2026-10-11 给的参考截图（其自有工具箱产品的「右键菜单管理」窗口）。
> 本仓库当前实现（`examples/toolbox` 第 8 页）只有：单张扁平表格（区域/标题/命令/状态）+ 扫描 / 禁用 / 恢复 / 备份并删除 / 还原本次改动，且**只扫 HKCU**。
> 用户判词：「功能还是没有齐全」「不要偷工减料」→ 本文即缺口清单与切片顺序，逐片交付、逐片提交，不许再把某片悄悄省掉。

## 1. 参考截图里能看清的全部要素（验收逐条对照）

| # | 要素 | 参考实现的样子 | 当前实现 | 状态 |
|---|---|---|---|---|
| A | **位置维度** | 5 个页签：文件 17 / 文件夹 28 / 文件夹空白处 10 / 桌面 6 / 磁盘 3（各自带条数） | 一张表用「区域」列混着放 | 缺 |
| B | **搜索** | 顶部「输入名字过滤…」框 | 无 | 缺 |
| C | **过滤** | 全部 / 未禁用 / 程序装的 / 已禁用 / 扩展项 五个按钮 | 无 | 缺 |
| D | **分组** | 工具箱自己装的（4）/ 程序装的（5）/ DLL 扩展项（8），每组一个「收起」按钮 | 无分组 | 缺 |
| E | **行内容** | 复选框 + 图标 + 名称（+「（工具箱）/（扩展项）/（扩展项，系统自带）」标签）+ 第二行命令或说明 + 右侧状态 | 复选框 ✗、图标 ✗；状态只在状态列 | 部分 |
| F | **状态口径** | 在用·用户区 / 已禁用·用户区 / 在用·系统区（要管理员）/ 扩展项·用户区+系统区 / 扩展项·系统区（要管理员） | 在用 / 已禁用 / 无命令 | 部分 |
| G | **系统区** | 扫 HKLM（含 `SOFTWARE\Classes` 下的 shell 键与 shellex），并标注「要管理员」 | 只扫 HKCU | 缺（**要有 runtime 支持**） |
| H | **DLL 扩展项** | 扫 `shellex\ContextMenuHandlers`，把 CLSID 解析成名字（Quark / 7-Zip / TortoiseGit…）；标注「这一版只显示、不给动」 | 不扫 | 缺 |
| I | **批量操作** | 底部：禁用选中的 N 项 / 恢复选中的 N 项 / 删除选中的 N 项 / 取得所有权 | 一次一行 | 缺 |
| J | **勾选计数/统计** | 已勾选 0 项 · 共 17 条（在用 8 / 已禁用 1 / 扩展项 8；其中 12 条在系统区，改它要管理员） | 扫描完成：共 N 条 · 在用 a · 已隐藏 b · 无命令 c | 部分 |
| K | **动作记录** | 一个「动作记录」按钮 | 有日志页但按钮缺 | 缺 |
| L | **取得所有权** | 对系统区键夺所有权（改 ACL） | 无 | 缺 |

## 2. 结论：runtime 必须先补的东西

当前 `cg_reg_*` **只认 HKCU**（`cg_reg_read/write/subkeys/del/del_key`），G/H 两个要素没有 runtime 支持就只能靠 `reg.exe` 拼命令 —— 那是偷工减料。故先补：

```c
/* root 取值："HKCU" / "HKLM" / "HKCR"（大小写不敏感）；NULL / "" = HKCU（与老函数同义） */
char *cg_reg_read_ex (const char *root, const char *subkey, const char *name, const char *def);
int   cg_reg_write_ex(const char *root, const char *subkey, const char *name, const char *value);
int   cg_reg_subkeys_ex(const char *root, const char *subkey, char *out, int cap);
int   cg_reg_del_ex   (const char *root, const char *subkey, const char *name);
int   cg_reg_del_key_ex(const char *root, const char *subkey);
int   cg_reg_key_exists(const char *root, const char *subkey);
int   cg_reg_is_admin(void);   /* 进程是否已提权（系统区能不能改） */
int   cg_reg_acl_take(const char *root, const char *subkey);  /* 夺所有权：SeTakeOwnership + SetNamedSecurityInfo */
```

老 5 个函数变成 `root = "HKCU"` 的薄壳（行为一字不变，现有 kit 用例与示例不受影响）。

## 3. 切片（每片自带门禁 + 提交）

| 片 | 内容 | 判据 |
|---|---|---|
| **P1** | runtime：`*_ex` 家族 + `cg_reg_is_admin` + kit 用例（HKLM 读、HKCR 读、root 非法、空 subkey）+ 支持库表 + `docs/CGUI-FORMAT.md` | `run-tests --full` 绿；新增 kit 用例真读 HKLM/HKCR；支持库表门禁绿 |
| **P2** | 页面重排：5 个位置页签（带条数）+ 搜索框 + 5 个过滤按钮；扫描改成「按位置分别扫 HKCU + HKLM」 | 真机：切页签条数变化、搜索能过滤、过滤按钮真的筛 |
| **P3** | 分组（工具箱自己装的 / 程序装的 / DLL 扩展项）+ 每组「收起」；行的复选框 + 图标 + 名称标签 + 第二行命令 + 右侧状态（含「要管理员」） | 真机截图逐项对照 |
| **P4** | 批量动作：禁用/恢复/删除「选中的 N 项」（含系统区判定：非管理员时明确拒绝并说清）+ 勾选计数与统计行 | 真机：勾 2 项批量禁用 → 注册表读回；非管理员点系统区按钮 → 明确提示 |
| **P5** | DLL 扩展项扫描（`*\shellex\ContextMenuHandlers` + `Directory\shellex` + `HKCR\CLSID\{guid}` 取名）+ 只显示不给动 | 真机：能列出 7-Zip/TortoiseGit 这类条目与名字 |
| **P6** | 「动作记录」按钮（跳到日志）+「取得所有权」（`cg_reg_acl_take`，含提权判定） | 真机：取得所有权后该项可改（或给出需要管理员的明确提示） |

## 4. 口径（不许再省的地方）

- **只显示不给动**的项（DLL 扩展项）必须在行内写明，别让用户点了没反应。
- 系统区（HKLM）读得到、改不了时要**说清是权限问题**，不能静默失败。
- 每一片都要真机截图；不许用「编译过了」代替「跑起来对了」。
- 参考截图里的每个可见元素（A~L）都必须落到某一片；若最终决定不做某条，必须在本文标记并给出理由。
