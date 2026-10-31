---
name: test-driven-development
description: C 层测试先行（RED→GREEN→REFACTOR）——没有先失败的断言，不写生产代码。触发条件：改 runtime/src 的行为、修 bug（先写复现测试）、给 core/kit 加能力、重构；用户说"加测试""测试驱动""写个单测"；`piv-implement` 写行为代码前自动触发。
auto_invoke: true
argument-hint: [模块名 | 要修的行为 | 要加的能力]
---

# 测试驱动开发（C 层）

**输入**：$ARGUMENTS

来源：`obra/superpowers` 的 `test-driven-development`（30.9 万★），
把 TS/Jest 语境换成**本工程真实可跑**的 `tests/unit/` + `cg_test.h` + `tools\run-tests.bat`。

## 铁律

```
没有先行失败的测试，就不写生产代码。
```

先写了代码再补测试？**删掉重写。** 不当作参考、不"边写测试边改造它"。
理由：事后写的测试会立刻通过，而**"立刻通过"什么也证明不了**——它可能测错了对象。

**没有例外**，除以下三种（须在交付说明里点名）：
一次性原型、生成代码、纯配置/纯声明（如新增一个宏）。

## 本工程测试设施（用这些，别新建框架）

| 件 | 位置 | 说明 |
|---|---|---|
| 断言 | `tests/cg_test.h` | `cg_test_ok(what, cond)` / `cg_test_int(what, got, want)` / `cg_test_str(what, got, want)` / `cg_test_note(text)` |
| 注册 | `CG_TEST_SUITE(name, fn)` | 写在套件函数**之后、文件末尾**；编译期扫描 + 运行时自注册，**不用改任何清单** |
| 跑法 | `tools\run-tests.bat` | 全量；`--memcheck` 叠堆泄漏记账；`--no-kit` 跳过 kit 冒烟 |
| 单个套件 | 测试程序的 `--grep=子串` | 调试期用，**收尾必须跑全量** |

断言失败**只记账、不终止**：一个套件里能一次看到全部失败项，退出码非 0 表示有失败。

## 🔴 RED —— 先写一个会失败的断言

在对应文件里加套件（改了 `cg_kit` 的切帧就放 `tests/unit/test_kit_frame.c`，
新模块就新建 `tests/unit/test_<模块>.c`）：

```c
/* 示意：签名与语义以 runtime/include/cstackgui.h 为准 */
static void suite_frame_gap(void)
{
    cg_frame_t    f;
    unsigned char buf[64];
    unsigned char out[16];

    cg_test_ok("init 成功",
               cg_frame_init(&f, buf, (int)sizeof buf, CG_FRAME_SILENCE, 3) == 0);

    cg_frame_feed(&f, "AB", 2, 0);
    cg_test_int("空档不足不切", cg_frame_take(&f, out, (int)sizeof out), 0);

    cg_frame_feed(&f, "C", 1, 5);          /* 距上一字节 5ms > 3ms：先把 AB 收尾 */
    cg_test_int("超静默切出上一帧", cg_frame_take(&f, out, (int)sizeof out), 2);
    cg_test_int("帧内容为 AB", (out[0] == 'A' && out[1] == 'B'), 1);
}

CG_TEST_SUITE("frame/silence-gap", suite_frame_gap);
```

**好断言的三个特征**：
- **一个行为**：名字里出现"和/并且"就拆成两条；
- **名字说行为**：`"空档不足不切"` 而不是 `"test1"`；
- **测外部不变量**：不知道实现细节也能判定对错（`test_kit_frame.c` 的做法是"切出来的帧必须能被
  `cg_modbus_parse` 接受"、"灌噪声时缓冲两侧金丝雀一个字节不许变"）。

## ✅ 验证 RED —— 亲眼看着它失败（强制，不许跳）

```bat
tools\run-tests.bat
```

必须确认三件事：
- [ ] 输出里出现 `[ RUN  ] <你的套件>` 且对应断言是 `[FAIL] <断言名>`
- [ ] 失败原因**是功能缺失**（例：`[FAIL] 超静默切出上一帧 （实际 0，期望 2）`），
      不是拼写错、不是段错误、不是没编译进去
- [ ] 结尾是 `== 单测 FAILED ==`（**不是** `ALL PASS`）

异常处理：
- **一写就全过** → 你在测已有行为。改断言，或换一个真的还不存在的目标。
- **测试程序崩了 / 套件没出现** → 先修测试本身，直到它以**正确方式**失败为止。
- **`[fail] 有 N 个套件因为超出上限 256 没跑`** → 这是真失败，不是"通过"。

## 🟢 GREEN —— 写最小代码让它变绿

```bat
tools\run-tests.bat
```

- 只写**够让这条断言过**的代码：不加参数、不加选项、不加"以后可能要用"的分支（配合 `ponytail`）。
- **不许顺手重构别的代码**，不许改其它断言。
- 测试失败 → **改实现，不改断言**。
- **其它套件红了 → 立刻修**，不能"不是我弄的就不管"。

## 🔵 REFACTOR —— 绿灯之后才清理

消重复、改命名、抽辅助函数。**全程保持绿灯，不新增行为。**

## 🔁 重复

一条行为 → 一次循环。多个行为不要凑成一次大批量修改。

## 本工程特有的四条

1. **不用 mock**：C 层靠"假输入"而不是假对象——假时间戳（`cg_frame_feed` 的 `now_ms` 由调用方给，
   一轮都不用 sleep）、回环、可注入的坏数据。
2. **测行为不测实现**：断言 `take` 出来的字节，不断言内部游标下标。
3. **资源必须配对**：涉及 malloc / `CreateXxx` 的套件，收尾跑 `--memcheck`，看"这一套跑完没有新增存活堆块"。
4. **跨端契约也要测**：改了 `.cgui` 字段 / 控件类型表 / 事件表，跑 `tools\check-format.bat`——
   它和单测同等地位（都是"能变红"的判据）。

## 反合理化对照表

| 你说的 | 现实 |
|---|---|
| "太简单了，不用测" | 简单代码也会坏，写条断言 30 秒。 |
| "我先实现，回头补测试" | 事后测试立刻变绿，证明不了任何事。 |
| "我手动跑过 `--selftest` 了" | 手动验证没有覆盖记录，代码一改就没法重跑。 |
| "删掉两小时的工作太浪费" | 沉没成本。真正的选择是「重写（高置信度）」vs「留着（低置信度）」。 |
| "留着当参考，我先写测试" | 你会去"适配"它，那就成了事后测试。删除就是删除。 |
| "现有代码本来就没测试" | 你正要改进它。为它补测试。 |
| "TDD 拖慢我" | 快的那条路叫"在生产里调试"。 |

## 危险信号（出现即删掉代码重来）

代码先于测试；测试第一次就通过；说不清测试为什么失败；"就这一次"；"这次情况不一样"。

## 输出

- 改动：`tests/unit/test_<模块>.c`（新增/追加套件） + 被驱动的生产代码。
- 交付说明里必须给出 **RED 证据**：那条断言在实现前失败时的原文（如 `[FAIL] 超静默切出上一帧 （实际 0，期望 2）`）。
  没有这段原文 = 没做 TDD。
- 收尾：`tools\run-tests.bat` 全量 `== 单测 ALL PASS ==`（改过 malloc/free 再叠 `--memcheck`）。
