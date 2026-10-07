/**
 * @file    splash.c
 * @brief   便携版画布的原生"预启动器"：先画出启动图，再拉起真正的 CStackGUI-app.exe
 *
 * 为什么需要它（2026-10-07）：便携版每次启动都要自解压，而打包器那张启动图**到解包结束就撤**
 * （electron-builder 的 portable.nsi 把 BgImage::Destroy 写在 ExecWait 之前）。解包之后
 * Electron 还要约 2.7 秒才加载到 main.mjs —— 那段时间屏幕上什么都没有，而且**补不了**：
 * 那时还没有任何属于本程序的进程在运行。
 *
 * 这个 20KB 的启动器在解包结束那一刻被解包器拉起（打包时把 CStackGUI.exe 这个名字让给它，
 * 真程序改名成 CStackGUI-app.exe），所以它能在 1.6 秒时就画出画面，一直盖到真窗口出现为止，
 * 启动图与窗口之间不再有空档。单文件分发形态不变。
 *
 * 与 designer/build/afterPack.cjs 的约定（改一处必须改另一处）：
 *   1. 真程序叫 CStackGUI-app.exe，与本启动器同目录
 *   2. 同目录若有 splash.bmp 就画它；没有就画品牌底色兜底
 *   3. 带命令行参数时不显示启动图（--build 这类无窗口调用不该被挡住）
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#define SPLASH_CLASS L"CStackGUISplash"
#define APP_EXE_NAME L"CStackGUI-app.exe"
#define SPLASH_BMP_NAME L"splash.bmp"

/**
 * @brief 诊断日志：设了 CSTACKGUI_SPLASH_LOG=<文件路径> 才写一行
 * @param tag 事件名（ASCII）
 * @param val 附带数值（pid、毫秒数等；没有就传 0）
 * @return void 写不进去就静默跳过，绝不影响启动
 */
static void slog(const char *tag, unsigned long val)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"CSTACKGUI_SPLASH_LOG", path,
                                      sizeof(path) / sizeof(wchar_t));
    if (n == 0 || n >= sizeof(path) / sizeof(wchar_t))
    {
        return;
    }
    FILE *f = _wfopen(path, L"a");
    if (f == NULL)
    {
        return;
    }
    fprintf(f, "t=%lu %s %lu\n", GetTickCount(), tag, val);
    fclose(f);
}

/* 子进程起来后多久还没窗口才显示启动图：安装版约 0.9 秒出窗口，先等一会儿免得闪一下。
 * 便携版例外 —— 解包器 1.6 秒撤掉它自己那张图，晚一步就露白，所以那边宽限期是 0。 */
#define GRACE_MS 250
/* 轮询子进程窗口的间隔 */
#define POLL_MS 20
/* 认作"真窗口"的最小尺寸，用来滤掉 Chromium 那些不可见的辅助窗口 */
#define MIN_WIN_W 300
#define MIN_WIN_H 200

static HBITMAP s_bmp; /* 启动图位图；NULL = 画品牌底色兜底 */
static int s_bmp_w;
static int s_bmp_h;

/**
 * @brief 取本 exe 所在目录（不带结尾反斜杠）
 * @param out 输出缓冲区
 * @param cap out 的 wchar 容量
 * @return void 容量不够时写出空串
 */
static void get_exe_dir(wchar_t *out, int cap)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (n == 0 || n >= (DWORD)cap)
    {
        out[0] = L'\0';
        return;
    }
    int cut = -1;
    for (DWORD i = 0; i < n; i++)
    {
        if (path[i] == L'\\' || path[i] == L'/')
        {
            cut = (int)i;
        }
    }
    if (cut < 0)
    {
        out[0] = L'\0';
        return;
    }
    for (int i = 0; i < cut && i < cap - 1; i++)
    {
        out[i] = path[i];
    }
    out[cut < cap - 1 ? cut : cap - 1] = L'\0';
}

/**
 * @brief 加载同目录的 splash.bmp 到 s_bmp
 * @param dir 本 exe 所在目录
 * @return void 失败时 s_bmp 保持 NULL，由绘制端退化成品牌底色
 */
static void load_splash(const wchar_t *dir)
{
    wchar_t path[MAX_PATH * 2];
    int n = 0;
    while (dir[n] != L'\0' && n < MAX_PATH)
    {
        path[n] = dir[n];
        n++;
    }
    if (n + 1 + (int)(sizeof(SPLASH_BMP_NAME) / sizeof(wchar_t)) >= (int)(sizeof(path) / sizeof(wchar_t)))
    {
        return;
    }
    path[n++] = L'\\';
    for (int i = 0; SPLASH_BMP_NAME[i] != L'\0'; i++)
    {
        path[n++] = SPLASH_BMP_NAME[i];
    }
    path[n] = L'\0';

    s_bmp = (HBITMAP)LoadImageW(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (s_bmp == NULL)
    {
        return;
    }
    BITMAP bm;
    if (GetObjectW(s_bmp, sizeof(bm), &bm) == 0)
    {
        DeleteObject(s_bmp);
        s_bmp = NULL;
        return;
    }
    s_bmp_w = (int)bm.bmWidth;
    s_bmp_h = (int)bm.bmHeight;
}

/**
 * @brief 启动图窗口过程：客户区整块填图，没有图就填品牌底色
 * @param hwnd 窗口句柄
 * @param msg 消息
 * @param wp wParam
 * @param lp lParam
 * @return LRESULT 走 DefWindowProc 的默认处理
 */
static LRESULT CALLBACK splash_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1; /* 下面整块重画，别再擦一遍底色闪一下 */
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (s_bmp != NULL)
        {
            HDC mem = CreateCompatibleDC(dc);
            if (mem != NULL)
            {
                HGDIOBJ old = SelectObject(mem, s_bmp);
                SetStretchBltMode(dc, HALFTONE);
                SetBrushOrgEx(dc, 0, 0, NULL);
                StretchBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, s_bmp_w, s_bmp_h, SRCCOPY);
                SelectObject(mem, old);
                DeleteDC(mem);
            }
        }
        else
        {
            /* 兜底：品牌紫 #6750A4 + 中间一个 C，与画布启动页同款 */
            HBRUSH bg = CreateSolidBrush(RGB(0xF3, 0xED, 0xF7));
            if (bg != NULL)
            {
                FillRect(dc, &rc, bg);
                DeleteObject(bg);
            }
            HBRUSH mark = CreateSolidBrush(RGB(0x67, 0x50, 0xA4));
            if (mark != NULL)
            {
                HGDIOBJ old = SelectObject(dc, mark);
                int d = 64;
                int x = (rc.right - d) / 2;
                int y = (rc.bottom - d) / 2;
                Ellipse(dc, x, y, x + d, y + d);
                SelectObject(dc, old);
                DeleteObject(mark);
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/**
 * @brief 诊断日志（带字符串）：记下被判成"主窗口"的那个窗口长什么样
 * @param tag 事件名
 * @param cls 窗口类名
 * @param w 窗口宽
 * @param h 窗口高
 * @return void
 */
static void slog_win(const char *tag, const wchar_t *cls, int w, int h)
{
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"CSTACKGUI_SPLASH_LOG", path,
                                      sizeof(path) / sizeof(wchar_t));
    if (n == 0 || n >= sizeof(path) / sizeof(wchar_t))
    {
        return;
    }
    FILE *f = _wfopen(path, L"a");
    if (f == NULL)
    {
        return;
    }
    fprintf(f, "t=%lu %s %ls %dx%d\n", GetTickCount(), tag, cls, w, h);
    fclose(f);
}

/** @brief EnumWindows 回调的上下文：找某个进程的可见主窗口 */
typedef struct
{
    DWORD pid;
    HWND found;
    wchar_t cls[64]; /* 命中窗口的类名，用于诊断日志 */
    int w;
    int h;
} find_ctx_t;

/**
 * @brief EnumWindows 回调：挑出属于该进程、可见且尺寸像样子的顶层窗口
 * @param hwnd 候选窗口
 * @param lp find_ctx_t* 上下文
 * @return BOOL FALSE 表示已经找到，停止枚举
 */
static BOOL CALLBACK find_proc(HWND hwnd, LPARAM lp)
{
    find_ctx_t *ctx = (find_ctx_t *)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != ctx->pid || !IsWindowVisible(hwnd))
    {
        return TRUE;
    }
    RECT rc;
    if (!GetWindowRect(hwnd, &rc))
    {
        return TRUE;
    }
    if (rc.right - rc.left < MIN_WIN_W || rc.bottom - rc.top < MIN_WIN_H)
    {
        return TRUE;
    }
    ctx->found = hwnd;
    ctx->w = (int)(rc.right - rc.left);
    ctx->h = (int)(rc.bottom - rc.top);
    if (GetClassNameW(hwnd, ctx->cls, 64) == 0)
    {
        ctx->cls[0] = L'\0';
    }
    return FALSE;
}

/**
 * @brief 找出子进程那个"像主窗口"的可见窗口
 * @param pid 子进程 id
 * @param out 命中时写回类名与尺寸（可为 NULL）
 * @return HWND 没找到返回 NULL
 */
static HWND find_child_window(DWORD pid, find_ctx_t *out)
{
    find_ctx_t ctx;
    ctx.pid = pid;
    ctx.found = NULL;
    ctx.cls[0] = L'\0';
    ctx.w = 0;
    ctx.h = 0;
    EnumWindows(find_proc, (LPARAM)&ctx);
    if (ctx.found != NULL && out != NULL)
    {
        *out = ctx;
    }
    return ctx.found;
}

/**
 * @brief 从完整命令行里跳过程序名，取出后面的参数原文
 * @param cmd GetCommandLineW() 的原文
 * @return const wchar_t* 指向参数段（可能为空串），不新分配内存
 */
static const wchar_t *args_after_program(const wchar_t *cmd)
{
    const wchar_t *p = cmd;
    while (*p == L' ' || *p == L'\t')
    {
        p++;
    }
    if (*p == L'"')
    {
        p++;
        while (*p != L'\0' && *p != L'"')
        {
            p++;
        }
        if (*p == L'"')
        {
            p++;
        }
    }
    else
    {
        while (*p != L'\0' && *p != L' ' && *p != L'\t')
        {
            p++;
        }
    }
    while (*p == L' ' || *p == L'\t')
    {
        p++;
    }
    return p;
}

/**
 * @brief 创建启动图窗口（居中、无边框、置顶、不进任务栏）
 * @return HWND 失败返回 NULL，调用方当成"没有启动图"继续跑
 */
static HWND create_splash(void)
{
    HINSTANCE inst = GetModuleHandleW(NULL);
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = splash_proc;
    wc.hInstance = inst;
    wc.lpszClassName = SPLASH_CLASS;
    /* IDC_ARROW 是 ANSI 版资源宏，这里走 W 系 API 所以显式转一下 */
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return NULL;
    }

    int w = s_bmp_w > 0 ? s_bmp_w : 420;
    int h = s_bmp_h > 0 ? s_bmp_h : 260;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    return CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, SPLASH_CLASS, L"CStackGUI",
                           WS_POPUP, (sw - w) / 2, (sh - h) / 2, w, h, NULL, NULL, inst, NULL);
}

/**
 * @brief 入口：起真程序；带参数就直接转发不管画面，不带参数则用启动图盖住冷启动那几秒
 * @param inst 本 exe 实例句柄
 * @param prev 保留
 * @param cmdline ANSI 命令行（不用，实际取 GetCommandLineW）
 * @param show nCmdShow
 * @return int 真程序的退出码；拉不起来时返回 1
 */
int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    (void)inst;
    (void)prev;
    (void)cmdline;
    (void)show;

    SetProcessDPIAware(); /* 高清屏上别让位图被系统拉糊 */

    wchar_t dir[MAX_PATH];
    get_exe_dir(dir, MAX_PATH);
    if (dir[0] == L'\0')
    {
        MessageBoxW(NULL, L"取不到自身所在目录。", L"CStackGUI", MB_ICONERROR);
        return 1;
    }

    /* 拼子进程命令行：完整路径 + 原样透传的参数 */
    const wchar_t *args = args_after_program(GetCommandLineW());
    wchar_t cmd[4096];
    int n = 0;
    cmd[n++] = L'"';
    for (int i = 0; dir[i] != L'\0' && n < 3800; i++)
    {
        cmd[n++] = dir[i];
    }
    cmd[n++] = L'\\';
    for (int i = 0; APP_EXE_NAME[i] != L'\0' && n < 3800; i++)
    {
        cmd[n++] = APP_EXE_NAME[i];
    }
    cmd[n++] = L'"';
    if (*args != L'\0')
    {
        cmd[n++] = L' ';
        for (int i = 0; args[i] != L'\0' && n < 4000; i++)
        {
            cmd[n++] = args[i];
        }
    }
    cmd[n] = L'\0';

    wchar_t exe[MAX_PATH * 2];
    n = 0;
    for (int i = 0; dir[i] != L'\0'; i++)
    {
        exe[n++] = dir[i];
    }
    exe[n++] = L'\\';
    for (int i = 0; APP_EXE_NAME[i] != L'\0'; i++)
    {
        exe[n++] = APP_EXE_NAME[i];
    }
    exe[n] = L'\0';

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi))
    {
        /* 真程序不在旁边：多半是名字改了而 afterPack 没跟着改，直接说清楚 */
        wchar_t msg[1200];
        wsprintfW(msg, L"启动失败：找不到或起不来\n%s\n\n(错误码 %lu)", exe, GetLastError());
        MessageBoxW(NULL, msg, L"CStackGUI", MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread);

    /* 便携版：解包器会设 PORTABLE_EXECUTABLE_FILE（见 electron-builder 的 portable.nsi）。
     * 它那张启动图到解包结束就撤，所以这边一秒都不能晚 —— 宽限期直接给 0。
     * 安装版没有这个变量，走 GRACE_MS，快速启动时不会闪一下。 */
    wchar_t portable[8];
    DWORD grace = (GetEnvironmentVariableW(L"PORTABLE_EXECUTABLE_FILE", portable,
                                           sizeof(portable) / sizeof(wchar_t)) > 0)
                      ? 0
                      : GRACE_MS;

    slog("spawned pid=", pi.dwProcessId);
    HWND splash = NULL;
    if (*args == L'\0')
    {
        load_splash(dir);
        slog("bmp loaded=", s_bmp != NULL ? 1UL : 0UL);
        splash = create_splash(); /* 先建好但藏着，过 grace 还没窗口才显示 */
        slog("splash created=", splash != NULL ? 1UL : 0UL);
        if (splash != NULL)
        {
            ShowWindow(splash, SW_HIDE);
        }
    }

    DWORD start = GetTickCount();
    BOOL splash_shown = FALSE;
    DWORD exit_code = 1;
    for (;;)
    {
        DWORD w = MsgWaitForMultipleObjects(1, &pi.hProcess, FALSE, POLL_MS, QS_ALLINPUT);
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (w != WAIT_TIMEOUT) /* 子进程已退出 */
        {
            break;
        }
        if (splash == NULL)
        {
            continue;
        }
        if (!splash_shown && GetTickCount() - start >= grace)
        {
            ShowWindow(splash, SW_SHOWNOACTIVATE);
            UpdateWindow(splash);
            splash_shown = TRUE;
            slog("splash shown after ms=", GetTickCount() - start);
        }
        find_ctx_t hit;
        if (splash_shown && find_child_window(pi.dwProcessId, &hit) != NULL)
        {
            slog_win("child window at ms=", hit.cls, hit.w, hit.h);
            DestroyWindow(splash);
            splash = NULL; /* 画面交给真窗口，启动器从此静默到子进程退出 */
        }
    }

    if (splash != NULL)
    {
        DestroyWindow(splash);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    return (int)exit_code;
}
