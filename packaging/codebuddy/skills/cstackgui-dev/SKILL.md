---
name: cstackgui-dev
description: 在 CStackGUI 发布包/项目里写界面业务逻辑（C 回调）。触发条件：用户要写或改 <名字>_ui_events.c、main.c，问 cg_ 开头的 API 怎么用（找控件、改文字、读写状态、定时器、菜单 id）时自动触发。
---

# 写 CStackGUI 业务代码

**任务**：$ARGUMENTS

## 核心原则

1. 业务代码**只 include `"cstackgui.h"`**，绝不 include 运行时内部头或任何 Windows 头。
2. 回调签名固定 `void f(cg_widget_t *self, cg_event_t ev, void *user)`，不要改；
   业务参数用 `cg_widget_set_user()` 挂在控件上。
3. **改完必须验证**：`build-app.bat <名字>` 编译 + `<exe> --selftest` 返回 0 才算闭环。

## 流程

### 1. 先看接口

- 完整 API 和注释在 `runtime\include\cstackgui.h`（注释即文档），写之前现查，别凭记忆编 API。
- 常用速查：

```c
cg_app_t *app = cg_app_of(self);              /* 从控件拿应用 */
cg_widget_t *w = cg_find(app, "控件id");       /* 按 id 找别的控件 */
cg_widget_set_text(w, "文字");                 /* 改文字 */
const char *s = cg_widget_text(w);            /* 读文字 */
int on = cg_widget_checked(w);                /* 读勾选（checkbox/radio/switch） */
double v = cg_widget_value(w);                /* 读值（slider/progress/spinner/tabs页号） */
cg_widget_set_value(w, 50);                   /* 写值（进度条就是被程序写的） */
cg_widget_list_addf(list, "日志 %d", n);       /* 往 listbox/statusbar 追加条目 */
cg_set_timer(app, 1000, on_tick, user);       /* 延迟/周期做事；on_tick 签名 void on_tick(void *user) */
cg_menu_last_id();                            /* 菜单 click 里取是哪一项（.cgui 里写的菜单 id） */
cg_quit(app);                                 /* 关窗回调里决定到底关不关 */
cg_widget_id(self), cg_event_name(ev)         /* 调试打印用 */
```

### 2. 写在哪

- 只改 `apps\<名字>\<名字>_ui_events.c` —— 画布重新导出**不会覆盖**它。
- 事件绑定在 `.cgui` 里（画布「事件」页签填的函数名），代码里只实现函数，不用自己注册。
- 函数名必须和 `.cgui` 里写的一致：**写错了编译不报错**，运行时会警告。

### 3. 编译与验证

```bat
build-app.bat <名字>                :: 需要 MinGW-w64 的 gcc
apps\<名字>\<名字>.exe --selftest   :: 不开窗：解析界面、真调一遍每个回调、查未登记名字
apps\<名字>\<名字>.exe              :: 正式运行（printf 输出在控制台版可见）
```

`--selftest` 退出码非 0 = 有回调名没登记 / 绑了控件不支持的事件，按输出修。

## 事件（按控件特性分配，不全都有）

click/dblclick（按钮类）· change（输入/勾选/滑块拖动）· submit（单行回车）·
select（列表/下拉/标签页/树/表格/月历）· release（滑块松手）· expand/collapse（树）。
窗口 `close` 事件在 `.cgui` 的 `app.events.close` 里绑，回调里用 `cg_quit()` 决定关不关。
每个控件支持哪些事件见 README 第四节；绑了不支持的事件运行时会拒绝。

## 陷阱

- `cg_find` 返回找不到的控件会打警告，别假定一定找得到。
- 定时器回调里不要做耗时操作（阻塞界面消息循环）。
- `.cgui` 是**运行时读**的：改界面布局不用重编 exe，改了 C 代码才要。
