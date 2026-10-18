# 【案例】串口助手：无界面串口组件 + 编辑框接收视图

> 完整工程在 `examples\suites\comm\`：**一条命令编译、双击就能跑**，本文文案与数字都取自它。
>
> **2026-10-18 注**：本文对应的工程已并入 [`examples\suites\${suite}\`](../../examples/suites/${suite})
> （本工具是其中一个页签，完整源码在 `modules\st.c`，整套编译 `build-app.bat suites\comm`）。
> 文中的界面截图与自检数字取自合并前的独立运行；套件自检是整套口径（控件数更多），逐工具走读仍按本文。
> 它不是「入门系列」的某一课，而是一份**实战样板**：把「串口组件（无界面控件）」
> 和「编辑框当接收视图」这两种东西拼起来，讲清「数据从哪来、落到哪去由谁决定」。
>
> 配图：见 `docs\articles\images\` 下随工程提供（必要时补）。

---

## 一、这个例子想讲清什么

「跟板子说话」那类工具，界面其实就三块：**端口/波特率选择 + 收发区 + 速率曲线**。
但真正容易讲不清的是中间那根线——

> **串口是界面里的一个控件，但它不建任何窗口；数据到了发一个事件，数据落到哪个控件由你写。**

也就是说：

- 串口是 `.cgui` 里的一个**无界面控件**（`type: "serial"`，id 为 `ser_main`）：像易语言的串口组件——
  画布上是个图标，**运行时不建窗口**，只带着端口/波特率/数据位/停止位/校验这组参数；
- 链路内部照旧是「读线程 + 64KB 环形缓冲」：读线程只搬字节、**绝不碰控件**（Win32 的硬约束）；
- 运行时在主线程里发现缓冲有数据，就派发 **`arrive`（数据到达）** 事件，回调拿到的 `self` 就是串口组件本身；
- **数据落到哪个控件，由这行代码决定**：本示例把同一份数据**同时**灌进 `edt_recv`（接收编辑框）
  和 `cht_rate`（速率曲线）——换个项目灌进 `listbox`/`label`/你自己的解析器，都只是这个回调里的事。

> 接收视图这里故意**不用 `terminal` 控件**，而用**多行 `edit`**：就是为了演示
> 「无界面控件 + 普通控件」这种自由组合——去哪个控件由 `main.c` 的 `on_arrive` 决定，
> 而不是被某个控件绑架。

---

## 二、跑起来 + 体检报告

```bat
build-app.bat serialterm
examples\suites\comm\comm.exe

:: 自检（不碰硬件，合成触发每个回调，含 arrive）
examples\suites\comm\comm.exe --selftest examples\suites\comm\comm.cgui
```

自检期望：`控件 N 个, 触发回调 M 次, 未登记 0 个, 事件不支持 0 个, 绑定出错 0 个`。
（具体数字以本机当前 `.cgui` 为准；重点是「未登记 / 不支持 / 出错」都是 0。）

---

## 三、一屏总览

| 控件 | 类型 | 干什么 |
| --- | --- | --- |
| 串口组件 | `serial` | **无界面**：链路 + 参数，数据到达发 `arrive`（id `ser_main`） |
| 端口/波特率 | `combo` | 选链路参数；波特率是文字，要 `sscanf` 转数字 |
| 状态栏 | `statusbar` | 三段：连接状态 / RX 字节 / TX 字节（**累计量业务自己记**，断连不归零） |
| 链路灯 | `led` | 连上亮、断开灭 |
| 接收编辑框 | `edit`（multiline） | 接收视图：`on_arrive` 把字节当文本灌进去，自动滚到底 |
| 分隔条 | `splitter`（row） | 上下拖：日志区变高、下面让位 |
| 发送框 | `edit` + `checkbox` + `button` | HEX 可选；回车等价点「发送」 |
| 速率曲线 | `chart` | 每秒推一个点，Y 轴只往上自适应扩 |

---

## 四、核心代码：数据的落点

### 4.1 数据到达：同一份数据，去几个控件你说了算

```c
static void on_arrive(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t     *app  = cg_app_of(self);
    cg_serial_t  *ser  = cg_widget_serial(self);
    unsigned char buf[RX_CHUNK];
    int           loops, n;

    if (!ser) return;                       /* 自检合成触发：链路不存在，直接走 */

    for (loops = 0; loops < RX_MAX_CHUNKS; loops++) {
        if (cg_serial_available(ser) <= 0) break;
        n = cg_serial_read(ser, buf, (int)sizeof(buf));
        if (n <= 0) break;
        recv_append(app, (const char *)buf, n);   /* 去处 1：接收编辑框 */
        s_biz.rx_window += n;                      /* 去处 2：速率曲线（1 秒聚一次） */
        s_biz.rx_total  += n;
    }
    refresh_stats(app);
}
```

`recv_append` 自己维护一个**环形缓冲**（超了从最老处丢），每次 `set_text` 整段重设，
再 `cg_edit_scroll_end` 把新数据露出来（`set_text` 会把编辑框滚回顶部，所以要救回来）：

```c
static void recv_append(cg_app_t *app, const char *data, int n)
{
    cg_widget_t *edt = cg_find(app, "edt_recv");
    if (n <= 0) return;
    /* 环形：装不下就丢最老的那截 */
    if (s_biz.recv_len + n > RECV_CAP) { /* ... memmove 丢最老 ... */ }
    memcpy(s_biz.recv_buf + s_biz.recv_len, data, (size_t)n);
    s_biz.recv_len += n;
    s_biz.recv_buf[s_biz.recv_len] = '\0';
    if (edt) {
        cg_widget_set_text(edt, s_biz.recv_buf);
        cg_edit_scroll_end(edt);                  /* set_text 会滚回顶部，这里救回来 */
    }
}
```

### 4.2 链路开关/销毁全归运行时，发送按 HEX 勾选

- 打开失败别只报「失败了」：`cg_serial_last_error(NULL)` 给的是中文原因
  （端口被占用 / 找不到 / 波特率不支持），直接写进接收视图；
- HEX 发送的手写解析（`hex_parse`）**不用 `strtok`**：它会就地改原串（控件内部串动不得），
  而且 `1A2B` 这种没分隔符的也得算对；
- 链路的开关、销毁在 app 退出时由运行时自动收，业务代码不用自己兜底。

---

## 五、踩坑清单（写对了的地方）

1. **`arrive`/`on_ready` 第一件事判 `cg_widget_serial()` / `cg_app_has_window()` 是不是 NULL**：
   自检会合成触发这些回调，那时链路不存在、窗口没建，**绝不能去碰硬件或弹对话框**。
2. **累计收发明细业务自己留一份**（`rx_total`/`tx_total`）：链路断了句柄就没了，
   状态栏不能因此瞬间归零，让人以为数据丢了。
3. **`set_text` 会把编辑框滚回顶部**，所以 `recv_append` 末尾必跟 `cg_edit_scroll_end`。
4. **`combo` 里存的是文字**：波特率要先 `sscanf` 转数字再写进组件。
5. **HEX 解析手写、不用 `strtok`**；数字个数必须是偶数，落单半个字节算写错。
6. **状态栏写在 `widgets` 数组前面**（本示例它就在最前）——有分隔条的窗口若把状态栏写后面，
   拖分隔条会去改状态栏的宽度（见入门 04 的同一坑）。

---

## 六、已知问题（已修复）：多行编辑框接收视图会画出横贯实心线

**症状**：串口数据灌进 `edt_recv`（多行 `edit`）后，内容区里出现**一条条横贯整行的实心线**
（颜色和边框同色系，隔几行一条）。

**已定位并修复（2026-09-25）**：不是数据问题，也不是 `\r\n` 转换问题
（`cgw_wc_from_utf8_crlf` 对 `\r\n` 是幂等的，不会变成 `\r\r\n`）。
用 `tests\unit\test_skin_edit.c` 的「编辑框：多行渲染实心线」套件做了像素级复现：

- **复现条件**：多行 `edit` + **挂上皮肤**（`cg_skin_apply_edit`）+ **反复 `SetWindowText` + `cg_edit_scroll_end` 滚动**；
- **干净路径不重现**：① 不挂皮肤 / ② 单层弹窗单次灌入 —— 都没有线条；
- **重现点**：子窗口 + 挂皮肤 + 流式灌入，约第 20 轮扫描到一条**整行 100% 覆盖率**的实心线
  （正常文本行字间有空隙，覆盖率到不了 ~80%，一眼可分）；抓到的颜色 `RGB=B8B2BD` 正是皮肤默认灰
  `SK_EDGE_GREY`，确认是皮肤画的，不是 EDIT 自己。

**根因（已推翻两个误诊）**：

1. 不是圆角窗口区域（`SetWindowRgn`）导致 EDIT 滚动死循环——回退该猜测后问题依旧；
2. 不是 `WM_SIZE` 漏转发——子类末尾已 `DefSubclassProc`，顺序无碍。

**真正根因**：`sk_edit_box()`（画边框）被挂在了 `WM_PAINT` 分支里，用 `GetWindowDC` 画非客户区边框。
流式灌入滚动时 EDIT 在客户区重画，DC 带滚动偏移，边框就被画进了内容区，留下一条皮肤灰横贯线。

**修法**：边框是非客户区，只在 `WM_NCPAINT` / `WM_SETFOCUS` / `WM_KILLFOCUS` 时重画；
`WM_PAINT` 只走 `DefSubclassProc` 让 EDIT 画客户区（提交 `1af3513 [修复][皮肤] ...`）。

**复现方法沉淀**见 `.codebuddy/skills/firmware_code_style` 的 §7；该套件现为回归测试，修复后转绿。

---

## 七、验收

```bat
examples\suites\comm\comm.exe --selftest examples\suites\comm\comm.cgui
:: 期望：未登记 0 个, 事件不支持 0 个, 绑定出错 0 个
```

---

## 八、下一篇

回到入门系列：[入门 07 实战-记事本](07-实战-记事本.md)
｜ 无界面控件专题：[入门 04 数据与日志](04-数据与日志.md)（那里讲 `terminal` 控件，和本例的「edit 当接收视图」对照看）。

> 系列导航：上一篇：[入门 07 实战-记事本](07-实战-记事本.md)

---

## 附：本例文件清单

```
examples\suites\comm\
  serialterm.cgui   界面：状态栏 / 串口组件(serial) / 接收编辑框(multiline) / 分隔条 / 发送组 / 速率曲线
  main.c            行为：on_arrive 定落点、recv_append 环形缓冲 + 滚到底、HEX 发送、配置记忆
```

---

## 附：排查过程中沉淀的测试

`tests\unit\test_skin_edit.c` 新增了「编辑框：多行渲染实心线」套件（含像素扫描与流式复现），
目前作为**该 bug 的回归测试**存在：未修复前它会 FAIL（第 25 轮扫到实心线），修复后应当转绿。
