# 已解决：memcheck 下间歇性崩溃 —— 根因是我的 `WlanCloseHandle` 反复开关

> **2026-09-27 已修好**，根因与最初判断**完全相反**，过程值得留着。

## 结论（修法）

`runtime/src/kit/cg_kit_wireless.c` 原来**每次调用都 `WlanOpenHandle` + `WlanCloseHandle`**。
实测反复开关会让 `WlanCloseHandle` **间歇性崩在 rpcrt4**，gdb 抓到的栈：

```
Thread 1 received signal SIGSEGV
#0  RPCRT4!NDRCContextBinding
#1  RpcSsDestroyClientContext
#2  wlanapi!WlanCloseHandle
#3  cg_wifi_networks ()
#4  test_wifi () at tests/unit/test_kit_wireless.c:74
```

改成**进程内开一次、复用**（`wlan_handle()`）后：gdb 三轮 `exited normally`、直接连跑 **6 次退出码全 0**。

## 为什么前后错过了三次（都被"证据不足"骗了）

1. 看日志尾部在 led/meter 那里，就当成"led 套件的问题"；
2. 把我的测试文件移开跑了一次绿的，就判"不是我引入的"；
3. 又怀疑"输出被缓冲吃掉了"，而框架的 main **本来就设了 `_IONBF`**。

三条都是同一个毛病：**崩点随机漂移，而日志尾部只是"最后打印的"，不等于崩点**。
真正的转折是**上 gdb 拿调用栈**（一次就中）—— 位置漂移的崩溃不要靠读日志猜。

## 遗留：汇总行的套件数在 memcheck 构建下印错（功能不受影响）

`--memcheck` 构建里汇总行印 `62 个套件（注册 114 个）`。已核实**功能是全量跑的**：
`这一套跑完没有新增存活堆块` 出现 **114 次**（每个套件一条）、断言数 2826 = 非 memcheck 全量 2712 + 114。
非 memcheck 构建印的是正确的 114。差异八成来自那段"热身跑"，根因未查。
已在汇总行**把两个数一起印**并在不一致时 `[warn]`，免得以后这种不一致继续藏着。

## 原始记录（现象，供对照）

## 现象

`tools\run-tests.bat --memcheck` 有时会失败：日志最后停在这三行之后，**没有 `[FAIL]` 行**，
直接是 `[fail] unit tests failed`：

```
[cstackgui] cg_meter_set: 需要 meter 控件，实际给的是 listbox
[cstackgui] cg_led_set: 需要 led 控件，实际给的是 listbox
[cstackgui] cg_led_set_color: 需要 led 控件，实际给的是 listbox
```

⇒ 像是**进程在中止**（不是断言失败）。测试框架 main 里本来就设了 `setvbuf(stdout, NULL, _IONBF, 0)`
（"卡死/被杀也能看到最后进度"），所以**日志尾部是可信的真实最后输出**，不存在"被缓冲区吃掉"。

## 证据（2026-09-27 实测）

| 跑法 | 结果 |
| --- | --- |
| `--memcheck --grep=led` | **失败**（那一跑里无线套件根本没参与 ⇒ 与 cg_kit_wireless.c 无关） |
| `--memcheck --grep=meter` | 通过 |
| `--grep=led`（**不带** memcheck）连跑两次 | **都通过** ⇒ 与 memcheck 的 `--wrap malloc/free` 相关 |
| 全量 `--memcheck` | 失败 3 次、通过 2 次 ⇒ **间歇性** |

## 下一步怎么查（留个入口）

1. 先看清是哪个套件、哪一行：`tools\run-tests.bat --memcheck --grep=状态灯`，
   必要时在 `tests/unit/test_led_meter.c` 的守卫套件里临时加点打印；
2. 重点看 `状态灯：颜色与类型守卫` / `仪表盘：设值与类型守卫` —— 那三行消息正是它们打出来的，
   而它们测的正是"给错类型时不许崩"；
3. 怀疑方向：memcheck 的 `--wrap free` 遇到"不是它记的指针"（或重复释放）时会直接中止 ——
   这类守卫路径上如果有 `cg_free` 一个非常量/未分配指针的分支，memcheck 下就会炸、正常跑却没事。
4. 修完请顺带确认：`--memcheck --grep=led` 连跑 5 次都过。

## 与本次改动的关系

**无**（见上表第二行）。本次只新增 `cg_kit_wireless.c` 与它的测试，未触碰 led/meter 的任何代码。
