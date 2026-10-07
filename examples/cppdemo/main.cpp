/**
 * @file    main.cpp
 * @brief   C++ 例程 · 数据放 STL 容器、状态放对象，界面只当镜子
 * @details MinGW-w64 自带 g++，所以 CStackGUI 的工程不只能写 C：把源文件叫 `.cpp`，
 *          构建脚本就自动走 `g++ -std=c++17` 编译、用 g++ 链接，`.cgui` 界面文件、
 *          绑定表、`--selftest` 自检跟 C 工程一字不差。
 *
 *          这一份示例演示四件"C 里要手写、C++ 里白拿"的事：
 *            - **数据进容器**：词条存在 `std::vector<std::string>`，不再自己记数组长度；
 *            - **状态进对象**：`WordBook`（只认 STL）和 `Shell`（只认界面）各管一摊，
 *              业务代码里看不到一个 `cg_xxx` 调用；
 *            - **算法用现成的**：`std::sort` / `std::reverse` / `std::max_element` /
 *              `std::accumulate` 直接拿来用，少写循环就少写错；
 *            - **回调转一手**：绑定表要的是 C 函数指针（`cg_event_fn` 声明在
 *              `cstackgui.h` 的 `extern "C"` 块里），成员函数不能直接放进去 ——
 *              这里一律走"extern "C" 自由函数 → 单例对象的成员方法"这一层。
 *
 *          界面在 `cppdemo.cgui`：上面一行输入框 + 四个按钮，左边一个列表，
 *          右边统计与开关，底部状态栏回话。
 *
 *          编译：在项目根跑 `build-app.bat cppdemo`
 *          自检：`examples\cppdemo\cppdemo.exe --selftest examples\cppdemo\cppdemo.cgui`
 */
#include "cstackgui.h"          /* 头里自带 extern "C"，C++ 直接 include 即可 */

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

/* ============================================================ 业务层（不含界面） */

/**
 * @brief 一本词条簿：只认 STL，一个 cg_xxx 都不认识
 * @details 业务写成这样有个直接好处 —— 不启界面也能测（命令行里 new 一个出来跑），
 *          换界面（改 .cgui、换控件）不用碰这里一个字。
 */
class WordBook {
public:
    /**
     * @brief 追加一个词条
     * @param word 词条文字；空串和已存在的都不收
     * @return bool true 收下了、false 被拒（空或重复）
     */
    bool add(std::string word)
    {
        if (word.empty()) return false;
        if (std::find(words_.begin(), words_.end(), word) != words_.end()) return false;
        if (upper_) to_upper(word);
        words_.push_back(std::move(word));
        return true;
    }
    /** @brief 按字典序排序（std::sort，默认就是 std::string 的 <） */
    void sort() { std::sort(words_.begin(), words_.end()); }
    /** @brief 把顺序整个倒过来（std::reverse） */
    void reverse() { std::reverse(words_.begin(), words_.end()); }
    /** @brief 清空 */
    void clear() { words_.clear(); }

    /**
     * @brief 切"显示成大写"：只影响**之后**的显示，不改动存下来的原词
     * @param on true 转大写
     * @return void 无返回值
     */
    void set_upper(bool on) { upper_ = on; }

    /** @brief 词条数 */
    std::size_t size() const { return words_.size(); }
    /** @brief 取第 index 条；越界返回 std::nullopt（比"返回空串自己猜"清楚） */
    std::optional<std::string> at(int index) const
    {
        if (index < 0 || static_cast<std::size_t>(index) >= words_.size()) return std::nullopt;
        return words_[static_cast<std::size_t>(index)];
    }
    /** @brief 给外面渲染用的只读视图 */
    const std::vector<std::string> &words() const { return words_; }

    /** @brief 一行统计：条数 / 总字数 / 最长词条的字数 */
    std::string summary() const
    {
        std::ostringstream os;
        os << "共 " << words_.size() << " 条 / 累计 " << total_chars()
           << " 字 / 最长 " << longest() << " 字";
        return os.str();
    }

private:
    /** @brief 总字数（std::accumulate：一行顶一个 for） */
    std::size_t total_chars() const
    {
        return std::accumulate(words_.begin(), words_.end(), std::size_t{0},
                               [](std::size_t n, const std::string &w) { return n + w.size(); });
    }
    /** @brief 最长词条的字数（std::max_element + 比较"长度"的 lambda） */
    std::size_t longest() const
    {
        if (words_.empty()) return 0;
        return std::max_element(words_.begin(), words_.end(),
                                [](const std::string &a, const std::string &b) {
                                    return a.size() < b.size();
                                })->size();
    }
    /**
     * @brief 就地转大写
     * @param word 被改的字符串
     * @return void 无返回值
     * @note `std::toupper` 的参数必须转成 `unsigned char`：中文是 UTF-8 多字节，
     *       直接把值为负的 char 喂进去是未定义行为（常见的"一转大写就崩"）。
     *       转大写在 UTF-8 中文上本来也没意义，所以字节值不变、原样留下。
     */
    static void to_upper(std::string &word)
    {
        std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
    }

    std::vector<std::string> words_;
    bool                     upper_ = false;
};

/* ============================================================ 界面层 */

/**
 * @brief 把"词条簿"和"窗口上的控件"接起来的那一层
 * @details 规矩只有一条：**所有对界面的写都发生在 render() 里**。
 *          按钮回调只改数据、再调一次 render()，界面就不会出现"改了一半"的中间态。
 */
class Shell {
public:
    /** @brief 记住窗口（ready 时才拿得到 cg_app_t*） */
    void attach(cg_app_t *app) { app_ = app; }

    /**
     * @brief 加入输入框里的词条
     * @param text 输入框文字
     * @return void 无返回值
     */
    void add(const char *text)
    {
        const std::string word = text ? text : "";
        if (word.empty()) {
            say("输入框是空的：先打一个词再点「加入」");
            return;
        }
        if (!book_.add(word)) {
            say("「%s」已经在列表里了，没重复加", word.c_str());
            return;
        }
        picked_ = static_cast<int>(book_.size()) - 1;
        render();
        say("加入了「%s」，现在 %zu 条", word.c_str(), book_.size());
    }
    /** @brief 排序 */
    void sort_words()
    {
        if (book_.size() < 2) { say("不足两条，排序看不出效果"); return; }
        book_.sort();
        picked_ = -1;
        render();
        say("排好序了（std::sort，按字典序）");
    }
    /** @brief 反序 */
    void reverse_words()
    {
        if (book_.size() < 2) { say("不足两条，反转看不出效果"); return; }
        book_.reverse();
        picked_ = -1;
        render();
        say("倒过来了（std::reverse，就地反转）");
    }
    /** @brief 清空 */
    void clear_words()
    {
        if (book_.size() == 0) { say("列表已经是空的"); return; }
        const std::size_t n = book_.size();
        book_.clear();
        picked_ = -1;
        render();
        say("清掉了 %zu 条", n);
    }
    /** @brief 切"转成大写"开关：切完整体重画 */
    void set_upper(bool on)
    {
        book_.set_upper(on);
        render();
        say(on ? "切成大写显示（std::transform + std::toupper）" : "还原成原样");
    }
    /** @brief 列表里选中了第 index 条 */
    void pick(int index)
    {
        picked_ = index;
        render();
        const std::string w = book_.at(index).value_or("（这条不存在）");
        say("选中第 %d 条：%s", index, w.c_str());
    }
    /** @brief 往状态栏写一段话（C++ 的变参模板，替掉 C 的 va_list 那套） */
    template <typename... Args>
    void say(const char *fmt, Args &&...args)
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), fmt, std::forward<Args>(args)...);
        cg_status_set(cg_find(app_, "sb_main"), 0, buf);
    }

    /** @brief 进程内唯一一个 Shell（C++ 的"函数内静态"，线程安全且不用 new） */
    static Shell &instance()
    {
        static Shell s;
        return s;
    }

    /** @brief 界面就绪：放初始数据，然后整体重画一次 */
    void seed()
    {
        for (const char *w : {"C 语言", "Win32", "GDI+", "串口", "Modbus"}) book_.add(w);
        render();
        say("C++ 例程就绪：数据在 std::vector 里，界面只是它的镜子。");
    }

private:
    /**
     * @brief 把数据整份重画到界面上（列表 / 统计 / 选中项 / 选中态）
     * @return void 无返回值
     * @note 先 clear 再逐条 add：列表控件内部自己存一份字符串表，
     *       所以"改一条"和"全部重画"代价差不多，整体重画最不容易写错。
     */
    void render()
    {
        cg_widget_t *lst = cg_find(app_, "lst_words");
        cg_widget_list_clear(lst);
        for (const auto &w : book_.words()) cg_widget_list_add(lst, w.c_str());
        if (picked_ >= 0) cg_widget_set_value(lst, picked_);
        cg_widget_list_scroll_bottom(lst);

        cg_widget_set_text(cg_find(app_, "lab_count"), book_.summary().c_str());
        const std::string picked_text = "选中：" + book_.at(picked_).value_or("—");
        cg_widget_set_text(cg_find(app_, "lab_pick"), picked_text.c_str());
    }

    cg_app_t *app_    = nullptr;
    WordBook  book_;
    int       picked_ = -1;
};

/* ============================================================ 回调
 * 绑定表要的是 cg_event_fn —— 它在 cstackgui.h 的 extern "C" 块里声明，
 * 所以这几个函数也按 C 链接给出（成员函数、lambda 都放不进绑定表）。
 * 函数体里照样能写 C++：这里只是"转发到 Shell::instance()"。
 */
extern "C" {

/**
 * @brief 窗口就绪：接上窗口、放初始数据
 * @param self 窗口伪控件（id 是 CG_WINDOW_ID）
 * @param ev ready
 * @param user 未使用
 * @return void 无返回值
 */
static void on_app_ready(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    Shell &s = Shell::instance();
    s.attach(cg_app_of(self));
    s.seed();
}

/**
 * @brief 输入框里按了回车：和点「加入」走同一条业务路径
 * @param self 输入框自己（submit 带的是它，不用再 cg_find）
 * @param ev submit
 * @param user 未使用
 * @return void 无返回值
 */
static void on_edt_word_submit(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    Shell::instance().add(cg_widget_text(self));
}

/**
 * @brief 「加入」被点：把输入框里的词收进 std::vector
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_add_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    cg_app_t *app = cg_app_of(self);
    Shell::instance().add(cg_widget_text(cg_find(app, "edt_word")));
}

/**
 * @brief 「排序」被点
 * @param self 未使用（排序不关心是谁点的）
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_sort_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self;
    (void)ev;
    (void)user;
    Shell::instance().sort_words();
}

/**
 * @brief 「反转」被点
 * @param self 未使用
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_reverse_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self;
    (void)ev;
    (void)user;
    Shell::instance().reverse_words();
}

/**
 * @brief 「清空」被点
 * @param self 未使用
 * @param ev click
 * @param user 未使用
 * @return void 无返回值
 */
static void on_btn_clear_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)self;
    (void)ev;
    (void)user;
    Shell::instance().clear_words();
}

/**
 * @brief 列表里选中了另一条
 * @param self 列表控件
 * @param ev select
 * @param user 未使用
 * @return void 无返回值
 * @note 列表"选中第几条"读 value（不是 text，也不是 checked）。
 */
static void on_lst_words_select(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    Shell::instance().pick(cg_widget_value(self));
}

/**
 * @brief 「显示成大写」被切换
 * @param self 开关控件
 * @param ev change
 * @param user 未使用
 * @return void 无返回值
 */
static void on_sw_upper_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    Shell::instance().set_upper(cg_widget_checked(self) != 0);
}

} /* extern "C" */

/** `.cgui` 里出现的函数名 -> 这里的函数；漏一个 `--selftest` 会报"未登记" */
static const cg_binding_t k_bindings[] = {
    CG_BIND("on_app_ready",        on_app_ready),
    CG_BIND("on_edt_word_submit",  on_edt_word_submit),
    CG_BIND("on_btn_add_click",    on_btn_add_click),
    CG_BIND("on_btn_sort_click",   on_btn_sort_click),
    CG_BIND("on_btn_reverse_click", on_btn_reverse_click),
    CG_BIND("on_btn_clear_click",  on_btn_clear_click),
    CG_BIND("on_lst_words_select", on_lst_words_select),
    CG_BIND("on_sw_upper_change",  on_sw_upper_change),
    CG_BIND_END
};

} /* namespace */

/**
 * @brief 程序入口：按命令行参数走自检或正常运行
 * @param argc 参数个数
 * @param argv 参数数组（argv[1] = .cgui 路径，或 --selftest）
 * @return int 进程退出码（0 正常）
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc{};        /* C++ 的 `{}` 一次性把整个结构清零 */
    desc.bindings        = k_bindings;
    desc.auto_locate_cgui = 1;

    const bool selftest = argc > 1 && std::string(argv[1]) == "--selftest";
    desc.cgui_path = (argc > (selftest ? 2 : 1)) ? argv[selftest ? 2 : 1] : "cppdemo.cgui";
    return selftest ? cg_app_selftest(&desc) : cg_app_run(&desc);
}
