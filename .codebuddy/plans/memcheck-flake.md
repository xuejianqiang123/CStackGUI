# 既有问题：`状态灯 / 仪表盘` 套件在 memcheck 下会间歇性中止（与本次改动无关）

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
