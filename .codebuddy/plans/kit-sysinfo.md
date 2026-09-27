# 实现计划（简版）：cg_kit 系统信息

> 继续"从易语言精易模块学功能"那条线。这一片挖 `系统_`（254 条）。

## 1. 证据：`系统_` 的形状

- **头 34 条是"每个环境变量一个命令"**：`系统_COMPUTERNAME` / `系统_APPDATA` / `系统_TEMP` /
  `系统_WINDIR` / `系统_SYSTEMROOT` / `系统_USERNAME` / `系统_NUMBER_OF_PROCESSORS` /
  `系统_PROCESSOR_ARCHITECTURE` … 外加一条 `系统_读环境变量`。
  → 这是**易语言没有 `getenv`** 的产物。C 里有，所以这一族在 C 侧首先是**一个** `cg_env()`。
  计算机名/用户名/临时目录/系统目录/CPU 个数全都能从它拿到，不需要 34 个函数。
- 其余有价值的：`取MAC地址` / `取本机IP` / `取网格主机名`、`取所有磁盘盘符` / `取磁盘大小信息`、
  `取CPU名称/制造商/时钟频率/占用率`、`取系统版本` / `是否64位操作系统` / `是否为Server版本`、
  `取启动时间` / `取空闲时间`、`取DOS执行结果`、`取屏幕分辨率` / `取系统DPI`、
  `取显卡信息列表` / `取摄像头数量` / `取字体名`。

## 2. 明确**不抄**的三类（写下来免得以后照抄回来）

1. **恶意软件相邻的**：`取宽带用户名密码`、`修改主页`、`设置IE代理地址`、`设置为默认浏览器`、
   `屏蔽任务管理器`、`用户名密码验证`、`遍历右键菜单并执行`、`修改MAC地址`/`删除MAC地址`。
   这工程的立身之本是**给人做正规工具**。抄进来不是"功能更全"，是把一个工具库变成可疑软件。
2. **破坏性的**：`关机` / `关电源` / `强制关机` / `远程关机` / `强制重启` / `强制注销` /
   `修改开机密码` / `切换用户` / `卸载组件` / `清空系统临时目录` / `格式化软盘` /
   `创建还原点`。一个调试工具误点的代价远大于它的用处。
3. **C 里已有等价物或一行能写的**：34 个环境变量命令（→ `cg_env`）、
   `取DOS路径`（→ `cg_env("COMSPEC")`）、`取屏幕分辨率`（界面侧本来就在处理 DPI）。

## 3. 硬约束：**不引入新的链接库**（这条是被实验校准过的）

`runtime\src\kit\cg_kit_sys.c` 里早就在用 `RegOpenKeyExW`（advapi32），而链接表里
**没有** `-ladvapi32` —— 于是我用工程自己的链接参数编了一个探针
（`build\probe_link\crypt_probe.c`，同时调 `CryptAcquireContextW` 与 `GetAdaptersInfo`）：

| 符号 | 结果 |
|---|---|
| `CryptAcquireContextW`（advapi32） | **链接通过** —— 本来就在可解析范围内 |
| `GetAdaptersInfo`（iphlpapi） | `undefined reference` —— 确实没链接 |

⇒ 结论：**"要枚举网卡拿 MAC 地址就得 -liphlpapi"是真的**，那才是会让所有既有用户项目
构建失败的那种改动，所以 `取MAC地址` 不做。
⇒ 同时这条实验**推翻了**我给哈希片写的理由（我当时说 CryptoAPI 也会改链接命令 —— 错），
`cg_kit_check.c` / `cstackgui.h` / `kit-hash.md` 三处的说法都已按事实更正。
⇒ 教训：**"我以为需要新链接库"是可验证的假设，先编译一次再拿它当理由。**

## 4. 契约（7 个函数）

```c
char *cg_env(const char *name);                          /* UTF-8；没有 NULL */
int   cg_env_int(const char *name, int fallback);
int   cg_sys_is64(void);
char *cg_sys_version(void);                              /* "Windows 10 (10.0.19045)" */
int   cg_disk_space(const char *path, unsigned long long *free_b, unsigned long long *total_b);
int   cg_process_running(const char *exe_name);
char *cg_local_ip(void);
```

刻意的决定：

- **`cg_env` 不用 `getenv()`**：CRT 的窄接口返回 ANSI/MBCS，中文用户名/路径
  （`C:\Users\张三`）会变乱码，而本工程内部一律 UTF-8 → 走 `GetEnvironmentVariableW` + `kit_u8`。
  另外区分"没有这个变量"（NULL）与"有但值是空的"（空串）。
- **`cg_env_int` 复用 `cg_base_to_int`**（上一片做的）：整串必须合法、溢出算失败。
  `atoi("abc") == 0` 这种"错得看不出来"的结果比 fallback 更糟。
- **`cg_sys_is64` 用 `GetNativeSystemInfo`**，不用 `IsWow64Process`（后者问的是"我是不是被
  WOW64 套着"）。环境变量 `PROCESSOR_ARCHITECTURE` 给的是进程视角，会误导。
- **`cg_sys_version` 用 ntdll 的 `RtlGetVersion`**（`GetProcAddress` 取）：`GetVersionEx` 会被
  exe 清单里的兼容性声明骗。Win11 也报 10.0，按 build ≥ 22000 分。
- **`cg_local_ip` 落在 `cg_net.c`**：`winsock2.h` 必须在 `windows.h` 之前（那个文件已经处理好），
  与其给 `cg_kit_sys.c` 做头文件手术，不如放在本来就用 winsock 的地方。
  拿法是"挑一条默认路由 + `getsockname`"，**不发包、不查 DNS** ——
  `gethostbyname(gethostname())` 那套在没有解析器的离线工控机上会失败。
- **不做**：`取CPU占用率`（本质是两次采样之间算差值 ⇒ 状态化，真用途是画曲线，属于图表那块）、
  `取DOS执行结果`（管道读满会死锁，要做就得连线程/超时一起设计，不该顺手塞进来）、
  MAC 地址与网卡枚举（iphlpapi）、屏幕分辨率（界面侧已有 DPI 处理）。

## 5. 落地

| # | 文件 | 改什么 |
|---|---|---|
| 1 | `runtime/src/kit/cg_kit_sys.c` | 6 个函数（`+ tlhelp32.h`） |
| 2 | `runtime/src/kit/cg_net.c` | `cg_local_ip()` |
| 3 | `runtime/include/cstackgui.h` | 7 个声明 + 「系统信息」一节（含不抄的三类） |
| 4 | `tests/unit/test_kit_sys.c`（新） | 3 个套件 |
| 5 | `designer/src/supportlib.ts` | 新分组「系统信息」7 条 |
| 6 | `docs/CGUI-FORMAT.md`、`README.md`、`docs/ARCHITECTURE.md` | 速查 / 能力段 / kit 清单 |

## 6. 验证

- 这些函数的结果**随机器而变**，所以断言只钉**性质**：存在的变量读得到、不存在的给 NULL、
  剩余 ≤ 总量、`cg_disk_space` 的两个出口都可单独用、不存在的进程查不到。
- 两条敢写死的值来自**不变量**：`NUMBER_OF_PROCESSORS >= 1`（Windows 的约定）；
  `cg_sys_is64() == 1`（本工程编的是 64 位 exe，64 位进程只可能在 64 位 Windows 上跑）。
- 最有力的一条：**`cg_process_running("cgu-tests.exe")` 必须为真** ——
  它同时证明"枚举进程"和"按名字匹配"两半都对。
- 本机 IP 在离线机器上拿不到属正常，那时"返回 NULL"才是对的，测试按契约不算失败。

## 7. 完成记录（2026-09-27）

- [x] 落地清单 1~6
- [x] 门禁：`build-all` 全示例自检、`run-tests --memcheck`、`check-format`、
      画布 `tsc --noEmit` + `vite build` —— 结果见提交信息
- [x] 三处文档与代码按实验更正（见 §3）
- [ ] 提交：按 `git-redlines` 口径
