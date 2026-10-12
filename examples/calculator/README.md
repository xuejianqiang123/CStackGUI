# CStackGUI 示例：一个能算表达式 / 转时间戳 / 进制 / 拼位宽的「万能计算器」

> 本文对应的源码在 `examples/calculator/`：界面 `calculator.cgui` + 业务 `main.c`，
> 双击 `calculator.exe` 即可运行，零依赖、复制走就能跑。

---

## 一、这个示例能干嘛

它不是一个只会 `1+1` 的计算器，而是一个**把日常调试里反复要用到的小工具揉进一个窗口**的合集，分六个区：

| 分区 | 功能 | 一句话 |
| --- | --- | --- |
| 计算 | 表达式求值 | 四则 / 括号 / 幂 / 位运算 / 20 个常用函数，回车即算 |
| 时间戳 | Unix 秒 ↔ UTC 文本 | 生成当前时间戳并自动复制进剪贴板 |
| 进制转换 | 2 / 8 / 10 / 16 互转 | 支持 `0x` / `0b` 前缀，两侧都能按位取反 |
| 长度计算 | 字节 / 字符 / 行数 | 按 UTF-8 码点统计，中文不乱码 |
| 字符串转换 | 文本 ↔ HEX | 可给 HEX 加 / 去空格 |
| 位运算 | 位宽'值 拼接 | `29'h0, 1'h1, 1'h0, 1'h0` → 一个整数 + 分组二进制 |

界面长这样（窗口 840×492，底色 `#F3EDF7`，底部一条状态栏）：

```
┌────────────────────────────────── 计算器 ──────────────────────────────────┐
│ ┌─ 计算 ───────────────┐  ┌─ 时间戳 ─────────────────────────────────────┐ │
│ │ 计算式：[ (1+2)*3-4/2 ] [计算] │ [点击生成时间戳(自动复)]               │ │
│ │ 结果 ：[ 7          ] [查看公式]│ [1762886400] [2025-...] [时间戳→UTC]   │ │
│ └──────────────────────┘  └──────────────────────────────────────────────┘ │
│ ┌─ 进制转换 ─────────────────────────────────────────────────────────────┐ │
│ │ 原进制 [十]  被进制 [十六]  数字 [255] [取反]  结果 [FF] [取反]         │ │
│ └────────────────────────────────────────────────────────────────────────┘ │
│ ┌─ 长度计算 ───────┐  ┌─ 字符串转换 ───────────────────────────────────┐ │
│ │ [多行文本框]   [计算] 输出：… │ [文本] [STR→HEX] [HEX] [HEX→STR] …    │ │
│ └──────────────────┘  └────────────────────────────────────────────────┘ │
│ ┌─ 位运算 ──────────────────────────────────────────────────────────────┐ │
│ │ [29'h0, 1'h1, …] [转换]  结果：0x20000000 · 000…010                  │ │
│ └───────────────────────────────────────────────────────────────────────┘ │
│ 就绪：六个分区都可以用了                                  [状态栏第二段]     │
└────────────────────────────────────────────────────────────────────────────┘
```

---

## 二、架构：界面与逻辑彻底分离

CStackGUI 的核心思路就一句话：**界面用 JSON 画出来，逻辑用 C 写回调**。

- `calculator.cgui` —— 纯声明式界面描述。里面罗列了 6 个 `group`、若干
  `label / edit / button / radio / checkbox`，每个控件一个 `id`，
  有交互的控件在 `events` 里写回调名（如 `"click": "on_calc_click"`）。
  **改长相不用重编 C**，改完 `.cgui` 直接生效。
- `main.c` —— 纯业务逻辑。只做两件事：
  1. 用 `CG_BIND("on_calc_click", on_calc_click)` 把 `.cgui` 里的名字和 C 函数绑定起来；
  2. 在回调里用 `cg_find(app, "edt_expr")` 按 id 取控件、读写文字、算结果。

入口极简：

```c
int main(int argc, char **argv)
{
    cg_app_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.bindings         = k_bindings;     // 那一堆 CG_BIND
    desc.auto_locate_cgui = 1;             // 双击 exe 也能自动找到旁边的 .cgui
    desc.on_ready         = on_ready;      // 界面就绪钩子：预填样例
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0)
        return cg_app_selftest(&desc);     // 不开窗自检
    desc.cgui_path = (argc > 1) ? argv[1] : "calculator.cgui";
    return cg_app_run(&desc);
}
```

---

## 三、重点：表达式求值器（递归下降手写解析器）

`计算` 区背后是一个**不依赖任何第三方库**的递归下降表达式求值器，把「写个计算器」里最头疼的词法/语法/优先级一次性讲清楚。语法层级从低到高：

```
ep_expr   : 加减层   + -
ep_term   : 乘除层   * / %
ep_bitor  : 位运算层 & | ^ << >>
ep_unary  : 一元     - + ~
ep_power  : 幂运算   ^   （右结合）
ep_atom   : 数 / 常量 / 函数 / 括号
```

支持的运算符与符号：

- **四则 + 取余**：`+ - * / %`
- **幂**：`^`（右结合，`2^3^2 == 2^(3^2)`）
- **位运算**：`& | ^ << >>`（`~` 一元按位取反，操作数取整后按 64 位算）
- **常量**：`pi`、`e`
- **前缀**：`0x` 十六进制、`0b` 二进制（`255` / `0xff` / `11111111` 同义）
- **函数（20 个）**：`sqrt cbrt ln log log2 exp sin cos tan asin acos atan abs floor ceil round min max pow`

体验几条：

```
(1+2)*3-4/2        → 7
0xff & 0x0f        → 15
sqrt(2)            → 1.41421356237
2^10              → 1024
~0xF              → -16（64 位按位取反）
min(3,8)+max(1,2)  → 5
```

解析器的工程化细节（也是这篇最值得抄的部分）：

1. **错误信息友好**：`ep_err()` 只记第一次错误，报错带位置，比如「括号不配平：少 )」「移位量要在 0~63」「除数为 0」。
2. **防御性边界**：表达式长度 ≤ 512、递归深度 ≤ 16、位宽 ≤ 64、段数 ≤ 16，所有输出走定长缓冲 + `snprintf`，**不会溢出**。
3. **结果合法校验**：`isnan / isinf` 一律判失败，「结果不是有限数」直接打回。
4. **常量歧义处理**：识别 `e` 时会看后面是不是字母，避免把 `exp(2)` 里的 `e` 当常量吃掉。

求值入口：

```c
static int ca_eval(const char *expr, double *out, char *errbuf, int errcap)
{
    ca_ep_t p;
    errbuf[0] = '\0';
    if (!expr || !*expr) { snprintf(errbuf, errcap, "计算式是空的"); return 0; }
    if (strlen(expr) > CA_EXPR_MAX) { snprintf(errbuf, errcap, "表达式太长"); return 0; }
    memset(&p, 0, sizeof(p));
    p.s = expr;
    *out = ep_expr(&p);
    if (p.err[0]) { snprintf(errbuf, errcap, "%s", p.err); return 0; }
    /* 尾巴还有多余字符也算错 */
    if (p.s[p.pos] != '\0') { snprintf(errbuf, errcap, "结尾有多余字符：%c", p.s[p.pos]); return 0; }
    if (isnan(*out) || isinf(*out)) { snprintf(errbuf, errcap, "结果不是有限数"); return 0; }
    return 1;
}
```

---

## 四、其它几个区的关键点

### 时间戳（免 `timegm` 依赖）
UTC 文本 → 秒用的是 Howard Hinnant 的「公历转天数」算法，纯整数运算，跨平台不需要 `timegm`：

```c
static long long ca_days_from_civil(int y, int m, int d) { /* ... */ }
```

「生成时间戳」会把当前 `unix` 秒写进输入框、同步出 UTC，并 `cg_clip_set_text()` 复制到剪贴板——**但只在有窗口时才复制**（`cg_app_has_window(app)` 判断），保证自检模式不碰剪贴板。

### 进制转换（容忍前缀 + 64 位取反）
`ca_parse_ull()` 先识别 `0x` / `0b`，没有前缀才按单选组的 base 走 `strtoull`；`do_negate()` 对结果 `~v` 后按当前进制格式化回去，所以「取反」在十进制下也能用（得到补码意义上的大整数）。

### 长度计算（UTF-8 正确）
字节数用 `strlen`，字符数**按 UTF-8 续字节规则数码点**（`(c & 0xC0) != 0x80` 才是新字符），所以「Hello, 计算器」是 11 字节 / 9 字符，中文不会算错。

### 字符串 / HEX（原地替换 + 安全显示）
`STR转HEX` 调 `cg_hex_encode()` 编成大写 HEX（空格分隔）写回原框；`HEX转STR` 调 `cg_hex_decode()` 解出来后，不可打印字符显示成 `.`，避免把二进制塞进文本框炸版。

### 位运算拼接（首段最高位）
输入 `29'h0, 1'h1, 1'h0, 1'h0`，按「位宽'值」切段，从左到右左移拼接，输出 `0x20000000` 和按段分组的二进制 `000...0 1 0 0`，**正好对应硬件寄存器拼接的写法**。

---

## 五、事件模型：控制台控件自己派发

所有交互都通过 `.cgui` 里写的回调名路由，C 侧只写函数。例如编辑「计算式」时回车即算：

```c
static void on_expr_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev; (void)user;
    do_calc(cg_app_of(self));   // 从控件反查 app，再算
}
```

注意 `字符串转换` 区的两个输入框**故意不写 change 即时转换**——因为「STR转HEX」是把结果写回同一个框，会再次触发 `change` 造成递归。这里只在 `change` 里给个状态栏提示，真正的转换交给按钮点击。这是用 CStackGUI 写「原地写回」类功能的一个标准坑。

---

## 六、如何编译与运行

### 前置
- 发布包：把 `build-app.bat` 放在 `CStackGUI.exe` 旁边即可，不需要 Node。
- 源码仓库：机器上装了 Node.js（仓库工具本身就用 Node）。

### 编译这个示例
在项目根目录执行：

```bat
build-app.bat calculator
```

产物是 `examples/calculator/calculator.exe`，**零依赖、单文件**。要是手头没有构建环境，直接双击目录里已经编好的 `calculator.exe` 也能跑（`auto_locate_cgui = 1` 会自动找旁边的 `.cgui`）。

### 跑自检（不开窗、CI 友好）
```bat
calculator.exe --selftest
```
`--selftest` 会走 `on_ready` 预填全部输入框并跑通每条路径，全程不弹窗、不写盘、不碰剪贴板，退出码即结果——适合流水线门禁。

---

## 七、用到的 CStackGUI API 速查

| API | 用途 |
| --- | --- |
| `cg_find(app, id)` | 按 id 取控件（找不到返回 NULL，调用方一律容忍） |
| `cg_widget_text(w)` / `cg_widget_set_text(w, t)` | 读写控件文字 |
| `cg_widget_checked(w)` | 读单选 / 复选状态 |
| `cg_status_set(w, 0, s)` | 写状态栏第一段 |
| `cg_clip_set_text(s)` | 写剪贴板（返回 0 成功） |
| `cg_hex_encode / cg_hex_decode` | HEX 编解码（空格 / 逗号可分隔） |
| `cg_app_has_window(app)` | 判断是否开了窗（自检时跳过副作用） |
| `cg_log(level, fmt, ...)` | 写日志 |
| `CG_BIND(name, fn)` | 把 `.cgui` 里的回调名绑到 C 函数 |

---

## 八、想接着改？几个切入点

1. **加函数**：往 `k_funcs[]` 表里加一行 `{ "xxx", 1, CA_F_XXX }`，在 `switch` 里加分支即可，解析器自动认得。
2. **加分区**：在 `.cgui` 里复制一个 `group` 块、起新 id，在 `main.c` 里加回调并 `CG_BIND`，逻辑完全独立。
3. **换皮肤**：改 `.cgui` 的 `app.bg` 或控件 `style`，不用动 C。
4. **做工具栏**：把常用操作提成「动作（actions）」，菜单项和按钮共用一个回调，还能绑 `Ctrl+S` 这种被输入框吃掉的快捷键。

> 想系统学控件与 `.cgui` 格式，看仓库根 `README.md` 与 `docs/CGUI-FORMAT.md`；
> 入门建议从 `examples\lesson01_basics` 六课教学示例开始。
