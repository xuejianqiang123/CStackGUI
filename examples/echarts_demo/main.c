/**
 * @file    main.c
 * @brief   应用入口（由 CStackGUI 画布生成）
 * @details 编译：本目录要放在 CStackGUI 项目的 apps\ 下，然后在项目根目录跑
 *          build-app.bat echarts_demo
 *          产物：apps\echarts_demo\echarts_demo.exe —— 单个 exe，无运行库依赖，双击就能跑。
 */
#include "echarts_demo_ui_events.h"

#include <stdio.h>
#include <string.h>

static const cg_binding_t k_bindings[] = {
    ECHARTS_DEMO_BINDINGS
    CG_BIND_END
};

/**
 * @brief 程序入口：组装启动参数（绑定表、自动找 .cgui、--selftest 分支）
 * @param argc 参数个数
 * @param argv 命令行参数（可带 .cgui 路径或 --selftest）
 * @return int 进程退出码（0 正常）
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.dblclick_suppress_click = 0;   /* 1 = 双击时不再回调 click */
    desc.auto_locate_cgui = 1;          /* 双击 exe 也能跑：自动去 exe 附近找 .cgui */

    /* --selftest：不开窗口，只校验 .cgui 能否解析、事件名是否都登记过 */
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "echarts_demo.cgui";
        return cg_app_selftest(&desc);
    }

    desc.cgui_path = (argc > 1) ? argv[1] : "echarts_demo.cgui";
    return cg_app_run(&desc);
}
