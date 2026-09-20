/**
 * @file    cg_skin.h
 * @brief   自绘外观层（M3 风格）的内部接口，业务代码不应 include
 * @details 当前用 GDI+ 实现矢量圆角/主题色/状态（运行时已依赖 gdiplus）。
 *          绘制集中在独立模块，将来换成 D2D 内核时只改 cg_skin.c，接入点不变。
 */
#ifndef CG_SKIN_H
#define CG_SKIN_H

#include "cg_internal.h"
#include <windows.h>   /* DRAWITEMSTRUCT 等，被 cg_win32.c 提前包含时需要 */

/**
 * @brief 用矢量绘制一个 M3 风格按钮（圆角 + 主题色 + 悬停/按下/禁用状态）
 * @param app 所属应用（读 app->scale 做 DPI 缩放）；dis WM_DRAWITEM 的结构体
 * @return void 无返回值；GDI+ 不可用时回退到 GDI RoundRect，按钮不会消失
 */
void cg_skin_draw_button(cg_app_t *app, const DRAWITEMSTRUCT *dis);

/**
 * @brief 用矢量绘制一个 M3 风格复选/单选框（圆角方框 + 勾 / 圆环 + 实心点），支持禁用态
 * @param app 所属应用（读 app->scale 与 doc.bg 做缩放与背景填充）；dis WM_DRAWITEM 的结构体
 * @return void 无返回值；控件类型由 w->type 区分（"checkbox" / "radio"）
 */
void cg_skin_draw_check(cg_app_t *app, const DRAWITEMSTRUCT *dis);

/**
 * @brief M3 开关自绘：右侧胶囊轨道（on 主题紫 / off 灰）+ 白色圆钮，规格对齐画布
 * @param w 开关控件（读 checked/enabled/hot/text）；hdc 目标 DC；rc 控件矩形
 * @return void 无返回值；GDI+ 不可用时回退 GDI
 */
void cg_skin_draw_switch(cg_widget_t *w, HDC hdc, const RECT *rc);

/**
 * @brief 滑块（trackbar）的 NM_CUSTOMDRAW 自绘：把轨道和滑块画成 M3 圆角
 * @param app 所属应用；hdr NM_CUSTOMDRAW 通知头（hwndFrom 必须是 slider）
 * @return LRESULT 返回给 WM_NOTIFY 的 CDRF_* 值，告诉系统哪些阶段我们跳过默认绘制
 */
LRESULT cg_skin_draw_slider_notify(cg_app_t *app, const NMHDR *hdr);

/**
 * @brief 进度条（progress）的 NM_CUSTOMDRAW 自绘：浅灰圆角轨道 + 主题紫填充，与画布一致
 * @param w 进度条控件（读 value/vmin/vmax 算填充比例）；hdr NM_CUSTOMDRAW 通知头
 * @return LRESULT 返回给 WM_NOTIFY 的 CDRF_* 值；PREPAINT 画完整条并 SKIPDEFAULT
 */
/**
 * @brief 进度条自绘：浅灰圆角轨道 + 主题紫填充，与画布预览一致
 * @param w 进度条控件（读 value/vmin/vmax 算填充比例）；hdc 目标 DC；rc 客户区矩形
 * @return void 无返回值；GDI+ 不可用时回退 GDI RoundRect
 */
void cg_skin_draw_progress(cg_widget_t *w, HDC hdc, const RECT *rc);

/**
 * @brief 给输入框（edit/multiedit/spinner）套圆角 + 单行文字垂直居中
 * @param w 输入框控件（读 w->native / w->style.radius / w->app->scale）
 * @return void 无返回值；GDI+ 不可用时也照常（圆角仅靠窗口区域，不依赖 GDI+）
 */
void cg_skin_apply_edit(cg_widget_t *w);

/** 输入框左右边距：文字别怼到圆角边框上（圆角 2px + 6px = 8px，和画布一致）。
 *  spinner 的上下箭头会给 buddy 重设边距、把左边距清掉，cg_win32 建完控件后
 *  要按这个值再补一次左边距。 */
#define CG_EDIT_LEFT_MARGIN  8
#define CG_EDIT_RIGHT_MARGIN 8

#endif /* CG_SKIN_H */
