/**
 * @file    test_kit_sys.c
 * @brief   cg_kit 系统信息：环境变量、系统位数与版本、磁盘容量、进程是否在跑、本机 IP
 * @details 这些函数的结果**随机器而变**，所以断言钉的是**性质**而不是具体值：
 *          存在的变量要读得到、不存在的必须给 NULL（不是空串）、剩余空间不能大于总量、
 *          自己这个测试进程必须能被查出来在跑（这一条同时证明"枚举"与"比名字"两半都对）。
 *
 *          唯一敢写死的值来自两处**不变量**：
 *          ① Windows 自身的约定（NUMBER_OF_PROCESSORS 一定 >= 1）；
 *          ② "能跑起本测试就必然成立"的前提 —— 本工程编出来的是 64 位 exe，而 64 位进程
 *             只可能在 64 位 Windows 上跑，所以 cg_sys_is64() 必然是 1。
 */
#include "cg_test.h"
#include "cstackgui.h"

#include <string.h>

/** @brief 环境变量：精易那 34 条命令在 C 侧就是这一个函数 */
/**
 * @brief 环境变量：那一大堆旧代码里的环境命令在 C 侧就是这一个函数
 * @return void 无返回值
 */
static void test_env(void)
{
    char *v;
    int   n;

    cg_test_note("---- 环境变量 ----");
    v = cg_env("PATH");
    cg_test_ok("PATH 读得到（非空）", v && *v);
    cg_free(v);

    v = cg_env("COMPUTERNAME");                    /* 对应精易的 系统_COMPUTERNAME */
    cg_test_ok("COMPUTERNAME 读得到（计算机名）", v && *v);
    cg_free(v);

    v = cg_env("USERNAME");                        /* 对应 系统_USERNAME */
    cg_test_ok("USERNAME 读得到（用户名）", v && *v);
    cg_free(v);

    v = cg_env("TEMP");                            /* 对应 系统_TEMP */
    cg_test_ok("TEMP 读得到（临时目录）", v && *v);
    cg_free(v);

    v = cg_env("path");                            /* Windows 环境变量名字本就不分大小写 */
    cg_test_ok("\"path\" 与 \"PATH\" 是同一个变量", v && *v);
    cg_free(v);

    cg_test_ok("不存在的变量 -> NULL（不是空串）", cg_env("__CG_NO_SUCH_VAR__") == NULL);
    cg_test_ok("NULL 名字 -> NULL", cg_env(NULL) == NULL);
    cg_test_ok("空名字 -> NULL", cg_env("") == NULL);

    cg_test_note("---- 当整数读（atoi 会把 \"abc\" 当成 0，所以这里整串必须合法） ----");
    n = cg_env_int("NUMBER_OF_PROCESSORS", -1);
    cg_test_ok("NUMBER_OF_PROCESSORS >= 1", n >= 1);
    cg_test_int("不存在的变量 -> fallback", cg_env_int("__CG_NO_SUCH_VAR__", 42), 42);
    cg_test_int("值不是数字 -> fallback（不是 0）", cg_env_int("PATH", 42), 42);
}

/** @brief 系统位数与版本串 */
/**
 * @brief 系统位数与版本串
 * @return void 无返回值
 */
static void test_sys(void)
{
    char *v;

    cg_test_note("---- 系统位数与版本 ----");
    /* 64 位 exe 跑不起来 32 位 Windows，所以这条是构造性的不变量 */
    cg_test_int("本机是 64 位 Windows", cg_sys_is64(), 1);

    v = cg_sys_version();
    cg_test_ok("系统版本串非空", v && *v);
    cg_test_ok("  以 \"Windows\" 开头", v && strncmp(v, "Windows", 7) == 0);
    cg_test_ok("  带版本号括号（形如 \"Windows 10 (10.0.19045)\"）", v && strchr(v, '(') != NULL
                                                                     && strchr(v, ')') != NULL);
    if (v) cg_test_note(v);
    cg_free(v);
}

/** @brief 磁盘容量与进程存在性、本机 IP */
/**
 * @brief 磁盘容量与进程存在性
 * @return void 无返回值
 */
static void test_disk_and_process(void)
{
    unsigned long long free_b = 0, total_b = 0;
    char              *dir;

    cg_test_note("---- 磁盘容量（写日志前问「还写得下吗」） ----");
    dir = cg_app_dir();
    cg_test_int("程序目录所在盘取得到", cg_disk_space(dir, &free_b, &total_b), 1);
    cg_test_ok("总量 > 0", total_b > 0);
    cg_test_ok("剩余 <= 总量", free_b <= total_b);
    cg_test_int("只要剩余（总量传 NULL）也成功", cg_disk_space(dir, &free_b, NULL), 1);
    cg_test_int("只要总量（剩余传 NULL）也成功", cg_disk_space(dir, NULL, &total_b), 1);
    cg_test_int("路径传 NULL -> 0", cg_disk_space(NULL, &free_b, NULL), 0);
    cg_test_int("空路径 -> 0", cg_disk_space("", &free_b, NULL), 0);
    cg_free(dir);

    cg_test_note("---- 进程在不在（现场排查服务 / 驱动起没起） ----");
    /* 自己肯定在跑：这条同时证明"枚举进程"和"比名字"两半都对。
     * 名字来自 tools\run-tests.bat 的 -o build\tests\cgu-tests.exe。 */
    cg_test_int("自己（cgu-tests.exe）查得到", cg_process_running("cgu-tests.exe"), 1);
    cg_test_int("大小写不敏感（CGU-TESTS.EXE）", cg_process_running("CGU-TESTS.EXE"), 1);
    cg_test_int("不存在的进程 -> 0", cg_process_running("__no_such_process__.exe"), 0);
    cg_test_int("名字传 NULL -> 0", cg_process_running(NULL), 0);
    cg_test_int("空名字 -> 0", cg_process_running(""), 0);

    cg_test_note("---- 本机 IP（挑一条默认路由再问本地端地址，不发包、不查 DNS） ----");
    dir = cg_local_ip();
    if (dir) {
        int dots = 0, i;

        for (i = 0; dir[i]; i++) {
            if (dir[i] == '.') dots++;
        }
        cg_test_int("是点分十进制 IPv4（3 个点）", dots, 3);
        cg_test_ok("不是回环地址（回环如实报 NULL）", strcmp(dir, "127.0.0.1") != 0);
        cg_test_note(dir);
        cg_free(dir);
    } else {
        /* 离线机器 / 没有默认路由时拿不到 —— 那时返回 NULL 才是对的，不算失败 */
        cg_test_note("这台机器目前拿不到本机 IP（离线或无默认路由）—— 按契约不算失败");
    }
}

CG_TEST_SUITE("kit：环境变量", test_env);
CG_TEST_SUITE("kit：系统位数与版本", test_sys);
CG_TEST_SUITE("kit：磁盘容量 / 进程是否在跑 / 本机 IP", test_disk_and_process);
