/* probe-modal.c -- 模态 / 非模态对话框真机验证的外部驱动（serial_verify Form C）
 * 只做 user32 检查与驱动；行为断言的证据在 modaltest 写出的 result.log 里。
 * 驱动序列：
 *   点 Open  -> A1/A2（独立 HWND + owner 禁用）
 *   phase1   点 OK        -> 期望 result=1，顺带读回 edt_port 的值（A3/A4）
 *   phase2   等重开 + WM_CLOSE -> 期望 result=0（"直接关掉"那条路）
 *   phase3   点 OpenNM    -> 非模态（T4）：立即返回 + owner 保持可用 + OK 关闭
 *   phase4   错误路径（T4 收尾）：同一节点重复 open / 全局单槽守卫 / 空关不崩
 *   phase5   对话框里的 tabs（T4 可选项）：页体切换 + select 事件，主窗口基线对照
 *   全程结束主窗口必须还活着（A5）。
 *
 * 入库位置说明：tools\ 下（build\ 整个不入库）——它是多窗体与深色皮肤的**永久回归**：
 * 模态 A1~A5 + 非模态 A6 + 错误路径 A7 + 对话框 tabs A8。别把它放 build\：那个目录会被清，
 * 清完这些真机证据就只剩文档里的一段散文。
 *
 * 复跑（夹具由 tools\gen-modaltest.mjs 生成）：
 *   gcc tools\probe-modal.c -o build\probe-modal.exe -luser32 -lcomctl32
 *   node designer\electron\cgbuild.mjs build\modaltest
 *   cd build\modaltest && ..\probe-modal.exe
 */
#include <windows.h>
#include <commctrl.h>
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
    HWND main, dlg, dlg2, tab, p1, p2, btn, ok;
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

    /* 关于驱动方式（实测，别再绕一遍）：
     *   - SendInput 真鼠标输入**在本环境无效** —— 拿它点 OpenNM 按钮，对话框根本不弹
     *     （dialog=0），点页签同样没反应；落点是对的（WindowFromPoint 就是页签本身）。
     *   - TCM_SETCURSEL 按 MSDN **不发** TCN_SELCHANGE，只会换页签高亮，我们收不到通知。
     * 所以切页一律由夹具在**进程内**照点击的顺序模拟（见 gen-modaltest.mjs 的 fake_selchange），
     * 探针只负责 BM_CLICK 按钮 + 看窗口可见性。 */

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

    /* ---- phase4：错误路径（T4 收尾）----
     *  4a 已经开着一个非模态时**再开一次** —— 守卫必须拒绝（nm2 result=0），
     *     而且原来那个**不许被顶掉**（还在、主窗口也没被禁用）
     *  4b 什么都没开时 cg_dialog_close —— 不许崩，且随后还能正常开（状态没写坏） */
    btn = FindWindowExW(main, NULL, L"BUTTON", L"OpenNM");
    PostMessageW(btn, BM_CLICK, 0, 0);
    dlg = wait_for(L"CStackGUIDialog", L"Port Setup");
    ck("A7 nonmodal reopened for the error-path round", dlg != NULL);
    if (!dlg) return 1;

    btn = FindWindowExW(main, NULL, L"BUTTON", L"OpenNM2");
    ck("OpenNM2 button found", btn != NULL);
    if (btn) PostMessageW(btn, BM_CLICK, 0, 0);
    Sleep(400);   /* 给被拒的那次 open 留出写日志的时间 */
    ck("A7 the already-open dialog survived the refused open", IsWindow(dlg) != 0);
    ck("A7 a refused open must not disable the owner", IsWindowEnabled(main) != 0);

    /* 4a-2：去开**另一个**对话框节点 —— 这才是"同一时间只支持一个"的全局守卫
     *       （对同一个节点重复 open 会被更早的 `native 已存在` 分支拦下，测不到它） */
    btn = FindWindowExW(main, NULL, L"BUTTON", L"OpenOther");
    ck("OpenOther button found", btn != NULL);
    if (btn) PostMessageW(btn, BM_CLICK, 0, 0);
    Sleep(400);
    ck("A7 one-slot guard: no second dialog window appeared",
       FindWindowW(L"CStackGUIDialog", L"Other") == NULL);
    ck("A7 the first dialog survived the refused second node", IsWindow(dlg) != 0);

    /* 关掉它，回到"什么都没开"的状态，再走 4b */
    ok = FindWindowExW(dlg, NULL, L"BUTTON", L"OK");
    if (ok) PostMessageW(ok, BM_CLICK, 0, 0);
    wait_gone(dlg);

    btn = FindWindowExW(main, NULL, L"BUTTON", L"CloseIdle");
    ck("CloseIdle button found", btn != NULL);
    if (btn) PostMessageW(btn, BM_CLICK, 0, 0);
    Sleep(400);
    ck("A7 main alive after closing an idle dialog", IsWindow(main) != 0);

    btn = FindWindowExW(main, NULL, L"BUTTON", L"OpenNM");
    PostMessageW(btn, BM_CLICK, 0, 0);
    dlg = wait_for(L"CStackGUIDialog", L"Port Setup");
    ck("A7 can still open after the idle close (state not corrupted)", dlg != NULL);
    if (dlg) {
        ok = FindWindowExW(dlg, NULL, L"BUTTON", L"OK");
        if (ok) PostMessageW(ok, BM_CLICK, 0, 0);
        wait_gone(dlg);
    }

    /* ---- 基线对照：主窗口里的 tabs ----
     *  判据不看页签高亮（TCM_SETCURSEL 自己会换），看**页体**：切页后页 1 隐藏 / 页 2 显示
     *  —— 那是我们自己的 cgw_sync_tabs_pages 做的。 */
    tab = FindWindowExW(main, NULL, L"SysTabControl32", NULL);
    ck("baseline: tab control exists in the main window", tab != NULL);
    if (tab) {
        p1 = FindWindowExW(tab, NULL, L"BUTTON", L"OnM1");
        p2 = FindWindowExW(tab, NULL, L"BUTTON", L"OnM2");
        if (!p1) p1 = FindWindowExW(main, NULL, L"BUTTON", L"OnM1");
        if (!p2) p2 = FindWindowExW(main, NULL, L"BUTTON", L"OnM2");
        ck("baseline: both page controls found", p1 != NULL && p2 != NULL);
        if (p1 && p2) {
            btn = FindWindowExW(main, NULL, L"BUTTON", L"SelMain");
            ck("baseline: SelMain button found", btn != NULL);
            if (btn) PostMessageW(btn, BM_CLICK, 0, 0);
            Sleep(400);
            ck("baseline: switching tabs in the MAIN window swaps the page body",
               IsWindowVisible(p1) == 0 && IsWindowVisible(p2) != 0);
        }
    }

    /* ---- phase5：对话框里的 tabs（T4 可选项，此前从未真机验证）----
     *  判据不看页签高亮（TCM_SETCURSEL 自己会换），看**页体**：
     *  页 1 的按钮显示 / 页 2 的隐藏，切页后反过来 —— 那是我们自己的
     *  cgw_sync_tabs_pages 做的，也是"通知有没有走到我们手里"的硬证据。 */
    btn = FindWindowExW(main, NULL, L"BUTTON", L"OpenTabs");
    ck("OpenTabs button found", btn != NULL);
    if (btn) PostMessageW(btn, BM_CLICK, 0, 0);
    dlg2 = wait_for(L"CStackGUIDialog", L"Other");
    ck("A8 tabs dialog opened", dlg2 != NULL);
    if (dlg2) {
        tab = FindWindowExW(dlg2, NULL, L"SysTabControl32", NULL);
        ck("A8 tab control exists inside the dialog", tab != NULL);
        if (tab) {
            /* 页内控件有的挂在 tab 窗口下、有的挂在对话框下，两处都找一遍 */
            p1 = FindWindowExW(tab, NULL, L"BUTTON", L"OnP1");
            p2 = FindWindowExW(tab, NULL, L"BUTTON", L"OnP2");
            if (!p1) p1 = FindWindowExW(dlg2, NULL, L"BUTTON", L"OnP1");
            if (!p2) p2 = FindWindowExW(dlg2, NULL, L"BUTTON", L"OnP2");
            ck("A8 page-1 control found", p1 != NULL);
            ck("A8 page-2 control found", p2 != NULL);
            if (p1 && p2) {
                ck("A8 initially page 1 shown, page 2 hidden",
                   IsWindowVisible(p1) != 0 && IsWindowVisible(p2) == 0);
                /* 驱动方式见文件头的注释：本环境注入不了真实鼠标输入，TCM_SETCURSEL 又不发
                 * TCN_SELCHANGE —— 所以由夹具在**进程内**照点击的顺序模拟（SelDlg 按钮），
                 * 对话框过程收到的 WM_NOTIFY 与真实点击完全同构。 */
                btn = FindWindowExW(main, NULL, L"BUTTON", L"SelDlg");
                ck("A8 SelDlg button found", btn != NULL);
                if (btn) PostMessageW(btn, BM_CLICK, 0, 0);
                Sleep(400);
                ck("A8 switching tabs inside a dialog swaps the page body",
                   IsWindowVisible(p1) == 0 && IsWindowVisible(p2) != 0);
            }
        }
        ok = FindWindowExW(dlg2, NULL, L"BUTTON", L"OK");
        if (ok) PostMessageW(ok, BM_CLICK, 0, 0);
        wait_gone(dlg2);
    }

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
            if (strstr(line, "nm result="))
                ck("A6 nm result=1 (nonmodal opened)", strstr(line, "result=1") != NULL);
            if (strstr(line, "nm2"))
                ck("A7 re-open on the SAME node refused (nm2 result=0)", strstr(line, "result=0") != NULL);
            if (strstr(line, "other"))
                ck("A7 one-slot guard refused a second dialog node (other result=0)", strstr(line, "result=0") != NULL);
            if (strstr(line, "tabsel="))
                ck("A8 tabs select event reached us inside a dialog", strstr(line, "tabsel=1") != NULL);
            if (strstr(line, "tabs result="))
                ck("A8 tabs dialog opened (tabs result=1)", strstr(line, "result=1") != NULL);
            if (strstr(line, "idle_close"))
                ck("A7 idle close did not crash (log written afterwards)", strstr(line, "done") != NULL);
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
