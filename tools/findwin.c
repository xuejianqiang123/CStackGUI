/* findwin.c -- 按类名找窗口并报告（probe-modal 同款手法，已验证可靠）
 *
 * 为什么要有它：PowerShell 的 Add-Type 委托在两轮跑法下都**静默失灵**（连已知有窗口的
 * modaltest 都"枚举不到"）——换个工具一次就对。**工具不可靠时换工具，别把"工具找不到"
 * 当成"应用没窗口"**。tools\probe-dialog-e2e.mjs 用它做真窗口断言。
 *
 * 入库位置说明：tools\ 下（build\ 整个不入库）。
 * 编译：gcc tools\findwin.c -o build\findwin.exe -luser32
 */
#include <windows.h>
#include <stdio.h>

int main(void)
{
    HWND main = FindWindowW(L"CStackGUIMain", NULL);
    HWND dlg  = FindWindowW(L"CStackGUIDialog", NULL);
    printf("main=%p dlg=%p\n", (void *)main, (void *)dlg);
    if (dlg) {
        RECT r;
        GetWindowRect(dlg, &r);
        printf("pos=%ld,%ld\n", (long)r.left, (long)r.top);
        if (main) printf("owner-enabled=%d\n", IsWindowEnabled(main));
    }
    return 0;
}
