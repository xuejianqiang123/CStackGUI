# 通信层：串口（`cg_serial_*`）与网口（`cg_net_*`：TCP + UDP）

> 一句话：**建立链路、收字节、发字节**收进运行时，业务代码照样只 `#include "cstackgui.h"` ——
> 不用 include `windows.h`，也不用自己写 `CreateFile` / `DCB` / `WSAStartup` / 读线程。

实现：`runtime/src/kit/cg_serial.c`（串口）、`runtime/src/kit/cg_net.c`（网口：TCP 与 UDP 在同一个文件里）。
用法示例：`examples/serialterm/`（串口助手）与 `examples/netterm/`（网口助手）—— 两个示例共用第 3 节的线程模型。
控件侧的配套是 `terminal`（日志视图），字段与坑见 [CGUI-FORMAT.md](CGUI-FORMAT.md) 第 2.9 节。

**串口与 TCP 网的形状是同一个**，因为面对的是同一件麻烦事（等 I/O 的每一毫秒都是消息循环被堵住的毫秒）：

```
运行时（本层，串口 / TCP）                       业务（UI 线程）
  读线程 ──> 环形缓冲（64KB，满了丢最老） ──> cg_set_timer 回调里 read() ──> cg_term_write()
```

**UDP 是这条线上刻意的例外**：没有读线程、也没有接收缓冲 —— 业务在时钟里 `cg_net_recvfrom()`
一次取一个完整数据报（为什么必须这样，§10.1 讲透）。

所以读本篇的顺序是：**先看串口（§1~§6，那套形状讲得最细）→ 再看网口 TCP（§7~§9，只讲它自己特有的部分）
→ 最后看 UDP（§10~§11，它不套那套形状）**。线程模型（§3）、接收缓冲策略（§4）这两节是
**串口与 TCP** 共用的，网口那几节只做引用、不再重讲。

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

**网口（§7）面对的是同一件事** —— 等 socket 数据同样会堵死消息循环，所以它是照这个形状复制的：
读线程 + 环形缓冲 + "业务在时钟里取"。串口与 TCP 唯一的区别是**"怎么建立链路"**：串口只有一件事（打开端口），
TCP 有两件（客户端 `connect`、服务端 `listen` + `accept`）。

**UDP（§10）不走这条路，它连读线程都不开**：数据报的**报文边界**与**来源地址**必须原样交到业务手里，
而"读线程 + 环形缓冲"是给字节流设计的，这两样一个都保不住（理由见 §10.1）。

## 2. 串口：API 速查（`cg_serial_*`）

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

## 3. 线程模型（串口与 TCP 共用，唯一容易出事的地方）

`cg_serial_open()` 内部起一个**读线程**：它阻塞等数据，读到就写进环形缓冲（临界区保护），
读不到就继续等。**它不认识控件，也不该认识** —— Win32 控件不是线程安全的，在读线程里
`cg_term_write()` / `cg_widget_set_text()` 属于未定义行为（轻则花屏，重则随机崩在 GDI 里，
且崩点每次都不同，极难查）。

运行时替你做的只有"字节进缓冲"这一半，另一半**必须由业务在 UI 线程里做**：

```
读线程（运行时内）                  UI 线程（你的代码）
  ReadFile / recv ──> 环形缓冲  ──>  cg_serial_read() / cg_net_read()  ──>  cg_term_write()
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

## 4. 接收缓冲：满了丢最老的（串口与 TCP 同策略）

内部环形缓冲上限 **64KB**，写满时**丢最老的字节、保住最新的** —— 与 terminal 控件"行数超上限丢最老"
是同一个策略：通信工具里最新的一帧永远比三分钟前的日志值钱。所以：

- `cg_serial_read()` 返回 0 只能说明"这一刻没有可取的"，**不能说明从来没漏过**；
- 想确认是不是一直在漏，看 `cg_serial_available()` 有没有长期顶在高位（一直贴着上限）；
- 不想攒旧数据（刚连上、刚改完波特率），`cg_serial_flush_rx()` 一把清掉。

要"一条都不丢"就得自己尽快取走：把轮询周期压到 30~50ms 对常见波特率（115200 约 11KB/s）足够了。

网口 TCP（§7）同一套：**每条连接一份 64KB 环形缓冲 + 一个读线程**，所以服务端接 16 个客户端就是
16 个缓冲、16 个读线程（约 1MB 内存）—— "多客户端"要不要这么多、句柄数组开多大，是业务自己掂量的。

**UDP（§10）没有这一节**：没有环形缓冲，也就没有"满了丢最老"这回事 —— 内核接收缓冲里的数据报
要业务自己及时取，取晚了就真的没了（UDP 本来就不保证送达）。

## 5. 串口 v1 的边界（有意不做）

自曝一下，免得你对着 API 猜：

| 边界 | 说明 |
| --- | --- |
| 帧格式**支持全参数**（2026-09-27 更正）| 数据位 / 停止位 / 校验都能配：`.cgui` 给 serial 写 `bits` / `stop` / `parity`（画布属性面板有这三个下拉）、运行期用 `cg_widget_serial_set_format()`、或直接 `cg_serial_open_ex(port, baud, bits, stop, parity)`。**7E1 这类老设备选好就能通**。原先这一格写着"只支持 8N1，板子配了 7E1 就一定是乱码" —— **那是错的**：底层一直走的就是 `open_ex`，示例里当时只是没把三个下拉摆出来。 |
| **一个句柄 = 一条线** | 没有"多口聚合"。要同时开两个口就调两次 `cg_serial_open`，自己拿两个句柄。 |
| 关一次就完了，**指针当场失效** | `cg_serial_close(s)` 会释放句柄结构：**同一个句柄关两次 = use-after-free**（2026-09-27 真机实测：进程以 `0xC0000374` 堆损坏退出，崩点可能离得老远）。写完立刻 `s = NULL;`。**组件形态没这个坑** —— `cg_widget_serial_close()` 内部置空，重复调真的安全。 |
| **不自动重连** | 拔掉 USB 转串口之后旧句柄就废了，`cg_serial_read()` 会一直返回 0。要恢复就"关掉旧的 + 重新 open"。 |
| **没有流控** | RTS/CTS（硬件）与 XON/XOFF（软件）都没接：`DCB` 里那两位始终关着。设备靠流控才稳的话，现在得自己在业务侧做（或给 `cg_serial` 加上）。 |
| `cg_serial_write()` 是**同步写** | 有超时，返回实际写出的字节数。**没有发送队列** —— 发送节奏很密时排队得自己来（最省事的做法仍然是走时钟，见 `examples/serialterm`）。 |
| 没有串口设备时**只能验证错误路径** | 没有板子的机器上就只有这一条路可走：`cg_serial_open()` 返回 `NULL`、`cg_serial_last_error()` 给出可读的中文原因。**这条路径本身就是排查第一步**，示例工程把它当正常分支处理（不是"测不到就不测"）。 |

## 6. 串口排查表

先看这张表（现象 → 先查什么，按顺序查）。下面每条都是串口这类活最常卡住的地方：

| 现象 | 先查什么 |
| --- | --- |
| `cg_serial_open()` 返回 NULL | ① **先看 `cg_serial_last_error()`** —— 它是中文的、直接说原因，别从代码猜 ② 端口号对不对：设备管理器里是 COM 几；`cg_serial_list()` 能列出当前可用端口（只给端口名，不带设备描述 —— 为什么见 §2）③ **端口被占**：串口助手 / IDE 的串口监视器 / 上一个没退干净的 exe，都会把端口占死，而现象同样是"打不开" |
| 打开成功，但收不到数据 | ① **时钟有没有真的跑起来**：`cg_set_timer` 是一次性的，回调里忘了再挂一次 = 只刷新第一轮 ② 周期别设 0 空转，也别设太长（30~50ms 够用；后端心跳本来就是 50ms 粒度，设得再密也不会更灵）③ `cg_serial_read` 返回 0 是"暂时没有"，不是错 ④ 先做**自环**：TX/RX 短接，`cg_serial_write` 出去的应该能从 `cg_serial_read` 读回来（一步同时排掉"线没接对"和"API 用错"）⑤ 板子的 TXD 要接你的 RXD，同名对接是收不到的 |
| 收到的是乱码 | ① 波特率对不上（最常见，先查这个）② **帧格式选错了**：板子配 7E1 / 2 位停止位，这边就得选成一样（`.cgui` 的 `bits`/`stop`/`parity` 或 `cg_widget_serial_set_format()`，见 §5；这几个参数**是支持的**，别按老说法以为只能 8N1）③ 板子用内部 RC 振荡器时波特率本身有误差，高波特率下会飘（换晶振或降速）④ 先把 terminal 切 `format="hex"` 看原始字节，确认是真乱码还是文本模式把控制字符显示成了 `.` |
| 中文（UTF-8）被截断 / 偶尔出现半个字 | ① `cg_serial_read` 的缓冲区边界可能正好切在 UTF-8 多字节序列中间 —— **一次收不完是常态**，不是丢数据 ② 别把每轮读到的字节单独当字符串处理；要拼就在下一轮拼齐再灌 ③ terminal 本身不会搅坏 UTF-8（`0x80` 以上原样保留，见 CGUI-FORMAT 2.9），所以问题通常在业务侧的拼接 |
| 界面卡顿 / 终端不刷新 | ① 最大嫌疑：**在读线程里碰控件** —— 读线程唯一被允许做的事就是"往环形缓冲写" ② 是不是以为有数据会自动回调？模型是**拉**：必须在时钟里主动 `cg_serial_read()`，没有推送 ③ 一次 `cg_term_write()` 刷一次界面，几十段别拆成几十次调用，能合并就合并 |
| 关闭时崩溃 / 卡住 | ① **同一个句柄关两次**（最隐蔽的一条）：`cg_serial_close()` 把句柄释放掉，第二次就是 use-after-free —— 实测以 `0xC0000374`（堆损坏）退出，**崩点可能离关的地方很远**，很难往这上面想。关完立刻 `s = NULL;`；组件形态（`cg_widget_serial_close`）内部置空，没这个坑 ② 还有别的地方抱着同一个句柄（别的定时器 / 别的回调）—— 先让它停，再关 ③ 别在读线程里调 `cg_term_write` ④ "半关"（关了还在用）与①是同一类问题 |

**中文被截断怎么处理**：UTF-8 里一个字符 1~4 字节（`0xxxxxxx` 自成 1 个；`110xxxxx` 起头 2 个；
`1110xxxx` 起头 3 个；`11110xxx` 起头 4 个；后缀字节一律是 `10xxxxxx`）。所以只要看 `buf` 末尾：

```c
int hold = 0;                                  /* 末尾有几个 10xxxxxx 的续字节 */
while (hold < 3 && hold < n && (buf[n - 1 - hold] & 0xC0) == 0x80) hold++;
/* 再看 buf[n-1-hold] 的起头位声明的字节数：凑不齐就把这 hold+1 个字节留下不灌，
   下一轮拼在新缓冲区的最前面。中文 3 字节，实际只会留 1~2 个，开 4 字节小尾巴就够 */
```

## 7. 网口：API 速查（`cg_net_*`，TCP）

**先读 §3**：线程模型、`cg_set_timer` 是一次性的、`read` 返回 0 不是错 —— 这些一个字都不用改，
把下面的 `cg_net_*` 当成 `cg_serial_*` 的同胞兄弟读就行。网口自己特有的只有"怎么建立链路"和"掉线怎么判"。

```c
typedef struct cg_net_t cg_net_t;   /* 不透明句柄：一个句柄 = 一条连接（或一个监听） */

cg_net_t   *cg_net_connect(const char *host, int port, int timeout_ms);  /* 客户端 */
cg_net_t   *cg_net_listen(int port, int backlog);                        /* 服务端：监听 */
cg_net_t   *cg_net_accept(cg_net_t *srv, int timeout_ms);                /* 服务端：收一条连接 */
int         cg_net_write(cg_net_t *n, const void *data, int len);
int         cg_net_read(cg_net_t *n, void *buf, int cap);
int         cg_net_available(const cg_net_t *n);
int         cg_net_connected(const cg_net_t *n);
long long   cg_net_rx_bytes(const cg_net_t *n);
long long   cg_net_tx_bytes(const cg_net_t *n);
const char *cg_net_peer(const cg_net_t *n);
const char *cg_net_last_error(const cg_net_t *n);
void        cg_net_close(cg_net_t *n);
```

| 函数 | 说明 |
| --- | --- |
| `cg_net_connect` | 连到 `host:port`。`host` 传 `NULL` / 空串 = `127.0.0.1`（本机自测最常用）；`timeout_ms <= 0` 按 **3000** 算。失败返回 `NULL`。实现是**非阻塞 connect + select** 带超时，所以坏地址不会把界面冻十几秒 —— 但它仍是同步调用，会占住调用线程最多 `timeout_ms`。 |
| `cg_net_listen` | 监听端口，`backlog <= 0` 按 **4**。绑不上（端口被占）返回 `NULL`，原因见 `cg_net_last_error(NULL)`。绑 `INADDR_ANY`，本机与局域网都能连进来。 |
| `cg_net_accept` | 收一条已到达的连接，**返回的是新句柄**（每个连接一个句柄，用完 `cg_net_close`）。`timeout_ms <= 0` = **不等、立刻返回**；没有连接时返回 `NULL` —— **那是正常情况，不是错误**（"服务端放时钟里轮询"的用法前提就在这里）。 |
| `cg_net_write` | **同步写**（内部 1 秒超时），返回实际写出的字节数；`< 0` 失败。**没有发送队列**，与串口同一取舍；`len` 必须 `> 0`（不像 `cg_term_write` 认 `-1`）。 |
| `cg_net_read` | **非阻塞**读，返回读到的字节数；**`0` = 此刻没有数据，不是错误**。对端已关闭时也能把缓冲里剩下的取完。 |
| `cg_net_available` | 这条连接已到达、还没被取走的字节数。 |
| `cg_net_connected` | 对端还在吗：**对端关闭后变 0**，这是判断"掉线"的**唯一信号**。监听句柄恒为 0。 |
| `cg_net_rx_bytes` / `cg_net_tx_bytes` | 这条连接从建立起累计收 / 发字节数（`long long`），状态栏用。 |
| `cg_net_peer` | 对端 `"ip:port"`；监听句柄返回 `"（监听中）"`。 |
| `cg_net_last_error` | 最后一次失败的中文原因；**句柄传 `NULL` 时给的是"最近一次连接/监听失败"的原因**（那时还没有句柄可问）—— 与串口同一个约定。 |
| `cg_net_close` | 关连接或关监听，**幂等、NULL 安全**。内部会**等读线程退出**（最坏一个收超时周期，约 50ms），所以它不是"零耗时"的。 |

**监听句柄上做读写不崩，只是没意义**：`write` 返回 `-1`、`read` 返回 `0`、`connected` 恒 `0`；
对一条已经连上的句柄再 `accept` 也是返回 `NULL`（它没在监听）。**UDP 句柄上是另一套同样明确的拒绝**
（`write` 是 `-1`，`read` / `available` / `connected` 都是 `0`），见 §10.3 —— 别把这段的代码抄到 UDP 上。
`WSAStartup` 在第一次建链路时自动做
（整进程一次），**业务不需要自己碰 Winsock 的初始化和清理** —— 这是"只 include `cstackgui.h`"的一部分。

## 8. 网口 TCP：与串口一样的地方、不一样的地方

**一样的地方（直接复用，别重造）**：线程模型（§3）、"时钟里取字节"那三步、接收缓冲满了丢最老（§4）、
`read` 返回 0 不是错、失败先看 `last_error()`、`close` 幂等且 NULL 安全、
`cg_set_timer` 是一次性的（回调末尾必须再挂一次才成为"周期"）。

**客户端：把 §3.1 那个例子照抄，只改建立/断开链路的两行** ——

```c
g_net = cg_net_connect("192.168.1.10", 8080, 3000);   /* 原来是 cg_serial_open("COM3", 115200) */
...
cg_net_close(g_net);                                  /* 原来是 cg_serial_close */
g_net = NULL;                                         /* 置 NULL 之后时钟回调自己就停了 */
```

再补一句掉线判断就闭环了（对端一走，`cg_net_connected()` 就变 0）：

```c
if (!cg_net_connected(g_net)) {                       /* 对端断开：唯一的信号 */
    cg_term_write(term, CG_TERM_SYS, "对端已断开\r\n", -1);
    cg_net_close(g_net);                              /* 自己回收，别留着句柄 */
    g_net = NULL;
    return;                                           /* 不再排下一拍 */
}
```

**服务端：多两件事** —— 监听，以及**自己管一组连接句柄**（本层没有"客户端列表"这个概念）：

```c
#define MAX_CLI  16
static cg_net_t *g_srv = NULL;        /* 监听句柄：不能读写，只用来 accept */
static cg_net_t *g_cli[MAX_CLI];      /* 一个句柄 = 一条连接，槽位由业务自己管 */

static void net_tick(void *user)
{
    cg_app_t     *app  = (cg_app_t *)user;
    cg_widget_t  *term = cg_find(app, "term_log");
    unsigned char buf[512];
    int           i, n;

    if (g_srv) {
        cg_net_t *c = cg_net_accept(g_srv, 0);        /* 0 = 不等、立刻返回 */
        if (c) {                                      /* NULL 是"还没人来"，不是错误 */
            for (i = 0; i < MAX_CLI; i++) if (!g_cli[i]) break;
            if (i < MAX_CLI) {
                g_cli[i] = c;
                cg_term_write(term, CG_TERM_SYS, "新客户端接入\r\n", -1);
                cg_log(CG_LOG_INFO, "来自 %s", cg_net_peer(c));   /* 想看是谁连的 */
            } else {
                cg_net_close(c);                      /* 满了就拒掉，别泄漏句柄 */
                cg_term_write(term, CG_TERM_SYS, "连接数已满，拒掉\r\n", -1);
            }
        }
    }

    for (i = 0; i < MAX_CLI; i++) {
        cg_net_t *c = g_cli[i];

        if (!c) continue;
        while ((n = cg_net_read(c, buf, (int)sizeof(buf))) > 0)
            cg_term_write(term, CG_TERM_RX, buf, n);
        if (!cg_net_connected(c)) {                   /* 客户端走了：服务端负责回收 */
            cg_net_close(c);
            g_cli[i] = NULL;
            cg_term_write(term, CG_TERM_SYS, "客户端断开\r\n", -1);
        }
    }

    cg_set_timer(app, 30, net_tick, app);             /* 再挂一次才是"周期" */
}
```

两个新手最容易栽的点：**`accept` 要放在时钟里**（`timeout_ms` 传 0 的那次调用永远不让你等），
以及**每条连接是独立句柄** —— 读错句柄（读了监听句柄、或读了别人的连接）不会崩，只是永远收不到数据。

**TCP 是字节流，没有"消息边界"**：一次 `cg_net_read()` 拿到的可能是半个报文，也可能是两条半；
它和对方调用了几次 `cg_net_write()` **没有任何对应关系**。按行 / 按长度切分是业务的事 ——
这一点和串口完全一样，§6 里那段 UTF-8 尾巴处理可以直接抄。

| 网口 v1 的边界（有意不做） | 说明 |
| --- | --- |
| 只有 **TCP** | UDP 是**另一套句柄与另一套 API**（`cg_net_udp_open` / `cg_net_sendto` / `cg_net_recvfrom`，见 §10），不是给 `connect` 多加两个函数；**组播**没有（广播有）。 |
| **不做 TLS** | 明文 TCP，要加密得自己在业务侧加一层。 |
| **不自动重连** | 对端断开后旧句柄就废了（`connected` 变 0）。要恢复就"`close` 掉旧的 + 重新 `connect`"。 |
| `cg_net_write()` 是**同步写** | 带 1 秒超时，**没有发送队列**（与串口同一取舍）；发送节奏很密时自己排队，最省事的做法仍然是走时钟。 |
| 接收缓冲 **64KB** 满了丢最老 | 与串口、terminal 同一策略，见 §4。 |
| **一个句柄 = 一条连接** | 多客户端要自己拿句柄数组（上面那段就是模板）；本层不引入"客户端列表"。 |
| `host` 只解析 **IPv4** | 写域名（A 记录）能解析；写 IPv6 字面量会直接解析失败（内部限定 `AF_INET`）。 |

**链接要 `-lws2_32`，但用户不用管**：winsock2 是系统自带的库，四个构建脚本
（根 `build-app.bat`、`tools\run-tests.bat`、`tools\check-kit.bat`、`packaging\build-app.bat`）**已经加好了** ——
自己写工程、或画布导出骨架后直接 `build-app.bat` 都能编过，不需要手工往链接行里加东西。
`ws2_32.dll` 是系统核心 DLL，所以"单 exe、零依赖"这条底线没破。

## 9. 网口 TCP 排查表

**所有网口问题的第一步都是本机回环**：让客户端连自己机器上的 `127.0.0.1:<你监听的端口>`，
一步就把"网络 / 防火墙 / 网线 / 对端程序"全部排除掉，剩下的只可能是代码。
这也是网口比串口好验的地方：**回环 100% 能自验**（串口要有板子），
`tools\run-tests.bat` 里的 `tests\unit\test_net.c` 就是这么干的 —— 同一个进程里
"监听 → 连接 → 接受 → 双向收发 → 关一端看另一端掉线"全跑通，服务端读到的对端是真实地址
（形如 `127.0.0.1:41635`），不是模拟出来的。

| 现象 | 先查什么 |
| --- | --- |
| `cg_net_connect()` 返回 NULL | ① **先看 `cg_net_last_error(NULL)`**：中文、直接说原因（拒绝连接 / 超时 / 网络不可达 / 主机名解析不了），别从代码猜 ② 端口和 IP 是不是服务端在听的那个（服务端 `cg_net_listen()` 有没有成功）③ **先本机回环**：连 `127.0.0.1` 都不通，就别去查网线和防火墙了，是服务端没在听 |
| 连不上，原因是"对方拒绝连接"（`WSAECONNREFUSED`） | ① 这个端口上**没人监听** —— 服务端没起 / 端口写错 / 服务端自己也 `listen` 失败了（那它的原因在 `last_error(NULL)` 里）② 服务端绑的是 `INADDR_ANY`，所以**同一台机器**上连 `127.0.0.1` 一定能通：通不了就是它确实没在听 |
| 连不上，原因是"连接超时"（`WSAETIMEDOUT`） | ① 对方 IP 对不对、和你是不是同一网段（跨网段要先有路由）② **Windows 防火墙**：程序第一次监听 / 外连会弹"是否允许"，点了拒绝就一直超时，而且不会再弹第二次（去入站/出站规则里看）③ 跨网、公网要把 `timeout_ms` 给大（默认 3000 可能不够）④ 别误判成"界面卡住"：`connect` 是同步的，它会占住调用线程最多 `timeout_ms`（内部非阻塞 + select，所以不会像裸阻塞那样冻十几秒）|
| `cg_net_listen()` 返回 NULL（端口被占 / 监听失败） | ① **先看 `cg_net_last_error(NULL)`**，最常见两种：`端口已被占用`（上一次的 exe 没退干净、或别的程序占着）、`端口被拒绝`（1024 以下要管理员）② 换个端口试（测试代码就是三个候选端口逐个试）③ 端口范围 1~65535：`listen(0)` / `listen(70000)` 直接失败，**不是**"自动挑一个空闲端口" ④ 监听句柄设了 `SO_REUSEADDR`，重启后立刻重绑通常没问题，但真有别的进程在抢同一端口时还是会失败 |
| 连上了，但收不到数据 | ① **时钟有没有真的跑起来**（`cg_set_timer` 一次性，回调末尾忘了再挂 = 只跑一轮，同 §3 / §6）② **服务端最容易拿错句柄**：要读的是 `cg_net_accept()` 返回的那条连接，不是监听句柄；读错不崩，只是永远 0 ③ `read` 返回 0 是"这一拍没有"，用 `cg_net_available()` / `cg_net_rx_bytes()` 判断字节**到底有没有到**（到了却读不出来 = 句柄拿错；根本没到 = 对端或网络侧）④ 先本机回环：回环能收，就往"防火墙 / 网段 / 对端程序"上查 |
| 服务端收不到连接 | ① **`accept` 必须在时钟里轮询**（`timeout_ms` 传 0 = 不等、立刻返回，返回 NULL 是正常的，别拿它当失败去弹框）② 时钟挂上了吗、有没有被自己 `cg_kill_timer` 掉 ③ 监听句柄是不是被提前 `close` 了 —— **别用 `connected()` 判断监听是否还活着**（监听句柄恒为 0）④ 客户端 `connect` 成功 ≠ 你 `accept` 过了：连接会先进内核等待队列，`backlog` 默认 4，攒满之前不会丢 ⑤ 防火墙拦的是**入站**方向：本机回环通、别的机器连不进来，先看这条 |
| 中文（UTF-8）被截断 / 偶尔半个字 | 与串口同一个原因：`cg_net_read()` 的缓冲区边界可能正好切在 UTF-8 序列中间 —— **一次收不完是常态**，拼接方法直接抄 §6 的"中文被截断怎么处理"。再加一条网络特有的：TCP 是字节流，**一次 `read` 与对方一次 `write` 没有边界对应**，别把一轮读到的字节当"一条完整报文"用 |
| 界面卡顿 / 终端不刷新 | ① 最大嫌疑：**在读线程里碰控件**（读线程唯一被允许做的事就是往环形缓冲写，见 §3）② 是不是以为有数据会自动回调 —— 模型是**拉**：必须在时钟里主动 `cg_net_read()` ③ 一次 `cg_term_write()` 刷一次界面，循环里逐段灌没问题（CGUI-FORMAT 2.9）④ 别把"连接"做成一个大 timeout 的同步等待去卡住消息循环 |
| 关闭时崩溃 / 卡住 | ① 关之前先让时钟停：把句柄置 `NULL`，回调里发现没了就 `return`，别让下一拍再用它 ② 同一条连接别被两处同时拿着（另一个定时器、另一个回调）③ `cg_net_close()` 会**等读线程退出**（最坏约 50ms），"关窗口那一瞬间"别反复开关多条连接 ④ 服务端的句柄数组里**每条已连句柄都要 `close`**（漏一个就是句柄 + 读线程泄漏）；它幂等、NULL 安全，放心写成"遍历数组全关" |

## 10. 网口：UDP（数据报）

**先把最容易踩的错摆出来**：UDP 不是"TCP 换个参数"。入口是 `cg_net_udp_open()`，收发是
`cg_net_sendto()` / `cg_net_recvfrom()` —— 它与 TCP 共用 `cg_net_t` 这个类型和 `close` / `peer` /
`last_error` / 累计字节数，但**内部没有读线程、也没有接收缓冲**。所以 §3 那套"读线程 + 环形缓冲 + 时钟里取"
在这里只剩最后半句。

**API 速查（`cg_net_udp_open` / `cg_net_sendto` / `cg_net_recvfrom`）**

```c
cg_net_t *cg_net_udp_open(int local_port, int broadcast);   /* local_port 传 0 = 让系统分配 */
int       cg_net_sendto(cg_net_t *n, const char *host, int port, const void *data, int len);
int       cg_net_recvfrom(cg_net_t *n, char *ip_out, int ip_cap, int *port_out, void *buf, int cap);
/* 与 TCP 共用：cg_net_close / cg_net_peer / cg_net_rx_bytes / cg_net_tx_bytes / cg_net_last_error */
```

| 函数 | 说明 |
| --- | --- |
| `cg_net_udp_open` | 开一个本地 UDP 端口，绑 `INADDR_ANY`（本机与局域网都收得到）。**`local_port` 传 `0` = 让系统分配**，实际拿到哪个端口看 `cg_net_peer()`（形如 `UDP 本地 51234`）。`broadcast != 0` 时才设 `SO_BROADCAST` —— **要发 `255.255.255.255` 就必须开**，否则 `sendto()` 失败；只收不发广播的场景传 `0` 就行。失败返回 `NULL`，原因在 `cg_net_last_error(NULL)`。 |
| `cg_net_sendto` | 发一个数据报，返回实际发出的字节数；`< 0` 失败。**`len` 可以是 0**（空数据报合法）。每次调用解析一次 `host`（填 IP 无所谓，填域名会多花一点时间）。`port` 必须在 1~65535。 |
| `cg_net_recvfrom` | 收一个数据报，**非阻塞**。`ip_out` / `port_out` 可以传 `NULL` = 不关心来源；`buf` 与 `cap` 必须是有效缓冲区（`cap > 0`）。返回值是三态，见下面那张表。 |
| `cg_net_peer` | 这里不是"对端"，而是**本机这个 UDP 端口**（UDP 没有连接，也就没有唯一对端）：形如 `UDP 本地 9000`，开了广播会带 `（可广播）`。`local_port` 填 0 时**只有这里能知道系统分了哪个端口**。 |
| `cg_net_rx_bytes` / `cg_net_tx_bytes` | 累计收 / 发字节数，与 TCP 用法相同（空数据报按 0 字节计）。 |
| `cg_net_close` | 与 TCP 同一个函数，**幂等、NULL 安全**。UDP 句柄上没有读线程，所以它**不会**像 TCP 那样等一个收超时周期。 |

**UDP 没有"建立连接"这一步**：开完端口就能 `sendto()`，没有 `connect` / `listen` / `accept` 三件套，
也没有"掉线"。广播发现（很典型的用法）就一行：

```c
static cg_net_t *g_bc = NULL;
g_bc = cg_net_udp_open(0, 1);                            /* 本地端口交给系统，1 = 允许广播 */
cg_net_sendto(g_bc, "255.255.255.255", 9000, "WHO", 3);  /* 板子回包，就知道它的 IP 了 */
```

### 10.1 为什么 UDP 句柄上没有读线程（本节最该记住的一段）

TCP 那边能安心用"读线程 + 环形缓冲"，是因为**字节流没有边界** —— 它本来就是一条没有起点终点的水，
谁来搬、分几次搬都不改变数据本身，业务晚一点取也不丢语义。

**数据报不是这样**。一个 UDP 数据报自带两样东西：

- **报文边界** —— "这一坨是一个包"；
- **来源地址** —— "这是谁发的"。

而"读线程 + 环形缓冲"恰好把这两样一起毁掉：

- **环形缓冲只记字节、不记包**：两个数据报会被首尾相接成一条流，业务再也分不出 `ONE` 和 `TWO`
  是两个包还是一个 6 字节的包（`tests/unit/test_net.c` 里专门有一条断言钉这个："不能粘成一条流"）；
- **缓冲里只有字节、没有 `sockaddr`**："谁发的"在写进缓冲那一刻就丢了 —— 而 UDP 的每个包
  **都可能来自不同的人**（一个本地端口面对 N 个说话者，这是 UDP 的常态用法）。

所以 UDP 是**拉取式**：不开线程，业务自己在时钟里 `cg_net_recvfrom()`，**一次拿一个完整数据报 + 它的来源**，
拿到就当场用掉。代价也很直白：**没有后台线程替你攒数据**，内核接收缓冲里的包要你自己及时取，
取晚了就丢（UDP 本来就允许丢）。

### 10.2 返回值是三态，别拿 TCP 的"0 = 没有数据"来套

| 返回 | 含义 |
| --- | --- |
| `> 0` | 收到了一个数据报，共这么多字节；`ip_out` / `port_out` 里是**这个包**的来源 |
| `= 0` | 收到了一个**空数据报** —— UDP 允许发 0 字节，这是**正常现象**，不等于"没收到" |
| `= -1` | **此刻没有数据**（**不是错误**）；参数不合法（`buf` 空 / `cap <= 0` / 句柄不是 UDP）也是 `-1`；**数据报比 `buf` 大**也是 `-1`（此时 `last_error()` 里有话，见 §10.5 / §11） |

想区分"没有数据"和"真出错"，看 `cg_net_last_error()` 是不是空串 —— 这条规则与 TCP 一致。
但有一处**正好相反**，照抄 TCP 代码一定踩：**TCP 的 `read` 返回 0 是"没数据"，UDP 的 `recvfrom` 返回 0 是
"收到了一个空包"**。所以 UDP 的抽干循环不能写 `while (n > 0)`，要写"**收到 `-1` 为止**"，
并且顺手把空包也处理掉：

```c
/* 一拍最多收 64 个包：不限量地收会把消息循环饿死，界面就不响应了（见 §11） */
for (i = 0; i < 64; i++) {
    n = cg_net_recvfrom(u, ip, (int)sizeof(ip), &port, buf, (int)sizeof(buf) - 1);
    if (n < 0) break;                        /* -1 = 这一拍没有了（不是错误） */
    if (n == 0) {                            /* 空数据报：也要照样报一行 */
        cg_term_write(term, CG_TERM_SYS, "（空数据报）\r\n", -1);
        continue;
    }
    buf[n] = '\0';
    line = cg_str_format("[%s:%d] %s", ip, port, buf);   /* 来源必须打出来：UDP 全靠它认人 */
    cg_term_write(term, CG_TERM_RX, line, -1);
    cg_free(line);
}
```

### 10.3 流那套 API 在 UDP 句柄上明确拒绝

不崩，但语义不对的行为宁可**明确拒绝**，别指望它"大概能用"：

| 调用 | UDP 句柄上返回 | 为什么 |
| --- | --- | --- |
| `cg_net_write` | `-1` | "流写"没有目的地这个概念；UDP 要发就用 `cg_net_sendto()` |
| `cg_net_read` | `0` | 数据报不走环形缓冲 |
| `cg_net_available` | `0` | 同上，没有那个缓冲 |
| `cg_net_connected` | `0`（恒为 0） | **UDP 无连接，没有"掉线"这个概念** —— 别拿它判断对端在不在，它永远回答"不在" |
| `cg_net_accept` | `NULL` | 它不是监听句柄 |

### 10.4 完整可抄的例子：开端口 → 时钟里收到 -1 为止

```c
#include "cstackgui.h"

static cg_net_t *g_udp = NULL;          /* 一个句柄 = 一个本地 UDP 端口 */

/* ---- 时钟回调：这里是 UI 线程，碰控件是安全的 ---- */
static void udp_tick(void *user)
{
    cg_app_t     *app  = (cg_app_t *)user;
    cg_widget_t  *term = cg_find(app, "term_log");
    unsigned char buf[1472];
    char          ip[64];
    char         *line;
    int           port = 0;
    int           i, n;

    if (!g_udp) return;                  /* 已关掉：不再排下一拍 */

    for (i = 0; i < 64; i++) {           /* 每拍设上限，防包风暴饿死界面 */
        n = cg_net_recvfrom(g_udp, ip, (int)sizeof(ip), &port, buf, (int)sizeof(buf) - 1);
        if (n < 0) break;                /* -1 = 这一拍没数据（不是错误） */
        if (n == 0) {                    /* 空数据报：也要照样报一行 */
            cg_term_write(term, CG_TERM_SYS, "（空数据报）\r\n", -1);
            continue;
        }
        buf[n] = '\0';
        line = cg_str_format("[%s:%d] %s", ip, port, buf);   /* 来源必须打出来 */
        cg_term_write(term, CG_TERM_RX, line, -1);
        cg_free(line);
    }

    cg_set_timer(app, 30, udp_tick, app); /* 一次性定时器：再挂一次才是"周期" */
}

/* ---- 「打开」按钮 ---- */
static void on_open_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);

    if (g_udp) { cg_net_close(g_udp); g_udp = NULL; }    /* 重复点：先关掉旧的 */

    g_udp = cg_net_udp_open(9000, 0);                    /* 本地 9000，不需要广播 */
    if (!g_udp) {
        cg_term_write(cg_find(app, "term_log"), CG_TERM_SYS, cg_net_last_error(NULL), -1);
        return;
    }
    /* local_port 传 0 时，实际端口要从 peer 里看 */
    cg_term_write(cg_find(app, "term_log"), CG_TERM_SYS, cg_net_peer(g_udp), -1);
    cg_set_timer(app, 30, udp_tick, app);
}
```

与 TCP 的两处写法差别：**没有"断开"要判**（UDP 不会掉线，`cg_net_close()` + 句柄置 `NULL` 就结束了），
以及**每次 `sendto` 都得重新给目的地**（没有连接可以复用）：

```c
if (cg_net_sendto(g_udp, host, port, "PING", 4) < 0)
    cg_term_write(term, CG_TERM_SYS, cg_net_last_error(g_udp), -1);
```

### 10.5 UDP 的边界（有意不做）

| 边界 | 说明 |
| --- | --- |
| **没有读线程、没有接收缓冲** | 见 §10.1。内核接收缓冲里的包要及时取，取晚了就丢。 |
| **装不进 `buf` 的数据报整包废掉** | 数据报比 `buf` 大时 `recvfrom()` 返回 `-1`，`last_error()` 里明确写着 `数据报比接收缓冲区大（尾巴取不回来，换更大的 buf）`（`WSAEMSGSIZE`）—— **不是静默截断**，但返回值与"这一拍没数据"长得一样，所以看 `last_error()`。`buf` 别开小了（常见以太网 MTU 下 `1472` 够），也别拿几十字节的小缓冲去收整个报文。 |
| **不保证到达 / 顺序 / 不重复** | 这是 UDP 本身的语义，不是本层省事。要"可靠"就得在**应用层**自己做序号 + 校验 + 重传（见 §11）。 |
| **一个句柄 = 一个本地端口** | 要同时守两个端口就开两次；没有"多端口聚合"。 |
| **没有组播** | 广播有（`broadcast != 0` → `SO_BROADCAST`），组播（`IP_ADD_MEMBERSHIP`）没有。 |
| **只解析 IPv4** | 与 TCP 同一个限制（内部限定 `AF_INET`），写 IPv6 字面量会直接解析失败。 |
| `cg_net_sendto()` 是同步调用 | 返回的是"已交给本机网络栈"的字节数，**不代表对端收到了** —— UDP 没有 ACK 可等。**没有发送队列**（与串口 / TCP 同一取舍）。 |

## 11. UDP 排查表

**第一步还是本机回环**：两个端口开在 `127.0.0.1` 上互发（`tests/unit/test_net.c` 里两个 UDP 套件就是这么干的，
`tools\run-tests.bat` 下全绿）—— 回环通了，剩下的一定是"地址 / 端口 / 防火墙 / 网段"；回环不通才是代码。
UDP 比 TCP 还多一层好处：**回环同时能验"报文边界没被粘成流"和"来源 IP/端口保真"**，而这两件事正是这套 API 存在的理由。

| 现象 | 先查什么 |
| --- | --- |
| `cg_net_udp_open()` 返回 NULL | ① **先看 `cg_net_last_error(NULL)`**：中文、直接说原因（`本地端口不合法（-1）` / `绑定 UDP 端口失败（被占用？）：端口已被占用（上一次没退干净，或别的程序占了）（10048）`）② `local_port` 只能是 0~65535：`-1`（常有人拿它表示"随便"）和 `70000` 都失败 —— **"随便"要传 `0`，不是 `-1`** ③ 1024 以下要管理员 ④ **端口被占会直接失败、不会静默降级**：UDP 句柄上**有意不设** `SO_REUSEADDR`（TCP 监听那边设了，UDP 这边不设 —— Windows 上它的语义是"允许后来者抢同一个端口"，那样"端口被占"会退化成**不报错但收不到**，而沉默的失败对调试工具是最贵的一种；UDP 又没有 `TIME_WAIT`，本来也不需要靠它快速重绑）。所以 `端口已被占用（10048）` 就是**真的**有人占着：上一次的 exe 没退干净、或别的程序（另一个助手 / 抓包工具）占着。**同一端口第二次开会直接失败，这是有意的取舍**（要响亮的失败，不要沉默的失败）|
| 发得出去但收不到 | ① **UDP 里"发出去"从来不等于"有人收到"** —— 先别怀疑网络，先确认对端**确实绑了这个端口、且在自己那侧调了 `recvfrom`** ② **先本机回环自测**：本机两个端口互发通了，问题就在"对端地址 / 端口 / 中间那台机器" ③ 对端 IP 写对了吗（`sendto` 每次都重新解析一次；域名解析不了会 `< 0`，看 `cg_net_last_error()`）④ **"没反应"是 UDP 的常态而不是故障**：对端程序没起来 / 崩了 / 还没开始收，包就是无声无息地没了，本层收不到任何通知 —— 别把它当成"连接断了"去查 |
| 收不到广播 | ① **开端口时 `broadcast` 必须非 0**：没设 `SO_BROADCAST` 的句柄发广播会失败（只收不发的场景才传 0）② **防火墙**：Windows 防火墙默认拦入站 UDP，第一次监听/外连时弹的"是否允许"点了拒绝就不再弹第二次（去入站规则里看）③ **不在同一个广播域**：广播只在同一网段内走，跨网段要靠设备转发（路由器一般**不**转发 `255.255.255.255`）④ **VPN / 虚拟网卡会截走广播**：机器上同时有 VPN、VMware / VirtualBox 虚拟网卡时，广播可能从那张虚拟网卡出去，板子自然收不到 —— 端口绑的是 `INADDR_ANY`，具体走哪张网卡由路由表决定，排查时先临时禁用无关网卡 |
| 收到了 0 字节，被当成"没收到" | ① **`cg_net_recvfrom()` 返回 0 是"收到了一个空数据报"**，不是"没有数据"（没有数据返回 `-1`）—— 这与 TCP 的 `read` **正好相反**，见 §10.2 ② 循环里写 `while (n > 0)` 就是把空包和"收完"混成一回事，会漏包；正确写法是"收到 `-1` 为止" ③ 空包在 `cg_net_rx_bytes()` 上计 0 字节，所以**别用累计字节数判断"收到过几个包"**，那是两回事 |
| 收到的东西不完整 / 拼不起来 | ① **UDP 保边界（这点比 TCP 好），但不保证顺序、不保证到达、还可能重复** —— 收到"后半条"往往是**前一个包真的丢了**，不是被切开 ② 顺序与完整性交给**应用层**：报文里自己加序号（`seq`）与长度 / 校验（如 CRC），收侧按序号判丢、判乱序、判重 ③ **单个数据报比 `buf` 大：`recvfrom()` 返回 `-1`，并且 `last_error()` 里有一句人话** —— `数据报比接收缓冲区大（尾巴取不回来，换更大的 buf）`（对应 `WSAEMSGSIZE`）；注意它**看起来和"这一拍没数据"一模一样**（都是 `-1`），所以"大包老是收不到"时**一定顺手看一眼 `last_error()`**；`buf` 别给太小（常见以太网 MTU 下 `1472` 够）。反过来 `sendto` 也别发太大（超过 MTU 会被 IP 分片，丢一片整包就废了）④ 文本类报文还有 UTF-8 半个字问题：**UDP 下一条报文就是一个完整包**，不需要 §6 那套"留尾巴拼下一轮"，按包解析即可（但一次没取完的老问题还在） |
| 目标端口写错，一点提示都没有 | **这是 UDP 最反直觉的地方：端口写错不会有任何报错。** TCP 连一个没人听的端口会立刻"拒绝连接"；UDP 的 `sendto()` **照样返回成功的字节数**，包在目标机器上被静默丢弃（运气好能收到一个 ICMP 端口不可达，但本层不把它报给业务）—— 所以"发得成功但对方没反应"时，**第一个要核对的是 IP 和端口，而不是代码**。同理，"对端程序没启动"与"端口写错"在现象上完全一样 |
| 界面卡顿 / 终端不刷新 | ① 时钟有没有真的跑起来（`cg_set_timer` 一次性，回调末尾必须再挂，同 §3 / §6）② **一拍里收得太多**：UDP 没有读线程替你限流，包一多就是"同一拍里无限循环 `recvfrom`" —— 像 §10.4 那样**给每拍设个上限**（几十个包），剩下的下一拍再收 ③ 一次 `cg_term_write()` 刷一次界面，循环里逐段灌没问题（CGUI-FORMAT 2.9）④ 这一层**没有"在读线程里碰控件"那个坑**（本来就没有读线程），但也别反过来以为可以不管时钟 —— 不去 `recvfrom`，就没有任何东西会被推进来 |

## 12. 后面还要做什么（计划中，不是已有能力）

- **WiFi**（`cg_wifi_*`）、**BLE**（`cg_ble_*`）：**计划中，尚未实现**。
- 两者的可行性已在本机工具链上实测过（2026-09-21，探针程序），所以"能不能做"不是问号：
  - **WiFi**：MinGW 自带 `wlanapi.h`，链接 `-lwlanapi`；`WlanOpenHandle` / `WlanEnumInterfaces` /
    `WlanGetAvailableNetworkList` / `WlanQueryInterface`(RSSI) / `WlanConnect` / `WlanDisconnect`
    实跑全通 —— "扫描 + 看信号 + 连/断"这一档可做。
  - **BLE**：链接 `-lbthprops -lbluetoothapis`；`BluetoothGATTGetServices` / `GetCharacteristics` /
    `Get`+`Set`CharacteristicValue（注意是 `Get`/`Set`，不是 `Read`/`Write`）/ `RegisterEvent` /
    `BluetoothFindFirstDevice` 可用。**但只限"Windows 已配对"的设备** —— 未配对设备的广播扫描
    只能走 WinRT（要 C++/WinRT + MSVC），本工具链没有。变通：先让 Windows 配对，或走
    "串口-BLE 透传模块"（那就回到本篇这层串口）。
- **网口 UDP 已经落地**（见 §10~§11）：`cg_net_udp_open` / `cg_net_sendto` / `cg_net_recvfrom`，
  **刻意不套"读线程 + 环形缓冲"**（理由在 §10.1）。还没做的是**组播**（`IP_ADD_MEMBERSHIP`）
  与 **IPv6**（TCP / UDP 都只解析 IPv4）。
- 通信层都落在 `runtime/src/kit/` 里：串口（`cg_serial.c`）列端口走注册表
  `HKLM\HARDWARE\DEVICEMAP\SERIALCOMM`（**不引 SetupAPI，见 §2**），网口（`cg_net.c`）只多一条
  `-lws2_32`（见 §8）—— 两者都**没有**给业务 exe 加"要用户自己配"的依赖。
  **运行时的工作线程只出现在这三个文件里**（串口与 TCP 各自的读线程，外加 GIF 录屏的编码线程
  `cg_win_gif.c` —— 它只碰抓帧缓冲与编码器、抓帧仍在 UI 线程，**UDP 不开线程**，见 §10.1）；
  业务侧永远只看到"在时钟里取字节"。
  WiFi 若落地，也沿用同一套形状（§3）。

## 13. 示例工程

`examples/serialterm/` —— 串口助手，演示第 3 节那套线程模型的完整版（**36 个控件、20 个事件回调**）：

- 端口下拉（`cg_serial_list()`，插拔设备后点「刷新」重新枚举）+ 波特率 + 打开 / 断开 + 清空 + 存日志；
- 一块 `terminal` 同时收 RX 与 TX（按方向着色），中间用 `splitter` 分隔（就是 CGUI-FORMAT 2.9 里
  那个经典布局），右下角一块 `chart` 画"每秒收到多少字节"；
- 显示开关**运行时可切**：16 进制（`cg_term_set_hex`）、时间戳（`cg_term_set_timestamp`）、
  **按帧显示**（`cg_frame_*`，**四种定界可选**：静默 / 换行符 / 定长 / 长度字段；每帧报长度与
  CRC16 结论 —— XCOM 没有这条）、自动保存（收到的字节按十六进制文本追加落盘）；
- 发送侧：单条（HEX / 发送新行）、**定时发送**、**多条发送**（一行一条、`#` 注释、可循环）、
  打开文件 / 发送文件 / 停止发送 —— 功能对标 XCOM；"输入 → 字节"的转换只有一处 `prepare_send()`，
  单条 / 多条 / 文件三条路径共用它；
- **协议组帧面板**：帧头 / 地址 / 功能码 /[数据长度]/ 数据 / 校验（五选一）/ 帧尾 + 实时预览 +
  发送一帧 + 周期发送 —— 这是 XCOM「协议传输」页签的**通用版**（它把厂商私有 IAP 格式写死在界面上，
  换个厂家的设备就不适用）。逻辑在 `cg_proto_build` / `cg_proto_parse`，纯函数、单测兜着；
- **重点看三处**：`on_ready` 里枚举端口 + 起时钟、`arrive` 回调里 `cg_serial_read` → `intake()`
  （一份数据三个去处：日志 / 按帧切分 / 自动保存）、断开时 `cg_serial_close`。
  把这三处抄进自己的工程，串口这块就不会踩到线程的坑；
- **自测不需要硬件**：`serialterm.exe --demo` 不开串口，灌合成字节走**同一条 `intake()`**，
  把"切出的帧 / CRC 结论 / 发送侧解析出的字节"打到 stdout。

`examples/netterm/` —— 网口助手（TCP 客户端 + 服务端 + **UDP 模式**），**与 serialterm 是同一个形状**，看点也还是那三处：

- 客户端半边：就是 §3.1 的三步，把 `cg_serial_*` 换成 `cg_net_*`（§8 写了具体改哪两行），
  外加 §8 那句掉线判断（`cg_net_connected()` 变 0 就收摊）；
- 服务端半边：`cg_net_listen()` + 时钟里 `cg_net_accept(srv, 0)` + 自己那份连接句柄数组
  （§8 里那段 `net_tick` 就是它的骨架）；
- **UDP 半边（第三种模式）**：角色选择里多一项 UDP（本地端口 + 是否允许广播），
  目的地址用**地址 + 端口两个控件**（UDP 没有连接，每次发都得给目的地），收在时钟里循环
  `cg_net_recvfrom`（即 §10.4 那段的完整版），并把来源 IP / 端口打进 terminal。
  **本段只按形状描述、未逐项核对控件与回调名** —— 该示例正与本节同步改动中，以 `examples/netterm/` 里的实际代码为准；
- 上手第一步按 §9 说的做：**先连 `127.0.0.1` 自测**，通了再去连真机器。
