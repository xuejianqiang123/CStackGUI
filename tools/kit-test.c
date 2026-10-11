/* kit-test.c -- cg_kit 基础能力库的冒烟测试（文件 / 路径 / INI / 编码 / 剪贴板 / 注册表）
 *
 * 为什么要留一个：cg_kit 里 30 个 API 全是"封装 Win32"的脏活（BOM、CRLF、节插入位置、
 * 编码往返），改一行就可能悄悄坏掉，而画布和 gallery 都覆盖不到它们。
 * 跑法：tools\check-kit.bat（编译 + 在 build\kittest\ 里跑，不碰仓库其它文件）。
 *
 * 约定：只用业务代码允许的头（cstackgui.h + 标准 C），顺便验证"业务代码不必 include
 * Windows 头也能写文件和配置"这条铁律。
 */
#include "cstackgui.h"

#include <stdio.h>
#include <string.h>

static int fails = 0;

/** @brief 断言 + 打一行结果 */
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "[ok]  " : "[FAIL]", what);
    if (!ok) fails++;
}

int main(void)
{
    /* ---- 文件 ---- */
    check(cg_file_write("kit_test.txt", "第一行\n") == 0, "cg_file_write");
    check(cg_file_append("kit_test.txt", "第二行\n") == 0, "cg_file_append");
    check(cg_file_exists("kit_test.txt") == 1, "cg_file_exists");
    {
        char *t = cg_file_read("kit_test.txt");
        check(t && strcmp(t, "第一行\n第二行\n") == 0, "cg_file_read 内容正确");
        check(t && strstr(t, "第一") != NULL, "cg_file_read 中文未损坏");
        cg_free(t);
    }
    check(cg_file_write("kit_bom.txt", "\xEF\xBB\xBFhello") == 0, "写带 BOM 的文件");
    {
        char *t = cg_file_read("kit_bom.txt");
        check(t && strcmp(t, "hello") == 0, "cg_file_read 去掉 UTF-8 BOM");
        cg_free(t);
    }
    check(cg_file_copy("kit_test.txt", "kit_copy.txt") == 0, "cg_file_copy");
    check(cg_file_delete("kit_copy.txt") == 0, "cg_file_delete");
    check(cg_file_exists("kit_copy.txt") == 0, "删除后不存在");

    /* ---- 目录 + 枚举 ---- */
    check(cg_dir_create("kit_dir") == 0, "cg_dir_create");
    check(cg_dir_create("kit_dir") == 0, "重复建目录也算成功");
    check(cg_dir_exists("kit_dir") == 1, "cg_dir_exists");
    cg_file_write("kit_dir\\a.txt", "a");
    cg_file_write("kit_dir\\b.txt", "b");
    {
        char name[256];
        int  n = 0;
        while (cg_dir_list("kit_dir", "*.txt", n, name, sizeof(name))) {
            if (strcmp(name, "a.txt") && strcmp(name, "b.txt")) { n = -100; break; }
            n++;
        }
        check(n == 2, "cg_dir_list 枚举到 2 个文件");
    }

    /* ---- 路径 ---- */
    {
        char *j = cg_path_join("d:\\a", "b.txt");
        char *d = cg_path_dir("d:\\a\\b\\c.txt");
        check(j && strcmp(j, "d:\\a\\b.txt") == 0, "cg_path_join");
        check(d && strcmp(d, "d:\\a\\b") == 0, "cg_path_dir（不带末尾分隔符）");
        check(strcmp(cg_path_name("d:\\a\\b\\c.txt"), "c.txt") == 0, "cg_path_name");
        check(strcmp(cg_path_ext("d:\\a\\c.txt"), ".txt") == 0, "cg_path_ext");
        check(strcmp(cg_path_ext("d:\\a\\c"), "") == 0, "cg_path_ext 无扩展名");
        cg_free(j);
        cg_free(d);
    }
    {
        char *ad = cg_app_dir();
        char *dd = cg_app_data_dir();
        check(ad && cg_dir_exists(ad) == 1, "cg_app_dir 指向一个存在的目录");
        check(dd && cg_dir_exists(dd) == 1, "cg_app_data_dir 自动建出来");
        cg_free(ad);
        cg_free(dd);
    }

    /* ---- INI（新建节 / 同节追加 / 改已有项 / 插到节内 / 不破坏别的节） ---- */
    cg_file_delete("kit_test.ini");
    check(cg_ini_write("kit_test.ini", "窗口", "宽", "800") == 0, "cg_ini_write 新建节");
    check(cg_ini_write("kit_test.ini", "窗口", "高", "600") == 0, "cg_ini_write 同节再加一项");
    check(cg_ini_write("kit_test.ini", "窗口", "宽", "1024") == 0, "cg_ini_write 改已有项");
    cg_file_append("kit_test.ini", "[其它]\r\n键=值\r\n");
    check(cg_ini_write("kit_test.ini", "窗口", "标题", "我的程序") == 0, "cg_ini_write 插到节内");
    {
        char *w = cg_ini_read("kit_test.ini", "窗口", "宽", "?");
        char *h = cg_ini_read("kit_test.ini", "窗口", "高", "?");
        char *t = cg_ini_read("kit_test.ini", "窗口", "标题", "?");
        char *o = cg_ini_read("kit_test.ini", "其它", "键", "?");
        char *x = cg_ini_read("kit_test.ini", "窗口", "没有的键", "默认值");
        char *raw = cg_file_read("kit_test.ini");
        check(w && strcmp(w, "1024") == 0, "INI 改过的值读回 1024");
        check(h && strcmp(h, "600") == 0, "INI 第二项读回 600");
        check(t && strcmp(t, "我的程序") == 0, "INI 中文值读回正确");
        check(o && strcmp(o, "值") == 0, "INI 其它节没被破坏");
        check(x && strcmp(x, "默认值") == 0, "INI 缺键返回默认值");
        check(raw && strstr(raw, "[窗口]") && strstr(raw, "[其它]"), "INI 保留了节结构");
        {
            int         cnt = 0;
            const char *p = raw;
            while (p && (p = strstr(p, "宽=")) != NULL) { cnt++; p++; }
            check(cnt == 1, "INI 没有重复写同一项");
            check(raw && strstr(raw, "标题=我的程序\r\n[其它]") != NULL, "插入行没粘住下一个节头");
        }
        printf("---- kit_test.ini ----\n%s----------------------\n", raw ? raw : "(读不到)");
        cg_free(w); cg_free(h); cg_free(t); cg_free(o); cg_free(x); cg_free(raw);
    }

    /* ---- 文本与编码 ---- */
    {
        char *f = cg_str_format("%s=%d（%s）", "数量", 42, "中文");
        char *t = cg_str_trim("  空白\n");
        char *r = cg_str_replace("a-b-c", "-", "+");
        check(f && strcmp(f, "数量=42（中文）") == 0, "cg_str_format");
        check(t && strcmp(t, "空白") == 0, "cg_str_trim");
        check(cg_str_equal("KeyName", "keyname") == 1, "cg_str_equal 忽略大小写");
        check(r && strcmp(r, "a+b+c") == 0, "cg_str_replace");
        cg_free(f); cg_free(t); cg_free(r);
    }
    {
        char *gbk = cg_utf8_to_gbk("中文ABC");
        char *u8  = gbk ? cg_gbk_to_utf8(gbk) : NULL;
        check(gbk && strcmp(gbk, "中文ABC") != 0, "cg_utf8_to_gbk 真的换了字节");
        check(u8 && strcmp(u8, "中文ABC") == 0, "GBK -> UTF-8 往返一致");
        cg_free(gbk);
        cg_free(u8);
    }

    /* ---- 时间 ---- */
    {
        char *now = cg_time_now(NULL);
        check(now && strlen(now) == 19, "cg_time_now 默认格式 YYYY-MM-DD HH:MM:SS");
        cg_free(now);
    }

    /* ---- 剪贴板 ---- */
    check(cg_clip_set_text("CStackGUI 剪贴板测试") == 0, "cg_clip_set_text");
    {
        char *c = cg_clip_get_text();
        check(c && strcmp(c, "CStackGUI 剪贴板测试") == 0, "cg_clip_get_text 往返一致");
        cg_free(c);
    }

    /* ---- 外部程序（cmd /c exit 7 → 退出码 7） ---- */
    check(cg_run_process("cmd /c exit 7", 1) == 7, "cg_run_process 拿到退出码");

    /* ---- 注册表（用完删掉测试键） ---- */
    check(cg_reg_write("Software\\CStackGUITest", "greeting", "你好") == 0, "cg_reg_write");
    {
        char *v = cg_reg_read("Software\\CStackGUITest", "greeting", "?");
        char *d = cg_reg_read("Software\\CStackGUITest", "没有的值", "兜底");
        check(v && strcmp(v, "你好") == 0, "cg_reg_read 读回中文");
        check(d && strcmp(d, "兜底") == 0, "cg_reg_read 缺值返回默认");
        cg_free(v);
        cg_free(d);
    }
    /* ---- 枚举子键：造两个子键再枚举（用完删掉；cg_run_process 不经 cmd，& 无效，逐条跑） ---- */
    cg_run_process("reg add \"HKCU\\Software\\CStackGUITest\\alpha\" /f", 1);
    cg_run_process("reg add \"HKCU\\Software\\CStackGUITest\\beta\" /f", 1);
    cg_run_process("reg add \"HKCU\\Software\\CStackGUITest\\alpha\\command\" /ve /d \"echo hi\" /f", 1);
    {
        char buf[1024];
        int  n = cg_reg_subkeys("Software\\CStackGUITest", buf, sizeof(buf));
        int  got_alpha, got_beta, i = 0, rows = 0;
        check(n > 0, "cg_reg_subkeys 枚举到子键");
        while (i < n) {
            int j = i;
            while (j < n && buf[j] != '\n') j++;
            buf[j] = '\0';
            if (strcmp(&buf[i], "alpha") == 0) got_alpha = 1;
            if (strcmp(&buf[i], "beta")  == 0) got_beta  = 1;
            rows++;
            i = j + 1;
        }
        check(got_alpha && got_beta && rows == 2, "cg_reg_subkeys 两个子键名都枚举到（含中文/无遗漏）");
        check(cg_reg_subkeys("Software\\CStackGUITest\\没有这个键", buf, sizeof(buf)) == 0,
              "cg_reg_subkeys 键不存在返回 0（不是错误）");
    }
    /* ---- 删值 / 删键：契约是"调用后该值/该键一定不存在"（幂等），不是"删到了才成功" ---- */
    check(cg_reg_write("Software\\CStackGUITest", "tmp", "x") == 0, "写一个待删的值");
    check(cg_reg_del("Software\\CStackGUITest", "tmp") == 0, "cg_reg_del 删值返回 0");
    {
        char *v = cg_reg_read("Software\\CStackGUITest", "tmp", "没了");
        check(v && strcmp(v, "没了") == 0, "cg_reg_del 删掉后读不回来");
        cg_free(v);
    }
    check(cg_reg_del("Software\\CStackGUITest", "tmp") == 0, "cg_reg_del 幂等：再删一次照样 0");
    check(cg_reg_del("Software\\CStackGUITest\\没有这个键", "tmp") == 0, "cg_reg_del 键不在也算达成");
    check(cg_reg_del(NULL, "tmp") == -1 && cg_reg_del("a", NULL) == -1, "cg_reg_del 参数为空报 -1");
    check(cg_reg_del_key("Software\\CStackGUITest\\alpha") == 0, "cg_reg_del_key 删键返回 0");
    check(cg_reg_del_key("Software\\CStackGUITest\\alpha") == 0, "cg_reg_del_key 幂等：再删一次照样 0");
    check(cg_reg_del_key(NULL) == -1 && cg_reg_del_key("") == -1,
          "cg_reg_del_key 空参数报 -1（挡住 RegDeleteTree(HKCU, NULL) 那种事故）");
    {
        char buf[1024];
        char *c = cg_reg_read("Software\\CStackGUITest\\alpha\\command", "", NULL);
        check(c == NULL, "cg_reg_del_key 连子键带值一起没了（alpha\\command 读不到）");
        cg_free(c);
        check(cg_reg_subkeys("Software\\CStackGUITest", buf, sizeof(buf)) > 0 && strcmp(buf, "beta\n") == 0,
              "枚举出来只剩 beta");
    }
    cg_run_process("reg delete \"HKCU\\Software\\CStackGUITest\" /f", 1);

    /* ---- 跨根 *_ex：真读 HKLM / HKCR、root 大小写不敏感、认不出的 root 走回退/报错 ----
     * 只做读与"不存在"判断，写系统区那条按是否提权分支，不留垃圾键。 */
    {
        char *ver  = cg_reg_read_ex("HKLM", "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                                    "ProductName", "?");
        char *bad  = cg_reg_read_ex("NOPE", "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                                    "ProductName", "回退");
        char buf[4096];
        check(ver && strcmp(ver, "?") != 0, "cg_reg_read_ex 读到 HKLM 系统区的 ProductName");
        check(bad && strcmp(bad, "回退") == 0, "cg_reg_read_ex 认不出的 root 走回退（不是崩、不是读错根）");
        cg_free(ver);
        cg_free(bad);
        check(cg_reg_subkeys_ex("HKCR", "CLSID", buf, sizeof(buf)) > 0,
              "cg_reg_subkeys_ex 枚举 HKCR\\CLSID（root 大小写不敏感）");
        check(cg_reg_subkeys_ex("NOPE", "CLSID", buf, sizeof(buf)) == 0,
              "cg_reg_subkeys_ex 认不出的 root 返回 0");
        check(cg_reg_key_exists("HKCR", "CLSID\\{00021401-0000-0000-C000-000000000046}") == 1,
              "cg_reg_key_exists HKCR 下快捷方式 CLSID 存在");
        check(cg_reg_key_exists("HKLM", "SOFTWARE\\CStackGUITestNoKey") == 0,
              "cg_reg_key_exists 不存在的键返回 0");
        check(cg_reg_key_exists(NULL, "") == 0 && cg_reg_key_exists("NOPE", "x") == 0,
              "cg_reg_key_exists 空参数 / 非法 root 返回 0");
        check(cg_reg_is_admin() == 0 || cg_reg_is_admin() == 1,
              "cg_reg_is_admin 只回 0 / 1");
        check(cg_reg_write_ex("NOPE", "x", "y", "z") == -1 &&
              cg_reg_del_ex("NOPE", "x", "y") == -1 &&
              cg_reg_del_key_ex("NOPE", "x") == -1,
              "写/删/删键的非法 root 都报 -1（不许静默落到别处）");
    }
    /* ---- 写路径：root 大小写不敏感的用户区往返 + 是否提权决定 HKLM 写入成败 ---- */
    check(cg_reg_write_ex("hKcU", "Software\\CStackGUITest2", "v", "跨根") == 0,
          "cg_reg_write_ex root 大小写不敏感（hKcU）");
    {
        char *v = cg_reg_read_ex(NULL, "Software\\CStackGUITest2", "v", "?");
        check(v && strcmp(v, "跨根") == 0, "root 传 NULL = HKCU（与老函数同义）");
        cg_free(v);
    }
    if (cg_reg_is_admin()) {
        check(cg_reg_write_ex("HKLM", "Software\\CStackGUITest2", "v", "1") == 0,
              "已提权写 HKLM 成功");
        check(cg_reg_del_ex("HKLM", "Software\\CStackGUITest2", "v") == 0,
              "已提权删 HKLM 值成功");
        check(cg_reg_del_key_ex("HKLM", "Software\\CStackGUITest2") == 0,
              "已提权删 HKLM 测试键成功（清干净）");
    } else {
        check(cg_reg_write_ex("HKLM", "Software\\CStackGUITest2", "v", "1") == -1,
              "未提权写 HKLM 明确返回 -1（权限问题不许静默）");
    }
    check(cg_reg_del_key_ex(NULL, "Software\\CStackGUITest2") == 0,
          "cg_reg_del_key_ex 清掉用户区测试键（幂等）");

    printf("\n%s（失败 %d 项）\n", fails ? "kit test FAILED" : "kit test OK", fails);
    return fails ? 1 : 0;
}
