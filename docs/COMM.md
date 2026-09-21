# 通信层：串口（`cg_serial_*`）

> 一句话：**枚举端口、打开、收字节、发字节**收进运行时，业务代码照样只 `#include "cstackgui.h"` ——
> 不用 include `windows.h`，也不用自己写 `CreateFile` / `DCB` / 读线程。

实现：`runtime/src/kit/cg_serial.c`。用法示例：`examples/serialterm/`（串口助手，第 3 节的线程模型完整版）。
控件侧的配套是 `terminal`（日志视图），字段与坑见 [CGUI-FORMAT.md](CGUI-FORMAT.md) 第 2.9 节。

---

## 1. 为什么单独做一层

`cg_kit` 的规矩是"跟界面无关的那一半 Win32 由运行时兜住"：读写文件、INI、编码、剪贴板都这么来的。
串口同理 —— 裸着写要碰 `CreateFile("\\.\COM3")` + `DCB` + `SetCommTimeouts` + overlapped 读，
每一项都能写错；更要紧的是**这些活不能做在 UI 线程上**，否则等数据就把消息循环堵死了。

所以这一层的形状是刻意定死的：

- 运行时内部起**一个读线程**，把串口字节搬进环形缓冲；业务只管**定周期来取**（第 3 节）。
- 业务侧 API 只有 10 个，全是"拿句柄做一件事"，没有回调、没有注册、没有事件。
- **不做**"收到数据自动回调业务" —— 那等于把线程问题原样塞回给业务，与项目一贯的
  "别开线程碰控件" 矛盾（`chart` 的实时刷新也是同一个立场，见 CGUI-FORMAT 2.10）。

## 2. API 速查

```c
typedef struct cg_serial_t cg_serial_t;   /* 不透明句柄：一个句柄 = 一条线 */

int  cg_serial_list(char *buf, int cap);
cg_serial_t *cg_serial_open(const char *port, int baud);
void         cg_serial_close(cg_serial_t *s);
int          cg_serial_write(cg_serial_t *s, const void *data, int len);
int          cg_serial_read(cg_serial_t *s, void *buf, int cap);
int          cg_serial_available(const cg_serial_t *s);
void         cg_serial_flush_rx(cg_serial_t *s);
long long    cg_serial_rx_bytes(const cg_serial_t *s);
long long    cg_serial_tx_bytes(const cg_serial_t *s);
const char  *cg_serial_last_error(const cg_serial_t *s);
```

| 函数 | 说明 |
| --- | --- |
| `cg_serial_list` | 枚举**当前可用**的串口，一行一个写进 `buf`（`"COM3\nCOM7\n"`），返回写入的字节数。给下拉框用就够：拿到 `COM3` 直接喂 `cg_serial_open`。 |
| `cg_serial_open` | 打开端口，`baud` 是波特率（如 `115200`）。`port` 写 `"COM3"`、或 C 源码里转义后的 `"\\\\.\\COM3"`，两种都认。失败返回 `NULL`。 |
| `cg_serial_close` | 关闭并释放句柄。**幂等、NULL 安全**，可以无条件调（关两次、关 `NULL` 都不出错）。 |
| `cg_serial_write` | **同步写**（内部有超时），返回实际写出的字节数；`< 0` 表示失败。 |
| `cg_serial_read` | **非阻塞**读，返回读到的字节数；**`0` = 此刻没有数据，不是错误**。 |
| `cg_serial_available` | 已到达、还没被取走的字节数。用来判断"是不是快堆满了"。 |
| `cg_serial_flush_rx` | 丢掉还没取走的接收字节（重连、换波特率之后清场用）。 |
| `cg_serial_rx_bytes` / `cg_serial_tx_bytes` | 从打开那一刻起的累计收发字节数（`long long`），状态栏显示用。 |
| `cg_serial_last_error` | 最近一次失败的中文原因；没出错返回 `""`。 |

**为什么只给端口名、不给友好名（"USB-SERIAL CH340 (COM3)"）**：拿设备描述必须走 SetupAPI，
而系统库是在 **exe 链接那一步**才连的 —— 用它就等于给每个应用的链接依赖里塞一个 `-lsetupapi`，
这点"好看"不值得。端口名本身已经够 `cg_serial_open` 用了。实现改走注册表
`HKLM\HARDWARE\DEVICEMAP\SERIALCOMM` 列端口，**零额外链接依赖** —— 和"单 exe、零依赖"这条底线一致。
想要设备描述就自己拿端口名去设备管理器对一下。

## 3. 线程模型（唯一容易出事的地方）

`cg_serial_open()` 内部起一个**读线程**：它阻塞等数据，读到就写进环形缓冲（临界区保护），
读不到就继续等。**它不认识控件，也不该认识** —— Win32 控件不是线程安全的，在读线程里
`cg_term_write()` / `cg_widget_set_text()` 属于未定义行为（轻则花屏，重则随机崩在 GDI 里，
且崩点每次都不同，极难查）。

运行时替你做的只有"字节进缓冲"这一半，另一半**必须由业务在 UI 线程里做**：

```
读线程（运行时内）                  UI 线程（你的代码）
  ReadFile ──> 环形缓冲  ────────>  cg_serial_read()  ──>  cg_term_write()
```

唯一被支持的用法就是这三步，其中"什么时候来取"由 `cg_set_timer()` 决定：

1. `on_ready`（或"打开"按钮的回调）里 `cg_serial_open()` 拿到句柄；
2. 起一个时钟，回调里**循环** `cg_serial_read()` 把缓冲抽干，逐段 `cg_term_write()` 灌进 terminal；
3. 断开时 `cg_serial_close()` 并把句柄置 `NULL`，时钟回调发现句柄没了就自然停。

**注意 `cg_set_timer()` 是一次性的**（见 `runtime/src/core/cg_timer.c`），所以"定周期"要在回调里
**再挂一次**。忘了重挂的现象是"只刷新了一轮就再也不动了"，很难当场看出是漏了一行。

### 3.1 完整可抄的例子

```c
#include "cstackgui.h"
#include <string.h>

static cg_serial_t *g_ser = NULL;        /* 一个句柄 = 一条线，业务自己拿稳 */

/* ---- 时钟回调：这里是 UI 线程，碰控件是安全的 ---- */
static void poll_rx(void *user)
{
    cg_app_t *app = (cg_app_t *)user;
    unsigned char buf[512];
    char *s;
    int n;

    if (!g_ser) return;                  /* 已断开：不再往下排队 */

    /* 非阻塞读：把这一轮攒下的字节抽干；返回 0 = 暂时没有，不是错误 */
    while ((n = cg_serial_read(g_ser, buf, (int)sizeof(buf))) > 0)
        cg_term_write(cg_find(app, "term_log"), CG_TERM_RX, buf, n);

    s = cg_str_format("RX %lld  TX %lld",
                      cg_serial_rx_bytes(g_ser), cg_serial_tx_bytes(g_ser));
    cg_widget_set_text(cg_find(app, "lab_stat"), s);
    cg_free(s);

    cg_set_timer(app, 30, poll_rx, app); /* 一次性定时器：再挂一次才成为"周期" */
}

/* ---- 「打开」按钮 ---- */
static void on_open_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    cg_widget_t *term = cg_find(app, "term_log");

    if (g_ser) { cg_serial_close(g_ser); g_ser = NULL; }  /* 重复点：先关掉旧的 */

    g_ser = cg_serial_open("COM3", 115200);
    if (!g_ser) {                        /* 没设备 / 端口被占，走的就是这条 */
        /* 打开失败时句柄就是 NULL —— last_error 拿到的正是刚刚那次失败的原因 */
        cg_term_write(term, CG_TERM_SYS, cg_serial_last_error(g_ser), -1);
        return;
    }

    cg_term_write(term, CG_TERM_SYS, "COM3 已打开 115200 8N1\r\n", -1);
    cg_set_timer(app, 30, poll_rx, app); /* 连上才开始轮询 */
}

/* ---- 「发送」按钮 ---- */
static void on_send_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    char *line;

    if (!g_ser) return;

    line = cg_str_format("%s\r\n", cg_widget_text(cg_find(app, "txt_send")));
    if (cg_serial_write(g_ser, line, (int)strlen(line)) < 0)
        cg_term_write(cg_find(app, "term_log"), CG_TERM_SYS, cg_serial_last_error(g_ser), -1);
    else
        cg_term_write(cg_find(app, "term_log"), CG_TERM_TX, line, -1);
    cg_free(line);
}

/* ---- 「断开」按钮 / 窗口 close 事件：两处都调它 ---- */
static void close_port(cg_app_t *app)
{
    cg_serial_close(g_ser);              /* 幂等、NULL 安全，可以无条件调 */
    g_ser = NULL;                        /* 置 NULL 之后 poll_rx 自己就停了 */
    cg_term_write(cg_find(app, "term_log"), CG_TERM_SYS, "已断开\r\n", -1);
}
```

两处细节：`cg_term_write()` 的 `len` 传 `-1` = 按字符串取长度（与 terminal 的约定一致）；
行首的 `RX ` / `TX ` / `-- ` 是 terminal **自己加的**，别在内容里再写一遍。
另外一次 `cg_term_write()` 只刷新一次界面，所以循环里逐段灌是没问题的（CGUI-FORMAT 2.9）。

## 4. 接收缓冲：满了丢最老的

内部环形缓冲上限 **64KB**，写满时**丢最老的字节、保住最新的** —— 与 terminal 控件"行数超上限丢最老"
是同一个策略：通信工具里最新的一帧永远比三分钟前的日志值钱。所以：

- `cg_serial_read()` 返回 0 只能说明"这一刻没有可取的"，**不能说明从来没漏过**；
- 想确认是不是一直在漏，看 `cg_serial_available()` 有没有长期顶在高位（一直贴着上限）；
- 不想攒旧数据（刚连上、刚改完波特率），`cg_serial_flush_rx()` 一把清掉。

要"一条都不丢"就得自己尽快取走：把轮询周期压到 30~50ms 对常见波特率（115200 约 11KB/s）足够了。

## 5. v1 的边界（有意不做）

自曝一下，免得你对着 API 猜：

| 边界 | 说明 |
| --- | --- |
| 只支持 **8N1** | `cg_serial_open(port, baud)` 只有两个参数：数据位/停止位/校验位/流控都写死（8 位数据、无校验、1 位停止、无流控）。板子配了 7E1 就一定是乱码，这不是 bug。 |
| **一个句柄 = 一条线** | 没有"多口聚合"。要同时开两个口就调两次 `cg_serial_open`，自己拿两个句柄。 |
| **不自动重连** | 拔掉 USB 转串口之后旧句柄就废了，`cg_serial_read()` 会一直返回 0。要恢复就"关掉旧的 + 重新 open"。 |
| `cg_serial_write()` 是**同步写** | 有超时，返回实际写出的字节数。**没有发送队列** —— 发送节奏很密时排队得自己来（最省事的做法仍然是走时钟，见 `examples/serialterm`）。 |
| 没有串口设备时**只能验证错误路径** | 没有板子的机器上就只有这一条路可走：`cg_serial_open()` 返回 `NULL`、`cg_serial_last_error()` 给出可读的中文原因。**这条路径本身就是排查第一步**，示例工程把它当正常分支处理（不是"测不到就不测"）。 |

## 6. 排查表

先看这张表（现象 → 先查什么，按顺序查）。下面每条都是串口这类活最常卡住的地方：

| 现象 | 先查什么 |
| --- | --- |
| `cg_serial_open()` 返回 NULL | ① **先看 `cg_serial_last_error()`** —— 它是中文的、直接说原因，别从代码猜 ② 端口号对不对：设备管理器里是 COM 几；`cg_serial_list()` 能列出当前可用端口（只给端口名，不带设备描述 —— 为什么见 §2）③ **端口被占**：串口助手 / IDE 的串口监视器 / 上一个没退干净的 exe，都会把端口占死，而现象同样是"打不开" |
| 打开成功，但收不到数据 | ① **时钟有没有真的跑起来**：`cg_set_timer` 是一次性的，回调里忘了再挂一次 = 只刷新第一轮 ② 周期别设 0 空转，也别设太长（30~50ms 够用；后端心跳本来就是 50ms 粒度，设得再密也不会更灵）③ `cg_serial_read` 返回 0 是"暂时没有"，不是错 ④ 先做**自环**：TX/RX 短接，`cg_serial_write` 出去的应该能从 `cg_serial_read` 读回来（一步同时排掉"线没接对"和"API 用错"）⑤ 板子的 TXD 要接你的 RXD，同名对接是收不到的 |
| 收到的是乱码 | ① 波特率对不上（最常见，先查这个）② v1 只有 8N1，板子配了 7E1 / 2 位停止位就一定会乱 ③ 板子用内部 RC 振荡器时波特率本身有误差，高波特率下会飘（换晶振或降速）④ 先把 terminal 切 `format="hex"` 看原始字节，确认是真乱码还是文本模式把控制字符显示成了 `.` |
| 中文（UTF-8）被截断 / 偶尔出现半个字 | ① `cg_serial_read` 的缓冲区边界可能正好切在 UTF-8 多字节序列中间 —— **一次收不完是常态**，不是丢数据 ② 别把每轮读到的字节单独当字符串处理；要拼就在下一轮拼齐再灌 ③ terminal 本身不会搅坏 UTF-8（`0x80` 以上原样保留，见 CGUI-FORMAT 2.9），所以问题通常在业务侧的拼接 |
| 界面卡顿 / 终端不刷新 | ① 最大嫌疑：**在读线程里碰控件** —— 读线程唯一被允许做的事就是"往环形缓冲写" ② 是不是以为有数据会自动回调？模型是**拉**：必须在时钟里主动 `cg_serial_read()`，没有推送 ③ 一次 `cg_term_write()` 刷一次界面，几十段别拆成几十次调用，能合并就合并 |
| 关闭时崩溃 / 卡住 | ① 关之前先 `cg_serial_close()` 再把自己的句柄置 `NULL`（幂等、NULL 安全）；"半关"（关了还在用）最容易出事 ② 还有没有别的地方抱着同一个句柄（别的定时器、别的回调）—— 先让它停，再关 ③ 别在读线程里用 `cg_term_write`，这是①之外最常见的崩因 |

**中文被截断怎么处理**：UTF-8 里一个字符 1~4 字节（`0xxxxxxx` 自成 1 个；`110xxxxx` 起头 2 个；
`1110xxxx` 起头 3 个；`11110xxx` 起头 4 个；后缀字节一律是 `10xxxxxx`）。所以只要看 `buf` 末尾：

```c
int hold = 0;                                  /* 末尾有几个 10xxxxxx 的续字节 */
while (hold < 3 && hold < n && (buf[n - 1 - hold] & 0xC0) == 0x80) hold++;
/* 再看 buf[n-1-hold] 的起头位声明的字节数：凑不齐就把这 hold+1 个字节留下不灌，
   下一轮拼在新缓冲区的最前面。中文 3 字节，实际只会留 1~2 个，开 4 字节小尾巴就够 */
```

## 7. 后面还要做什么（计划中，不是已有能力）

- **网口**（`cg_net_*`）、**WiFi**（`cg_wifi_*`）、**BLE**（`cg_ble_*`）：**计划中，尚未实现**。
- 三者的可行性已在本机工具链上实测过（2026-09-21，探针程序），所以"能不能做"不是问号：
  - **网口**：Winsock2 是 MinGW 自带的（`-lws2_32`），TCP 客户端 / TCP 服务端 / UDP 都能做。
    需求蓝本可直接对齐 NetAssist / Hercules（多客户端、ASCII↔Hex 互转、间隔/循环发送、从文件导入指令）。
  - **WiFi**：MinGW 自带 `wlanapi.h`，链接 `-lwlanapi`；`WlanOpenHandle` / `WlanEnumInterfaces` /
    `WlanGetAvailableNetworkList` / `WlanQueryInterface`(RSSI) / `WlanConnect` / `WlanDisconnect`
    实跑全通 —— "扫描 + 看信号 + 连/断"这一档可做。
  - **BLE**：链接 `-lbthprops -lbluetoothapis`；`BluetoothGATTGetServices` / `GetCharacteristics` /
    `Get`+`Set`CharacteristicValue（注意是 `Get`/`Set`，不是 `Read`/`Write`）/ `RegisterEvent` /
    `BluetoothFindFirstDevice` 可用。**但只限"Windows 已配对"的设备** —— 未配对设备的广播扫描
    只能走 WinRT（要 C++/WinRT + MSVC），本工具链没有。变通：先让 Windows 配对，或走
    "串口-BLE 透传模块"（那就回到本篇这层串口）。
- 本层（串口）落在 `runtime/src/kit/` 里，列端口走注册表 `HKLM\HARDWARE\DEVICEMAP\SERIALCOMM`
  （**不引 SetupAPI，见 §2**），所以串口这块没有给 exe 加任何新的链接依赖。
  **运行时的第一个、也是唯一一个工作线程就在这里** —— 网口/WiFi 若落地，会沿用同一套
  "运行时起读线程、你在时钟里取"的形状，业务侧不会再看到线程。

## 8. 示例工程

`examples/serialterm/` —— 串口助手，演示第 3 节那套线程模型的完整版：

- 端口下拉（`cg_serial_list()`）+ 波特率 + 打开 / 断开；
- 上面一块 terminal 收 RX、下面一块发 TX，中间用 `splitter` 分隔（就是 CGUI-FORMAT 2.9 里那个经典布局）；
- 文本 / HEX 切换、时间戳开关、`cg_term_save()` 存日志；
- **重点看它的三个回调**：`on_ready` 里开串口 + 起时钟、时钟回调里 `cg_serial_read` → `cg_term_write`、
  断开时 `cg_serial_close` + 置 `NULL`。把这三个改动抄进自己的工程，串口这块就不会踩到线程的坑。
