/* probe-inctest.c -- F3 多文件组装的真机驱动（A3 / A5 取证）
 * 只做 user32 检查与驱动；被测程序 inctest.exe 由调用方先启动
 * （它在 ready 里自动打开片段引用来的对话框）。
 *
 * 用法：probe-inctest.exe [期望的 label 文字]   （默认 "included label v1"）
 *   A5  片段引用来的对话框能开、控件在、OK 能关（事件命中）
 *   A3  改片段文件（不重编）重开程序，label 文字跟着变 —— 期望文字从命令行传
 *
 * 入库位置说明：tools\ 下（build\ 整个不入库）。复跑：
 *   node tools\gen-inctest.mjs
 *   cd build\inctest && ..\..\build\probe-inctest.exe
 * （先 start inctest.exe 再跑探针，与 probe-modal 同一套流程；或让调用方脚本起进程。）
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int fails = 0;

static void ck(const char *name, int ok)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) fails++;
}

static HWND wait_for(const wchar_t *cls, const wchar_t *title)
{
    HWND h = NULL;
    for (int i = 0; i < 50 && !h; i++) {
        h = FindWindowW(cls, title);
        if (!h) Sleep(100);
    }
    return h;
}

static void wait_gone(HWND h)
{
    for (int i = 0; i < 50 && IsWindow(h); i++) Sleep(100);
}

int main(int argc, char **argv)
{
    const wchar_t *want_lab = L"included label v1";
    HWND main, dlg, lab, ok;

    if (argc > 1) {
        size_t n = 0;
        wchar_t wbuf[128];
        mbstowcs_s(&n, wbuf, 128, argv[1], _TRUNCATE);
        want_lab = wcsdup(wbuf);
    }

    main = wait_for(L"CStackGUIMain", L"include test");
    ck("main window found", main != NULL);
    if (!main) return 1;

    /* A5：片段引用来的对话框自己弹出来了（ready 里 cg_dialog_open_nonmodal） */
    dlg = wait_for(L"CStackGUIDialog", L"PortSetup (from fragment)");
    ck("A5 dialog from the included fragment is open", dlg != NULL);
    if (!dlg) return 1;
    ck("A5 it is an independent HWND (not the main window)", dlg != main);

    /* A3/A5：片段里的 label —— 文字必须等于期望值（A3 时命令行传 v2） */
    lab = FindWindowExW(dlg, NULL, L"STATIC", want_lab);
    if (!lab) {
        /* 容错：不在直接子级就枚举找 */
        HWND c = FindWindowExW(dlg, NULL, NULL, NULL);
        for (; c && !lab; c = FindWindowExW(dlg, c, NULL, NULL)) {
            wchar_t t[128];
            if (GetWindowTextW(c, t, 128) > 0 && wcscmp(t, want_lab) == 0) lab = c;
        }
    }
    ck("A3/A5 fragment label with the expected text is there", lab != NULL);

    /* A5：片段里的 OK —— 事件命中（引用来的控件回调走宿主的 CG_BIND 表） */
    ok = FindWindowExW(dlg, NULL, L"BUTTON", L"OK");
    ck("A5 OK button (from the fragment) found", ok != NULL);
    if (ok) {
        PostMessageW(ok, BM_CLICK, 0, 0);
        wait_gone(dlg);
        ck("A5 OK closes the dialog (binding from the host's table hit)", IsWindow(dlg) == 0);
    }

    ck("main window still alive", IsWindow(main) != 0);

    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
