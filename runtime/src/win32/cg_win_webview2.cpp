/* cg_win_webview2.cpp -- ECharts 图表控件的 WebView2 宿主
 *
 * ECharts 是 JS 库，必须在浏览器里跑。这里用 WebView2(Edge Chromium) 在一个原生
 * 子窗口里渲染图表：把 echarts.min.js 内联进一段 HTML，再通过
 * PostWebMessageAsJson 把 option 推给页面里的 chart.setOption。
 *
 * 注意：WebView2 的头文件是 C++ 的，所以本文件编译为 C++（g++），和其余 C 后端混链。
 * 为避免引入 libstdc++ 负担，本文件不用异常/RTTI，handler 用 malloc+placement new。
 */
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <new>
#include "WebView2.h"

extern "C" {
#include "cg_win_internal.h"
#include "cstackgui.h"
}

/* 每个 echarts 控件一个宿主状态，挂在宿主窗口 GWLP_USERDATA 上 */
struct cg_echarts_state_t {
    cg_widget_t               *w;
    ICoreWebView2Controller   *controller;
    ICoreWebView2             *webview;
    int                        ready;
};

/* ---- WebView2 环境创建函数指针（来自 WebView2Loader.dll，运行时 LoadLibrary） ---- */
typedef HRESULT (STDMETHODCALLTYPE *PFN_CreateEnv)(
    PCWSTR browserExecutableFolder, PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions *options,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *handler);

static HMODULE g_wv2 = NULL;
static PFN_CreateEnv g_createEnv = NULL;

static int cgw_wv2_ready(void)
{
    if (g_wv2) return 1;
    g_wv2 = LoadLibraryW(L"WebView2Loader.dll");
    if (!g_wv2) {
        cg_log(CG_LOG_ERROR, "WebView2Loader.dll not found (%lu)", GetLastError());
        return 0;
    }
    g_createEnv = (PFN_CreateEnv)GetProcAddress(g_wv2, "CreateCoreWebView2EnvironmentWithOptions");
    if (!g_createEnv) {
        cg_log(CG_LOG_ERROR, "CreateCoreWebView2EnvironmentWithOptions not exported (%lu)", GetLastError());
        return 0;
    }
    return 1;
}

/* 前向声明（定义在本文件后面） */
static LRESULT CALLBACK cgw_echarts_wndproc(HWND, UINT, WPARAM, LPARAM);
static wchar_t *cgw_echarts_html(cg_widget_t *w);

/* UTF-8 -> UTF-16（用系统 API，自己分配/释放，不依赖 cg 分配器） */
static wchar_t *to_wide(const char *u8)
{
    if (!u8) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, u8, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, u8, -1, w, n);
    return w;
}

/* WebView2 没起来时画一行占位字 */
static LRESULT cgw_echarts_placeholder(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, L"图表：需要 WebView2 Runtime", -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    EndPaint(hwnd, &ps);
    return 0;
}

/* ============================ COM 回调（单次使用） ============================ */

class EnvHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
public:
    HWND hwnd;
    EnvHandler(HWND h) : hwnd(h) {}
    ULONG STDMETHODCALLTYPE AddRef() { return 1; }
    ULONG STDMETHODCALLTYPE Release() { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) {
        if (riid == IID_IUnknown ||
            riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *>(this);
            return S_OK;
        }
        *ppv = NULL; return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT res, ICoreWebView2Environment *env);
};

class CtrlHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
public:
    HWND hwnd;
    CtrlHandler(HWND h) : hwnd(h) {}
    ULONG STDMETHODCALLTYPE AddRef() { return 1; }
    ULONG STDMETHODCALLTYPE Release() { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) {
        if (riid == IID_IUnknown ||
            riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler *>(this);
            return S_OK;
        }
        *ppv = NULL; return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT res, ICoreWebView2Controller *ctrl);
};

class NavHandler : public ICoreWebView2NavigationCompletedEventHandler {
public:
    HWND hwnd;
    NavHandler(HWND h) : hwnd(h) {}
    ULONG STDMETHODCALLTYPE AddRef() { return 1; }
    ULONG STDMETHODCALLTYPE Release() { return 1; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) {
        if (riid == IID_IUnknown ||
            riid == IID_ICoreWebView2NavigationCompletedEventHandler) {
            *ppv = static_cast<ICoreWebView2NavigationCompletedEventHandler *>(this);
            return S_OK;
        }
        *ppv = NULL; return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2 *sender, ICoreWebView2NavigationCompletedEventArgs *args);
};

/* 环境就绪 -> 建控制器 */
HRESULT EnvHandler::Invoke(HRESULT res, ICoreWebView2Environment *env)
{
    if (FAILED(res) || !env) {
        cg_log(CG_LOG_ERROR, "CreateCoreWebView2Environment completed with error: 0x%08X", (unsigned)res);
        return S_OK;
    }
    CtrlHandler *h = (CtrlHandler *)malloc(sizeof(CtrlHandler));
    if (h) new (h) CtrlHandler(hwnd);
    env->CreateCoreWebView2Controller(hwnd, h);   /* 控制器在 CtrlHandler::Invoke 里给回 */
    env->Release();
    return S_OK;
}

/* 控制器就绪 -> 取 webview、设可见、导航到内联图表页 */
HRESULT CtrlHandler::Invoke(HRESULT res, ICoreWebView2Controller *ctrl)
{
    if (FAILED(res) || !ctrl) {
        if (FAILED(res)) cg_log(CG_LOG_ERROR, "CreateCoreWebView2Controller failed: 0x%08X", (unsigned)res);
        return S_OK;
    }
    cg_echarts_state_t *st = (cg_echarts_state_t *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (!st) { ctrl->Release(); return S_OK; }

    st->controller = ctrl;
    ctrl->AddRef();

    ICoreWebView2 *webview = NULL;
    ctrl->get_CoreWebView2(&webview);
    st->webview = webview;          /* get_CoreWebView2 返回的就是我们需要的引用 */

    ctrl->put_IsVisible(TRUE);
    RECT rc; GetClientRect(hwnd, &rc);
    ctrl->put_Bounds(rc);

    NavHandler *nh = (NavHandler *)malloc(sizeof(NavHandler));
    if (nh) new (nh) NavHandler(hwnd);
    EventRegistrationToken tok;
    if (st->webview) st->webview->add_NavigationCompleted(nh, &tok);

    wchar_t *html = cgw_echarts_html(st->w);
    if (st->webview && html) st->webview->NavigateToString(html);
    free(html);

    ctrl->Release();                /* state->controller 仍持有一份引用 */
    return S_OK;
}

/* 页面加载完 -> 标记 ready 并把当前 option 推过去 */
HRESULT NavHandler::Invoke(ICoreWebView2 *sender, ICoreWebView2NavigationCompletedEventArgs *args)
{
    (void)sender; (void)args;
    cg_echarts_state_t *st = (cg_echarts_state_t *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
    if (st) {
        st->ready = 1;
        cgw_echarts_apply(st->w);
    }
    return S_OK;
}

/* 拼出内联了 echarts 的图表页 HTML（宽字符，调用方 free） */
wchar_t *cgw_echarts_html(cg_widget_t *w)
{
    (void)w;
    char *dir  = cg_app_dir();
    char *path = cg_path_join(dir, "echarts.min.js");
    char *js   = cg_file_read(path);
    cg_free(dir);
    cg_free(path);

    const char *head =
        "<!doctype html><html><head><meta charset=\"utf-8\"></head>"
        "<body style=\"margin:0;overflow:hidden;background:#fff\">"
        "<div id=\"m\" style=\"width:100%;height:100%\"></div>"
        "<script>";
    const char *tail =
        "</script>"
        "<script>"
        "window.__cgChart=null;"
        "function __cgApply(o){"
        "  if(!window.__cgChart){window.__cgChart=echarts.init(document.getElementById('m'));}"
        "  window.__cgChart.setOption(o,true);window.__cgChart.resize();"
        "}"
        "if(window.chrome&&window.chrome.webview){"
        "  window.chrome.webview.addEventListener('message',function(e){__cgApply(e.data);});"
        "}"
        "</script></body></html>";

    size_t total = strlen(head) + (js ? strlen(js) : 0) + strlen(tail) + 1;
    char *html = (char *)malloc(total);
    if (html) snprintf(html, total, "%s%s%s", head, js ? js : "", tail);
    cg_free(js);

    wchar_t *whtml = to_wide(html);
    free(html);
    return whtml;
}

/* 把控件当前 option 推给 WebView2（控制器没建好时仅暂存，页面加载完会自动推一次） */
void cgw_echarts_apply(cg_widget_t *w)
{
    if (!w || !cgw_is_t(w, "echarts")) return;
    cg_echarts_state_t *st = (cg_echarts_state_t *)GetWindowLongPtr((HWND)w->native, GWLP_USERDATA);
    if (!st || !st->webview || !st->ready) return;
    if (!w->option) return;
    wchar_t *wopt = to_wide(w->option);
    if (wopt) { st->webview->PostWebMessageAsJson(wopt); free(wopt); }
}

/* 注册宿主窗口类（可重复调用：注册失败无害） */
void cgw_register_echarts_class(void)
{
    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = cgw_echarts_wndproc;
    wc.hInstance     = GetModuleHandle(NULL);
    wc.lpszClassName = CG_ECHARTS_CLASS;
    RegisterClassW(&wc);
}

/* 控件建好原生窗口后调用：挂上状态、起 WebView2 */
void cgw_echarts_init(cg_widget_t *w)
{
    if (!w) return;
    HWND hwnd = (HWND)w->native;
    if (!hwnd) return;

    cg_echarts_state_t *st = (cg_echarts_state_t *)malloc(sizeof(cg_echarts_state_t));
    if (!st) return;
    st->w = w; st->controller = NULL; st->webview = NULL; st->ready = 0;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)st);

    if (!cgw_wv2_ready()) return;   /* 没 WebView2Loader.dll：WM_PAINT 画占位 */

    char *datadir = cg_app_data_dir();
    char *udf = cg_path_join(datadir, "webview2");
    cg_free(datadir);
    wchar_t *wudf = to_wide(udf);
    cg_free(udf);

    EnvHandler *h = (EnvHandler *)malloc(sizeof(EnvHandler));
    if (h) new (h) EnvHandler(hwnd);
    HRESULT hr = g_createEnv(NULL, wudf, NULL, h);
    if (FAILED(hr))
        cg_log(CG_LOG_ERROR, "CreateCoreWebView2Environment failed: 0x%08X", (unsigned)hr);
    free(wudf);
}

/* 宿主窗口过程：转发尺寸/销毁，WebView2 没起来时画占位 */
static LRESULT CALLBACK cgw_echarts_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    cg_echarts_state_t *st = (cg_echarts_state_t *)GetWindowLongPtr(hwnd, GWLP_USERDATA);

    if (msg == WM_SIZE) {
        if (st && st->controller) {
            RECT rc; GetClientRect(hwnd, &rc);
            st->controller->put_Bounds(rc);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (msg == WM_PAINT) {
        if (!st || !st->webview) return cgw_echarts_placeholder(hwnd);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    if (msg == WM_DESTROY) {
        if (st) {
            if (st->webview)   st->webview->Release();
            if (st->controller) st->controller->Release();
            free(st);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
