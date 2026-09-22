/**
 * @file    cg_skin.h
 * @brief   自绘外观层（M3 风格）的内部接口，业务代码不应 include
 * @details 当前用 GDI+ 实现矢量圆角/主题色/状态（运行时已依赖 gdiplus）。
 *          绘制集中在独立模块，将来换成 D2D 内核时只改 cg_skin.c，接入点不变。
 */
#ifndef CG_SKIN_H
#define CG_SKIN_H

#include "cg_internal.h"
#include <windows.h>   /* DRAWITEMSTRUCT 等，被 cg_win_internal.h 提前包含时需要 */

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
 * @brief 圆环进度（progressring）的轨道 + 进度弧，走 GDI+ 抗锯齿
 * @param w 圆环控件（读 value/vmin/vmax/style）；hdc 目标 DC；rc 客户区矩形
 * @return void 无返回值；中心百分比文字由调用方补（那要用 cg_win_util.c 的 cgw_font_for）
 */
void cg_skin_draw_ring(cg_widget_t *w, HDC hdc, const RECT *rc);

/**
 * @brief 给输入框（edit：单行 / 多行 / 数值框都是它）套圆角 + 单行文字垂直居中
 * @param w 输入框控件（读 w->native / w->style.radius / w->app->scale）
 * @return void 无返回值；GDI+ 不可用时也照常（圆角仅靠窗口区域，不依赖 GDI+）
 */
void cg_skin_apply_edit(cg_widget_t *w);

/**
 * @brief 把输入框的调节器（上下箭头）摆到边框内侧的右边
 * @param w 输入框控件（读 w->native / w->native2）
 * @return void 无返回值
 * @details 调节器不用 UDS_ALIGNRIGHT：那个样式按"窗口矩形"贴右，会把箭头压在自绘边框
 *          和圆角上。这里按客户区内侧定位（右边让出边框宽度、上下各让出一半），
 *          并给输入框加右内边距，文字不会跑到箭头下面。创建后与每次改尺寸/换字体后都要调。
 */
void cg_skin_edit_fit_spin(cg_widget_t *w);

/** 输入框边框宽度：客户区四边各让出这么多，圆角边框画在客户区**之外**。
 *  这样控件自己重绘（选中文字 / 滚动 / 光标闪烁）永远盖不掉边框——
 *  否则双击选中时会出现"边框缺口"。EM_SETMARGINS 用 CG_EDIT_TEXT_MARGIN，
 *  视觉内边距 = 边框 + 该边距 = 8px（和画布一致）。 */
#define CG_EDIT_BORDER       2
#define CG_EDIT_TEXT_MARGIN  6

/** 数值框（edit + 调节器）右侧那列上下箭头的宽度（逻辑像素，按 scale 缩放）。
 *  **客户区右边要让出这个宽度**，箭头就摆在让出来的这块"非客户区"上 ——
 *  和边框同一个道理：放进客户区里就会被输入框自己的白底重绘盖掉（用户看到的是
 *  "箭头没了"，但窗口其实还在、还能点），点击也可能被输入框吞掉。
 *  给足宽度后 cg_skin_edit.c 会按它显式 SetWindowPos，不再依赖控件自报宽度。 */
#define CG_EDIT_SPIN_W       17

#endif /* CG_SKIN_H */
