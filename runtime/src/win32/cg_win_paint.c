/**
 * @file    cg_win_paint.c
 * @brief   自绘控件窗口类的公共注册样板（10 个自绘类共用）
 * @details 各自绘文件里的 cgw_register_*_class 原本都是逐字相同的 13 行
 *          WNDCLASSEXW 注册样板，差异只有类名 / 窗口过程 / 光标 / 背景画刷四个参数。
 *          收敛到 cgw_register_paint_class 一处后：新增自绘控件不再复制粘贴，
 *          注册字段要改（比如统一加 CS_DBLCLKS）也只改一个地方。
 *          各 cgw_register_*_class 仍保留自己的"整进程一次"守卫与 Doxygen —— 它们是对
 *          cg_win_create.c 的接口，本文件只是它们的共同实现。
 */
#include "cg_win_internal.h"

/**
 * @brief 注册一个自绘控件窗口类（无"整进程一次"守卫，由各 cgw_register_*_class 包装层保证）
 * @param class_name 窗口类名（CG_*_CLASS 常量）
 * @param proc 该类的窗口过程
 * @param cursor 光标资源名（IDC_ARROW / IDC_SIZEWE 等）
 * @param bg 背景画刷；可为 NULL（背景由窗口过程自己负责，自绘类通常如此）
 * @return void 无返回值（RegisterClassExW 的失败按既有行为忽略：类已存在等场景无害）
 */
void cgw_register_paint_class(const wchar_t *class_name, WNDPROC proc,
                              const wchar_t *cursor, HBRUSH bg)
{
    WNDCLASSEXW wc;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, cursor);
    if (bg) wc.hbrBackground = bg;
    wc.lpszClassName = class_name;
    RegisterClassExW(&wc);
}
