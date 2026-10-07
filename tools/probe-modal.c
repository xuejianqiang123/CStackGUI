/* probe-modal.c -- 模态 / 非模态对话框真机验证的外部驱动（serial_verify Form C）
 * 只做 user32 检查与驱动；行为断言的证据在 modaltest 写出的 result.log 里。
 * 驱动序列：
 *   点 Open  -> A1/A2（独立 HWND + owner 禁用）
 *   phase1   点 OK        -> 期望 result=1，顺带读回 edt_port 的值（A3/A4）
 *   phase2   等重开 + WM_CLOSE -> 期望 result=0（"直接关掉"那条路）
 *   phase3   点 OpenNM    -> 非模态（T4）：立即返回 + owner 保持可用 + OK 关闭
 *   全程结束主窗口必须还活着（A5）。
 *
 * 入库位置说明：tools\ 下（build\ 整个不入库）——它是多窗体与深色皮肤两轮的**永久回归**：
 * 27 项断言（模态 A1~A5 + 非模态 A6）。别把它放 build\：那个目录会被清，
 * 清完这些真机证据就只剩文档里的一段散文。
 *
 * 复跑（夹具由 tools\gen-modaltest.mjs 生成）：
 *   gcc tools\probe-modal.c -o build\probe-modal.exe -luser32
 *   node designer\electron\cgbuild.mjs build\modaltest
 *   cd build\modaltest && ..\probe-modal.exe
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
/* 轮询等一个窗口出现（5 秒） */
static HWND wait_for(const wchar_t *cls, const wchar_t *title)
{
    HWND h = NULL;
    int i;
    for (i = 0; i < 50 && !h; i++) {
        h = FindWindowW(cls, title);
        if (!h) Sleep(100);
    }
    return h;
}
/* 等一个窗口消失（phase1 关闭的判据） */
static void wait_gone(HWND h)
{
    int i;
    for (i = 0; i < 50 && IsWindow(h); i++) Sleep(100);
}

int main(void)
{
    HWND main, dlg, btn, ok;
    wchar_t title[128];
    LONG style;
    FILE *f;
    char line[256];
    int seen_phase1 = 0, seen_phase2 = 0, seen_alive = 0;
    int i;

    /* 等主窗口 */
    main = wait_for(L"CStackGUIMain", L"modal test");
    ck("main window found", main != NULL);
    if (!main) return 1;
    ck("pre-open owner enabled", IsWindowEnabled(main) != 0);

    /* 点 Open（BM_CLICK 直发到按钮） */
    btn = FindWindowExW(main, NULL, L"BUTTON", L"Open");
    ck("Open button found", btn != NULL);
    if (!btn) return 1;
    PostMessageW(btn, BM_CLICK, 0, 0);

    /* 等对话框 */
    dlg = wait_for(L"CStackGUIDialog", L"Port Setup");
    ck("A1 dialog is an independent HWND", dlg != NULL && dlg != main);
    if (!dlg) return 1;
    style = GetWindowLongW(dlg, GWL_STYLE);
    ck("A1 dialog has WS_CAPTION (title bar -> draggable)", (style & WS_CAPTION) != 0);
    ck("A1 dialog has WS_SYSMENU (close box)", (style & WS_SYSMENU) != 0);
    GetWindowTextW(dlg, title, 128);
    printf("     dialog title=%ls\n", title);
    ck("A2 owner disabled while modal", IsWindowEnabled(main) == 0);

    /* phase1：点 OK */
    ok = FindWindowExW(dlg, NULL, L"BUTTON", L"OK");
    ck("OK button found", ok != NULL);
    if (!ok) return 1;
    PostMessageW(ok, BM_CLICK, 0, 0);
    wait_gone(dlg);

    /* 等 phase2 的对话框重开（on_open 在 phase1 返回后立刻再开一遍） */
    for (i = 0; i < 50; i++) {
        if (fopen("result.log", "r")) break;
        Sleep(100);
    }
    dlg = wait_for(L"CStackGUIDialog", L"Port Setup");
    ck("phase2 dialog reopened", dlg != NULL);
    if (dlg) {
        PostMessageW(dlg, WM_CLOSE, 0, 0);   /* "直接关掉"那条路 */
        wait_gone(dlg);
    }

    /* ---- phase3：非模态（T4）——立即返回、主窗口不禁用、OK 走 cg_dialog_close 关闭 ---- */
    btn = FindWindowExW(main, NULL, L"BUTTON", L"OpenNM");
    ck("OpenNM button found", btn != NULL);
    if (!btn) return 1;
    PostMessageW(btn, BM_CLICK, 0, 0);
    dlg = wait_for(L"CStackGUIDialog", L"Port Setup");
    ck("A6 nonmodal dialog opened", dlg != NULL);
    if (!dlg) return 1;
    ck("A6 owner still enabled (nonmodal)", IsWindowEnabled(main) != 0);
    ok = FindWindowExW(dlg, NULL, L"BUTTON", L"OK");
    ck("OK button found (nonmodal)", ok != NULL);
    if (ok) PostMessageW(ok, BM_CLICK, 0, 0);
    wait_gone(dlg);
    ck("A6 nonmodal dialog closed via cg_dialog_close", IsWindow(dlg) == 0);
    ck("A6 main alive after nonmodal close", IsWindow(main) != 0);

    /* 等日志写完（alive= 是最后一行） */
    for (i = 0; i < 50; i++) {
        f = fopen("result.log", "r");
        if (f) {
            while (fgets(line, sizeof(line), f)) {
                if (strstr(line, "phase1")) seen_phase1 = 1;
                if (strstr(line, "phase2")) seen_phase2 = 1;
                if (strstr(line, "alive=")) seen_alive = 1;
            }
            fclose(f);
            if (seen_alive) break;
        }
        Sleep(100);
    }

    /* 打日志原文 + 断言 */
    printf("---- result.log ----\n");
    f = fopen("result.log", "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            printf("     %s", line);
            if (strstr(line, "phase1")) {
                ck("A3 phase1 result=1 (OK path)", strstr(line, "result=1") != NULL);
                ck("A4 edt_port readable via cg_find (port=COM3)", strstr(line, "port=COM3") != NULL);
            }
            if (strstr(line, "phase2"))
                ck("A3 phase2 result=0 (WM_CLOSE path)", strstr(line, "result=0") != NULL);
            if (strstr(line, "alive="))
                ck("A5 main window still alive", strstr(line, "alive=1") != NULL);
            if (strstr(line, "nm"))
                ck("A6 nm result=1 (nonmodal opened)", strstr(line, "result=1") != NULL);
        }
        fclose(f);
    } else {
        ck("result.log written", 0);
    }
    ck("A5 main window still alive (user32)", IsWindow(main) != 0);
    ck("A5 owner re-enabled after close", IsWindowEnabled(main) != 0);

    printf(fails ? "\n%d FAILED\n" : "\nALL PASS\n", fails);
    return fails ? 1 : 0;
}
