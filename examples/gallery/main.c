/**
 * @file    main.c
 * @brief   控件陈列室（三合一）入口：拼绑定表 + 依次初始化三页
 * @details 一个示例讲完所有东西，窗口顶部的大标签页分三页：
 *   页 0「基础闭环」：分组框 / 输入 / 勾选 / 滑块 / 进度条 / 列表框 / 按钮（单击+双击），
 *                     演示业务状态结构、cg_set_timer、控件间联动（modules\gal_p0.c）。
 *   页 1「控件陈列」：菜单栏 / 工具栏 / 菜单按钮 / 开关 / 微调 / 日期时间 / 树 / 表格 /
 *                     富文本 / 标签页 / 圆环 / 月历 / 链接 / 图标按钮 / 图片 / 状态栏，
 *                     演示 close 拦截、动态加节点加行（modules\gal_p1.c）。
 *   页 2「新特性」  ：时钟 / 悬停提示 / 通用对话框 / 画板 / 颜色选择 / 表格勾选多选 /
 *                     行数值 / 评分 / 分页 / 徽章 / 标签片 / 卡片 / 外形框 / 网页浏览 /
 *                     焦点事件 / 图标列表（modules\gal_p2.c）。
 *
 *          模块划分（每个模块自带绑定表 gal_*_bindings，main 只负责拼成一张总表）：
 *            modules\gal_common.c  三页共用业务状态 + 日志/状态栏回执 + 窗口/对话框事件
 *            modules\gal_p0.c      页 0 基础闭环
 *            modules\gal_p1.c      页 1 控件陈列
 *            modules\gal_p2.c      页 2 新特性
 *
 *          界面在 gallery.cgui 里；本文件只是入口。
 *          编译：在项目根跑 build-app.bat gallery
 */
#include "cstackgui.h"

#include <stdio.h>
#include <string.h>

#include "modules/gal_common.h" /**< 三页共用业务状态 + 窗口/对话框事件 */
#include "modules/gal_p0.h"     /**< 页 0 基础闭环 */
#include "modules/gal_p1.h"     /**< 页 1 控件陈列 */
#include "modules/gal_p2.h"     /**< 页 2 新特性 */

/** 各模块的绑定表规模上限：53 条，留足余量给新增模块 */
#define GAL_BIND_MAX 96

/** 运行时要一张**连续**的绑定表（表尾 CG_BIND_END），这里在启动时把各模块的拼起来 */
static cg_binding_t g_bindings[GAL_BIND_MAX];

/**
 * @brief 把一张以 CG_BIND_END 结尾的模块绑定表追加进总表
 * @param src 模块绑定表（不可为 NULL）
 * @param n 当前总表已用条数
 * @return int 追加后的新条数（不会越过 GAL_BIND_MAX；超了就停下，自检会报出未登记回调）
 */
static int append_bindings(const cg_binding_t *src, int n)
{
    int i;

    for (i = 0; src[i].name && n < GAL_BIND_MAX - 1; i++) g_bindings[n++] = src[i];
    g_bindings[n].name = NULL;
    g_bindings[n].fn   = NULL;
    return n;
}

/**
 * @brief 界面就绪钩子：依次调三页的初始化
 * @param app 所属应用
 * @param user 就绪钩子的业务数据，作为三页共用的业务状态
 * @return void 无返回值
 */
static void on_ready(cg_app_t *app, void *user)
{
    gal_p0_ready(app, (biz_t *)user);
    gal_p1_ready(app, (biz_t *)user);
    gal_p2_ready(app, user);
}

/**
 * @brief 程序入口：组装应用描述，按命令行参数走自检或正常运行
 * @param argc 命令行参数个数，大于 1 时才会解析开关
 * @param argv 命令行参数数组，argv[1] 可为 --selftest、--trace 或 .cgui 路径
 * @return int 进程退出码，0 表示正常结束
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;
    int           n = 0;

    n = append_bindings(gal_common_bindings, n);
    n = append_bindings(gal_p0_bindings, n);
    n = append_bindings(gal_p1_bindings, n);
    n = append_bindings(gal_p2_bindings, n);

    memset(&desc, 0, sizeof(desc));
    desc.bindings = g_bindings;
    desc.dblclick_suppress_click = 0;   /* 双击时先给 click 再给 dblclick */
    desc.auto_locate_cgui = 1;          /* 双击 exe 也能跑：自动去 exe 附近找 .cgui */
    desc.on_ready = on_ready;
    desc.ready_user = gal_biz();

    /* --selftest 走命令行自检：解析 + 打印控件树 + 合成事件，不开窗口 */
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "gallery.cgui";
        return cg_app_selftest(&desc);
    }
    if (argc > 1 && strcmp(argv[1], "--trace") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "gallery.cgui";
        desc.trace_doc = 1;
    } else {
        desc.cgui_path = (argc > 1) ? argv[1] : "gallery.cgui";
    }

    printf("CStackGUI %s -- 界面文件: %s\n", CG_VERSION_STRING, desc.cgui_path);
    return cg_app_run(&desc);
}
