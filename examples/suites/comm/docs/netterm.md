> **本文档是合并前独立工程 `examples\netterm\` 的原文**（2026-10-18 该工程已并入
> [`examples\suites\comm\`](../comm)：本工具的完整逻辑在 `modules\nt.c`，
> 编译 `build-app.bat suites\comm`，五个工具各占一个页签）。
>
> 文中「目录 / 编译 / 运行 / 自检」那几行命令是**合并前的口径，现已失效**（原工程目录已删）；
> 其余内容 —— 它演示什么、关键接口、容易踩的坑 —— 仍然逐条成立，对应 `modules\nt.c`。
# 网口调试助手：TCP 客户端 / 服务端 / UDP 三种角色共用一套接口

> 目录 `examples\netterm\`　编译 `build-app.bat netterm`　运行 `examples\netterm\netterm.exe`　自检 `netterm.exe --selftest` → 控件 19 个 / 回调 9 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 一、它演示什么
一个网口调试助手，客户端 / 服务端 / UDP 三种角色共用同一套 `cg_net_*` 接口。最想讲清的是**线程模型**：读线程只把 socket 字节搬进 64KB 环形缓冲，**绝不碰控件**；数据靠 `cg_set_timer(50)` 的时钟回调按时来取。
`cg_net` 比 `cg_serial` 多的一件事是**一个句柄 = 一条连接**：服务端要同时管多个客户端就得自己拿数组（本示例 8 条）。UDP 是刻意做的例外 —— 报文的"边界"和"谁发的"必须原样交给业务，所以收是拉取式的。工控价值：写上位机、做协议联调都能照着改。

## 二、怎么用
1. 顶部 `cmb_role` 选角色：
   - 客户端（连接）：填「地址:端口」→ 点「连接」。
   - 服务端（监听）：填本机端口 → 点「监听」，等客户端连进来，可同时管多条连接。
   - UDP：填本机端口 → 点「打开 UDP」绑定端口；这时「地址:端口」是**发给谁**（本机自测填 `127.0.0.1`，想收广播填广播端口）。
2. 选 UDP 时，「监听」按钮会自动改叫「打开 UDP」——别看 `.cgui` 里的静态文字。
3. 发送区 `edt_send` 输入内容，勾 `chk_hex` 就写十六进制；点「发送」或回车。服务端会同时发给所有连上的客户端；UDP 下发给「地址:端口」指定的目的地。
4. 「断开」关掉监听 / UDP 端口与全部连接（幂等，重复点没事）。
5. 「清空」清空日志（累计 RX/TX 计数保留）；`led_link` 与状态栏显示连接状态、对端与收发字节。
6. `cht_rate` 显示每秒收到字节数；`sp_main` 分隔条可上下拖，日志区 / 发送区联动。
7. 地址 / 端口 / 角色 / 分隔条会记进 INI，下次启动自动恢复。

## 三、一屏总览
| 控件 id | 类型 | 干什么 |
| --- | --- | --- |
| `cmb_role` | combo | 选客户端 / 服务端 / UDP |
| `edt_host` / `spn_port` | edit | 远端地址 / 端口 |
| `btn_conn` / `btn_listen` / `btn_stop` | button | 连接 / 监听（UDP 下变「打开 UDP」）/ 断开 |
| `led_link` | led | 链路状态灯 |
| `term_log` | terminal | 收发日志（RX 绿 / TX 蓝 / 系统灰） |
| `sp_main` | splitter | 日志区与发送区分界 |
| `edt_send` / `chk_hex` / `btn_send` | edit/checkbox/button | 发送 |
| `cht_rate` | chart | 每秒收到字节数 |

## 四、关键接口
- `cg_net_connect` / `cg_net_listen` / `cg_net_accept`：三种角色的入口（连接、监听、收连接）。
- `cg_net_read` / `cg_net_write`：在时钟回调里取字节、发字节。
- `cg_net_udp_open` / `cg_net_recvfrom` / `cg_net_sendto`：UDP 收发，一次一个完整数据报。
- `cg_net_available` / `cg_net_connected` / `cg_net_peer` / `cg_net_last_error` / `cg_net_close`：查询与收尾。
- `cg_set_timer`：50ms 轮询（一次性定时器，回调末尾再挂一次）。
- `cg_term_write`：日志落点。
- `cg_app_has_window`：真连网前先问，自检直接返回。

## 五、容易踩的坑
- `cg_net_accept(srv, 0)` 的 timeout=0 表示"不等、立刻返回"：**没有连接时返回 NULL 是正常情况，不是错误**，正因如此才适合放在时钟里轮询。
- 连接 / 监听失败时句柄是 NULL，原因用 `cg_net_last_error(NULL)` 取。
- 掉线的**唯一信号**是 `cg_net_connected()` 变 0；对端关闭后缓冲里可能还有尾巴，要先取干净再回收句柄。
- UDP 那支的返回值和 TCP 不通用：`> 0` 字节数、`= 0` 空数据报（合法，是"收到了一帧"）、`= -1` 暂时没有数据（也不是错误）；判断出没出错看 `cg_net_last_error(u)` 是不是空串，而那个串是**粘的**，报一次就够。
- 断开与退出要对**每一个**句柄 `cg_net_close()`（幂等且 NULL 安全）。

## 六、相关文档
- `examples\教学使用说明.md` → 「七、实用工具详解 · netterm」。
- `docs\CGUI-FORMAT.md` → 第 2.9 节（`terminal`）、第 2.10 节（`chart`）、第 4 节（控件类型表）。
