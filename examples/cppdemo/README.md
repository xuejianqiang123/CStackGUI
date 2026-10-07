# C++ 例程 · 数据与界面分开

> 目录 `examples\cppdemo\`　编译 `build-app.bat cppdemo`　运行 `examples\cppdemo\cppdemo.exe`　自检 `cppdemo.exe --selftest examples\cppdemo\cppdemo.cgui` → 控件 15 个 / 回调 7 次 /（未登记·事件不支持·绑定出错·动作出错·样式引用失效 全 0）→ `selftest OK`

## 这一份讲什么

MinGW-w64 自带 g++，所以工程不只能写 C：**源文件叫 `.cpp`，构建脚本就自动换 `g++ -std=c++17` 编译、用 g++ 链接**，
`.cgui` 界面文件、绑定表、`--selftest` 自检和 C 工程一字不差。

示例本身是个"词条簿"：上面输入一个词 → 加入 / 排序 / 反转 / 清空，左边列表显示，右边统计与开关，底部状态栏回话。
它演示四件"C 里要手写、C++ 里白拿"的事：

| 想看的点 | 落在哪 |
| --- | --- |
| 数据进容器 | `WordBook::words_` 是 `std::vector<std::string>`，不用自己记长度 |
| 状态进对象 | `WordBook`（只认 STL）与 `Shell`（只认界面）各管一摊，业务代码里没有一个 `cg_xxx` |
| 算法用现成的 | `std::sort` / `std::reverse` / `std::max_element` / `std::accumulate` / `std::find` |
| 回调转一手 | 绑定表要 C 函数指针，所以是 `extern "C"` 自由函数 → `Shell::instance()` 的成员方法 |
| 空/越界不再是"约定" | `std::optional<std::string>`：没选中就是 `nullopt`，不靠空串猜 |

## 怎么跑

1. 在项目根编译：`build-app.bat cppdemo`，双击 `examples\cppdemo\cppdemo.exe`。
2. 输入框里打一个词 —— 点「加入」或直接按回车（`submit` 事件）都能进去；重复的词不收。
3. 点「排序」/「反转」看 `std::sort` / `std::reverse` 的效果；点列表里的某一行看右下角"选中"。
4. 拨「显示成大写」开关：`std::transform` + `std::toupper`，中文（UTF-8 多字节）原样留下。

## 关键控件一览（15 个控件 / 7 个回调）

| 控件 id | 类型 | 看什么 |
| --- | --- | --- |
| `edt_word` | `edit` | 单行输入；回车发 `submit`，和按钮走同一条业务路径 |
| `btn_add` / `btn_sort` / `btn_reverse` / `btn_clear` | `button` | 四个按钮各改一次 `std::vector`，再整体重画 |
| `lst_words` | `listbox` | 条目由 C++ 侧整份重画；选中发 `select`，选中下标读 `value` |
| `lab_count` / `lab_pick` | `label` | 统计（`std::accumulate` / `std::max_element`）与选中项（`std::optional`） |
| `sw_upper` | `switch` | 状态读 `checked`；切完整体重画 |
| `sb_main` | `statusbar` | 所有回话都往它写（C++ 变参模板 `say()`，替掉 C 的 `va_list`） |

## 重点看哪几处

- **业务层不认识界面**：`WordBook` 里一个 `cg_xxx` 都没有 —— 换界面、换控件，这一层不用动。
- **对界面的写只在 `render()` 里**：回调只改数据再调 `render()`，界面不会停在"改了一半"的中间态。
- **回调必须是 C 函数指针**：`cg_event_fn` 声明在 `cstackgui.h` 的 `extern "C"` 块里，
  成员函数和 lambda 都放不进 `CG_BIND`；所以这里多一层转发。
- **`std::toupper` 要先转 `unsigned char`**：中文是 UTF-8 多字节，负的 `char` 直接喂进去是未定义行为
  （"一转大写就崩"多半是这个）。
- **链接加了 `-static`**：把 libstdc++ / libgcc / libwinpthread 静态收进 exe。
  不加的话，没装 MinGW 的机器上要么报 0xC0000135（找不到 `libstdc++-6.dll`），
  要么更糟 —— 从 PATH 里捡到一个位数不对的同名 dll，报 0xC000007B。

## 想自己写 C++ 工程

1. 工程目录里放 `main.cpp`（或 `.cc` / `.cxx`），`include "cstackgui.h"` —— 头文件自带 `extern "C"`，不用自己包。
2. 回调按 `extern "C"` 给（名字进不了绑定表就 `--selftest` 会报"未登记"），函数体里照样写 C++。
3. `.cgui` 与 C 工程完全一样：画布里照常拖控件、填回调名，`build-app.bat <名字>` 照常编。
4. 混编也行：同一目录里 `.c` 与 `.cpp` 各编各的（C 走 `gcc -std=c11`，C++ 走 `g++ -std=c++17`），
   最后一起链接。
