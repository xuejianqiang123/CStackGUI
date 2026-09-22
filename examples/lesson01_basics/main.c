/**
 * @file    main.c
 * @brief   入门 01：编辑框 + 按钮 —— 最小的一次「读输入、改界面」，外加按钮全功能演示
 * @details 这一个例子只讲三件事，也是所有 CStackGUI 程序的共同骨架：
 *
 *            1. `.cgui` 里给控件起 id（这里是 edt_name / btn_hello / lab_result）；
 *            2. `.cgui` 的事件格里填 **C 函数名**，C 侧用 CG_BIND 把名字接到函数上；
 *            3. 回调里用 cg_find(app, "id") 找到别的控件，读它 / 改它。
 *
 *          顺带演示四个新手最常问的点：
 *            - 两个按钮可以共用一个回调，用 cg_widget_id(self) 分辨是谁被点的；
 *            - 程序改控件（set_text / set_enabled）**不会**触发 change 回调，
 *              所以界面状态要自己同步（见 on_button_click 的「清空」分支）；
 *            - cg_str_format 返回的字符串是堆内存，用完必须 cg_free；
 *            - 想弹消息框的地方先问一句 cg_app_has_window()，否则 --selftest 会被弹窗卡住。
 *
 *          「按钮全功能」按钮演示按钮的全部能力：
 *            - 背景图：`.cgui` 里用 `src: "assets/icon.png"`（png/jpg/bmp/gif 拉伸铺满；
 *              别用自带文字的图，字会叠在按钮文字上）；
 *            - 全部 13 种事件（click/dblclick/mousedown/mouseup/rdown/rup/mousemove/
 *              gotfocus/lostfocus/keydown/keyup/char/wheel）绑到同一个 `on_full_event`，
 *              靠 cg_event_name() / cg_event_x() / cg_event_key() / cg_event_delta() 分流。
 *
 *          输入框（edit）的全功能也在这个界面里，分四块：
 *            - `edt_events`：edit 支持的 15 种事件（change/submit/spin + 鼠标/键盘全套）
 *              全绑到 `on_edit_event`，和按钮那套是同一个手法；
 *            - 外观与限制：`multiline`（多行）/`scroll`（滚动条）/`maxlen`（最大长度）/
 *              `conv`（大小写转换）/`border`（六种边框）/`style.align`（对齐方式）；
 *            - 输入方式：`input` 档位决定按键过滤与失焦校验（`edt_byte` 失焦时会看到
 *              0~255 之外的数被夹回、非数字被清空）；
 *            - 调节器：`spin`（右侧上下箭头）—— 开了它就是「数值框」：值是真相、显示文本
 *              由值派生，cg_widget_value()/set_value() 与双向绑定都按值走。自动方式点箭头
 *              按 ±1 改值并夹到 min/max，手动方式只报 ±1、步长由业务自己写（见 on_spin_manual）。
 *
 * 界面在 lesson01_edit_button.cgui 里；本文件只有行为。
 * 逐项说明见同目录的「按钮与输入框全功能说明.md」。
 * 编译：在项目根跑 build-app.bat lesson01_edit_button
 */
#include "cstackgui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ 业务状态
 * 例子只有一个计数器，但它放在"文件作用域的静态变量"里 —— 这是 C 里最省事的做法：
 * 回调随时能拿到。状态多的项目就定义一个结构体（见 examples\notepad）。
 */
static int s_hello_count;

/** @brief 数有几个字（UTF-8 编码下续字节不算一个字，这样中文按"字"算而不是按字节） */
static int count_chars(const char *s)
{
    int n = 0;

    if (!s) return 0;
    for (; *s; s++) {
        if (((unsigned char)*s & 0xC0) != 0x80) n++;   /* 10xxxxxx 是续字节 */
    }
    return n;
}

/** @brief 把状态栏第一段改成一句话（控件找不到就安静跳过，别崩） */
static void set_status(cg_app_t *app, const char *text)
{
    cg_status_set(cg_find(app, "sb_main"), 0, text);
}

/**
 * @brief 打招呼：按钮 click 和输入框 submit 共用这一份逻辑
 * @param app 所属应用
 */
static void say_hello(cg_app_t *app)
{
    cg_widget_t *edt = cg_find(app, "edt_name");
    cg_widget_t *lab = cg_find(app, "lab_result");
    const char  *name = cg_widget_text(edt);   /* 控件当前的文字 */

    if (!name || !*name) {
        cg_widget_set_text(lab, "先在上面输入一个名字吧。");
        return;
    }

    s_hello_count++;
    {
        /* cg_str_format 按 printf 规则拼串，返回堆内存 —— 传出去之后要还回去 */
        char *msg = cg_str_format("你好，%s！\n\n这是第 %d 次打招呼。", name, s_hello_count);
        cg_widget_set_text(lab, msg);
        cg_free(msg);
    }
    cg_log(CG_LOG_INFO, "打招呼：%s（第 %d 次）", name, s_hello_count);
    cg_status_setf(cg_find(app, "sb_main"), 0, "已经打了 %d 次招呼", s_hello_count);
}

/**
 * @brief 输入框内容变了（每敲一个字都会来一次）
 * @param self 输入框
 * @param ev change
 * @param user 未使用
 */
static void on_input_change(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);          /* 回调里第一步：从控件拿到应用 */
    int       n = count_chars(cg_widget_text(self));

    (void)ev;
    (void)user;

    /* 「能不能点」由业务决定：没输入名字时按钮直接禁用，用户一眼就知道要先填东西 */
    cg_widget_set_enabled(cg_find(app, "btn_hello"), n > 0);
    if (n > 0) cg_status_setf(cg_find(app, "sb_main"), 0, "已输入 %d 个字", n);
    else       set_status(app, "等待输入");
}

/**
 * @brief 两个按钮共用的点击回调：用 cg_widget_id() 分辨是谁
 * @param self 被点的按钮
 * @param ev click
 * @param user 未使用
 */
static void on_button_click(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *id = cg_widget_id(self);

    (void)ev;
    (void)user;

    if (cg_str_equal(id, "btn_hello")) {
        say_hello(app);
    } else if (cg_str_equal(id, "btn_clear")) {
        cg_widget_set_text(cg_find(app, "edt_name"), "");
        cg_widget_set_text(cg_find(app, "lab_result"), "（结果显示在这里）");
        /* 注意：上面那句 set_text **不会**触发 on_input_change（运行时故意屏蔽了
         * "程序改控件"产生的通知），所以这里自己把按钮再禁用一次，不然会出现
         * "框空了按钮还是亮的"。 */
        cg_widget_set_enabled(cg_find(app, "btn_hello"), 0);
        set_status(app, "已清空");
        cg_log(CG_LOG_INFO, "清空输入");
    }
}

/**
 * @brief 在输入框里按了回车（submit 事件）
 * @param self 输入框
 * @param ev submit
 * @param user 未使用
 */
static void on_hello_submit(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    say_hello(cg_app_of(self));
}

/**
 * @brief 全功能按钮的演示回调：把按钮支持的全部事件都接同一个函数，再按事件类型分流。
 * @details 这是「按钮全功能」的活教材——一个函数绑 13 种事件，靠 cg_event_name() 拿到
 *          事件名、cg_event_x/y/key/delta 拿到这次事件的参数。鼠标移动/焦点只更新状态栏
 *          （太频繁，不打到结果区），其余事件打到结果区方便对照。
 * @param self 「全功能」按钮
 * @param ev 事件枚举（CG_EVENT_*）
 * @param user 未使用
 */
static void on_full_event(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *lab = cg_find(app, "lab_result");
    cg_widget_t *sb  = cg_find(app, "sb_main");
    int x = cg_event_x(self), y = cg_event_y(self);
    int k = cg_event_key(self), d = cg_event_delta(self);

    (void)user;

    /* 高频事件只动状态栏，不打扰结果区 */
    switch (ev) {
    case CG_EVENT_MOUSEMOVE:
        if (sb) cg_status_setf(sb, 0, "鼠标在「全功能」按钮上：坐标 (%d, %d)", x, y);
        return;
    case CG_EVENT_GOTFOCUS:
        if (sb) set_status(app, "「全功能」按钮获得焦点（现在能收键盘事件）");
        return;
    case CG_EVENT_LOSTFOCUS:
        if (sb) set_status(app, "「全功能」按钮失去焦点");
        return;
    default:
        break;
    }

    /* 其余事件都打到结果区，方便对照 .cgui 里写的事件名。
     * cg_widget_set_text 内部会自己拷贝字符串，所以直接传栈上的 buf 即可。 */
    {
        const char *name = cg_event_name(ev);
        char        buf[160];

        switch (ev) {
        case CG_EVENT_CLICK:     snprintf(buf, sizeof(buf), "事件：click（左键单击）"); break;
        case CG_EVENT_DBLCLICK:  snprintf(buf, sizeof(buf), "事件：dblclick（双击）"); break;
        case CG_EVENT_MOUSEDOWN: snprintf(buf, sizeof(buf), "事件：mousedown（左键按下）@(%d,%d)", x, y); break;
        case CG_EVENT_MOUSEUP:   snprintf(buf, sizeof(buf), "事件：mouseup（左键松开）@(%d,%d)", x, y); break;
        case CG_EVENT_RDOWN:     snprintf(buf, sizeof(buf), "事件：rdown（右键按下）@(%d,%d)", x, y); break;
        case CG_EVENT_RUP:       snprintf(buf, sizeof(buf), "事件：rup（右键松开）@(%d,%d)", x, y); break;
        case CG_EVENT_KEYDOWN:   snprintf(buf, sizeof(buf), "事件：keydown（虚拟键码 %d）", k); break;
        case CG_EVENT_KEYUP:     snprintf(buf, sizeof(buf), "事件：keyup（虚拟键码 %d）", k); break;
        case CG_EVENT_CHAR:      snprintf(buf, sizeof(buf), "事件：char（字符 '%c'，码 %d）", k, k); break;
        case CG_EVENT_WHEEL:     snprintf(buf, sizeof(buf), "事件：wheel（滚动量 %d，正=上）", d); break;
        default:                 snprintf(buf, sizeof(buf), "事件：%s", name); break;
        }
        if (lab) cg_widget_set_text(lab, buf);
        if (sb) cg_status_setf(sb, 0, "最近事件：%s", name);
    }
}

/**
 * @brief 输入框全功能演示：edit 支持的 15 种事件绑到同一个函数
 * @details 手法和按钮的 on_full_event 完全一样 —— 一个函数接一堆事件，用 cg_event_name()
 *          看是谁、用 cg_event_x/y/key/delta 看参数。区别在于输入框的 15 种事件里多了
 *          change（改字）和 submit（回车），滚动条拖动之类也会发 change。
 * @param self 触发事件的输入框
 * @param ev 事件枚举（CG_EVENT_*）
 * @param user 未使用
 */
static void on_edit_event(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t    *app = cg_app_of(self);
    cg_widget_t *lab = cg_find(app, "lab_result");
    cg_widget_t *sb  = cg_find(app, "sb_main");
    int x = cg_event_x(self), y = cg_event_y(self);
    int k = cg_event_key(self), d = cg_event_delta(self);

    (void)user;

    /* 高频事件只动状态栏：鼠标移动、改字（每敲一个字都要来一次） */
    switch (ev) {
    case CG_EVENT_MOUSEMOVE:
        if (sb) cg_status_setf(sb, 0, "鼠标在「事件示范」框上：(%d, %d)", x, y);
        return;
    case CG_EVENT_CHANGE:
        if (sb) cg_status_setf(sb, 0, "内容被改变：现在 %d 个字符", count_chars(cg_widget_text(self)));
        return;
    case CG_EVENT_GOTFOCUS:
        if (sb) set_status(app, "「事件示范」框获得焦点（现在能收键盘事件）");
        return;
    case CG_EVENT_LOSTFOCUS:
        if (sb) set_status(app, "「事件示范」框失去焦点");
        return;
    default:
        break;
    }

    /* 其余事件打到结果区，方便对照 .cgui 里写的事件名 */
    {
        const char *name = cg_event_name(ev);
        char        buf[200];

        switch (ev) {
        case CG_EVENT_SUBMIT:    snprintf(buf, sizeof(buf), "事件：submit（回车提交，内容 \"%s\"）", cg_widget_text(self)); break;
        case CG_EVENT_MOUSEDOWN: snprintf(buf, sizeof(buf), "事件：mousedown（左键按下）@(%d,%d)", x, y); break;
        case CG_EVENT_MOUSEUP:   snprintf(buf, sizeof(buf), "事件：mouseup（左键松开）@(%d,%d)", x, y); break;
        case CG_EVENT_DBLCLICK:  snprintf(buf, sizeof(buf), "事件：dblclick（双击）"); break;
        case CG_EVENT_RDOWN:     snprintf(buf, sizeof(buf), "事件：rdown（右键按下）@(%d,%d)", x, y); break;
        case CG_EVENT_RUP:       snprintf(buf, sizeof(buf), "事件：rup（右键松开）@(%d,%d)", x, y); break;
        case CG_EVENT_KEYDOWN:   snprintf(buf, sizeof(buf), "事件：keydown（虚拟键码 %d）", k); break;
        case CG_EVENT_KEYUP:     snprintf(buf, sizeof(buf), "事件：keyup（虚拟键码 %d）", k); break;
        /* 中文输入法送进来的是双字节码，用 %c 会打出乱码，所以只在 ASCII 时打字符 */
        case CG_EVENT_CHAR:
            if (k >= 32 && k < 127) snprintf(buf, sizeof(buf), "事件：char（字符 '%c'，码 %d）", k, k);
            else                    snprintf(buf, sizeof(buf), "事件：char（字符码 %d，中文/宽字符）", k);
            break;
        case CG_EVENT_WHEEL:     snprintf(buf, sizeof(buf), "事件：wheel（滚动量 %d，正=上）", d); break;
        default:                 snprintf(buf, sizeof(buf), "事件：%s", name); break;
        }
        if (lab) cg_widget_set_text(lab, buf);
        if (sb) cg_status_setf(sb, 0, "最近事件：%s", name);
    }
}

/**
 * @brief 调节器（自动方式）被点：值已经被运行时改好并夹到 min/max，这里只报出来
 * @param self 数值输入框（edit + spin:1）
 * @param ev spin
 * @param user 未使用
 */
static void on_spin_auto(cg_widget_t *self, cg_event_t ev, void *user)
{
    (void)ev;
    (void)user;
    /* cg_event_delta() 就是这次箭头给的方向（+1 上 / -1 下）；
     * 自动方式下 cg_widget_value()/cg_widget_text() 都已是最新值，业务不用自己算。 */
    cg_status_setf(cg_find(cg_app_of(self), "sb_main"), 0,
                   "调节器（自动）：%d（delta %+d，夹在 0~100）",
                   cg_widget_value(self), cg_event_delta(self));
}

/**
 * @brief 调节器（手动方式）被点：运行时只报 ±1 与方向，值由业务自己算
 * @details 手动方式的用处就在这里 —— 步长由你定。这里做成"步长 5、区间 ±999"，
 *          自动方式做不到（它只会 ±1）。数值框的值用 cg_widget_set_value() 写，
 *          文本会跟着派生；写文本没用（显示文本由值决定）。
 * @param self 数值输入框（edit + spin:2）
 * @param ev spin
 * @param user 未使用
 */
static void on_spin_manual(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t *app = cg_app_of(self);
    int       v   = cg_widget_value(self) + cg_event_delta(self) * 5;   /* 步长 5 */

    (void)ev;
    (void)user;

    /* 区间 ±999 由 .cgui 的 min/max 定，set_value 会自动夹在里面 */
    cg_widget_set_value(self, v);
    cg_status_setf(cg_find(app, "sb_main"), 0, "调节器（手动）：步长 5 -> %d",
                   cg_widget_value(self));
}

/**
 * @brief 字节输入框失焦：运行时已经把越界/非法输入收干净了，这里读最终结果
 * @details `input: 5`（字节）的规矩是：按键只放行数字，失焦时把 0~255 之外的值夹回边界、
 *          把解析不了的文本清空。回调里读到的就是「收干净之后」的值。
 * @param self 字节输入框
 * @param ev lostfocus
 * @param user 未使用
 */
static void on_byte_blur(cg_widget_t *self, cg_event_t ev, void *user)
{
    cg_app_t   *app = cg_app_of(self);
    const char *t   = cg_widget_text(self);
    char        buf[200];

    (void)ev;
    (void)user;

    snprintf(buf, sizeof(buf),
             "字节输入框失焦：最终值 \"%s\"\n（敲 300 会被夹成 255，敲字母会被清空成空）",
             (t && *t) ? t : "（空）");
    cg_widget_set_text(cg_find(app, "lab_result"), buf);
    cg_status_setf(cg_find(app, "sb_main"), 0, "字节输入框：最终值 \"%s\"", (t && *t) ? t : "（空）");
}

/** @brief 界面建好之后调一次：把"初始状态"设成和业务一致 */
static void on_ready(cg_app_t *app, void *user)
{
    (void)user;
    cg_widget_set_enabled(cg_find(app, "btn_hello"), 0);   /* 一开始没输入，按钮先禁用 */
    cg_log(CG_LOG_INFO, "界面就绪，共 %d 个控件", cg_widget_count(app));
}

/* ------------------------------------------------------------ 绑定表
 * 左边是 .cgui 的事件格里写的名字，右边是 C 函数。
 * 名字对不上编译不报错（运行时按名字查表），靠 --selftest 兜底。
 */
static const cg_binding_t k_bindings[] = {
    CG_BIND("on_input_change", on_input_change),
    CG_BIND("on_button_click", on_button_click),
    CG_BIND("on_hello_submit", on_hello_submit),
    CG_BIND("on_full_event", on_full_event),
    /* 输入框全功能那几块 */
    CG_BIND("on_edit_event", on_edit_event),
    CG_BIND("on_spin_auto", on_spin_auto),
    CG_BIND("on_spin_manual", on_spin_manual),
    CG_BIND("on_byte_blur", on_byte_blur),
    CG_BIND_END
};

int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.auto_locate_cgui = 1;   /* 双击 exe 也能跑：自动在 exe 旁边找 .cgui */
    desc.on_ready = on_ready;

    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "lesson01_edit_button.cgui";
        return cg_app_selftest(&desc);   /* 不开窗，解析 + 真调一遍所有回调 */
    }
    desc.cgui_path = (argc > 1) ? argv[1] : "lesson01_edit_button.cgui";
    return cg_app_run(&desc);
}
