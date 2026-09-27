/**
 * @file    test_appinfo.c
 * @brief   窗口级事件解析（app.events）+ 程序配置（app.info）
 * @details 为什么单开一个文件：2026-09-23 的运行时排查发现这两处是**零断言**的空白区 ——
 *          改坏任何一处，单测、格式回归、各示例的 --selftest 全都照样绿：
 *            · `app.events` 的解析结果写在"虚拟根节点"的 ev[] 上（cg_doc.c），而
 *              --selftest 对窗口事件**只核对回调名字有没有登记、不做合成触发**（cg_app.c），
 *              所以名字绑丢了它也不会响；
 *            · `cg_app_info()` 的 14 个键到 cg_info_t 字段是一长串 strcmp（cg_app.c），
 *              把 fax 写成 phone 这种交叉接线，同样没有任何东西会红。
 *          这里就把这两条钉住：键名错了、接错线了，立刻红。
 */
#include "cg_test.h"
#include "cg_internal.h"     /* 文档模型 / 控件 / app 的内部结构（公开头只有 API） */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 一、窗口级事件：app.events -> 虚拟根节点上的 ev[]                    */
/* ------------------------------------------------------------------ */

static const char *k_win_events_doc =
    "{ \"app\": { \"title\": \"事件测试\","
    "    \"events\": { \"ready\": \"on_ready\", \"size\": \"on_size\","
    "                  \"keydown\": \"on_key\", \"wheel\": \"on_wheel\","
    "                  \"close\": \"on_close\" } },"
    "  \"widgets\": [] }";

/**
 * @brief 窗口级事件要落在虚拟根的对应字段上
 * @return void 无返回值
 */
static void test_window_events_parse(void)
{
    char      err[256] = { 0 };
    cg_doc_t *doc;

    cg_test_note("---- 窗口级事件（app.events）落在虚拟根的 ev[] 上 ----");
    doc = cg_doc_parse(k_win_events_doc, err, sizeof(err));
    cg_test_ok("解析成功", doc != NULL);
    if (!doc) {
        printf("        解析失败：%s\n", err);
        return;
    }

    /* 用 cg_event_from_name() 取下标，而不是硬写枚举常量：顺带把"事件名 -> 枚举"这条
     * 链路也一起断言了（名字拼错时这里就会红，而不是悄悄绑到别的下标上）。 */
    cg_test_str("ready",   doc->root->ev[cg_event_from_name("ready")],   "on_ready");
    cg_test_str("size",    doc->root->ev[cg_event_from_name("size")],    "on_size");
    cg_test_str("keydown", doc->root->ev[cg_event_from_name("keydown")], "on_key");
    cg_test_str("wheel",   doc->root->ev[cg_event_from_name("wheel")],   "on_wheel");
    cg_test_str("close",   doc->root->ev[cg_event_from_name("close")],   "on_close");
    cg_test_ok("没绑的窗口事件（tray）保持 NULL",
               doc->root->ev[cg_event_from_name("tray")] == NULL);

    cg_doc_free(doc);
}

/* ------------------------------------------------------------------ */
/* 二、程序配置：app.info 的 14 个键 -> cg_info_t 字段 -> cg_app_info() */
/* ------------------------------------------------------------------ */

/* 14 个键每个给**互不相同**的值：任何一处交叉接线（比如 fax 接到 phone）
 * 都会有两个断言同时红，一眼看得出接线断在哪。 */
static const char *k_info_doc =
    "{ \"app\": { \"info\": {"
    "    \"product\": \"产品名\", \"desc\": \"描述\", \"comments\": \"备注\","
    "    \"version\": \"1.2.3.4\", \"build\": \"20260923\", \"author\": \"作者\","
    "    \"company\": \"公司\", \"copyright\": \"版权\", \"address\": \"地址\","
    "    \"postcode\": \"邮编\", \"phone\": \"电话\", \"fax\": \"传真\","
    "    \"email\": \"邮箱\", \"homepage\": \"主页\" } },"
    "  \"widgets\": [] }";

/**
 * @brief 程序配置的 14 个键各自接到对应字段
 * @return void 无返回值
 */
static void test_app_info(void)
{
    char      err[256] = { 0 };
    cg_doc_t *doc;
    cg_app_t  app;

    cg_test_note("---- app.info 的 14 个键（cg_app_info）----");
    doc = cg_doc_parse(k_info_doc, err, sizeof(err));
    cg_test_ok("解析成功", doc != NULL);
    if (!doc) {
        printf("        解析失败：%s\n", err);
        return;
    }

    memset(&app, 0, sizeof(app));
    app.scale = 1;
    app.doc   = *doc;      /* app->doc 是内嵌结构，所有权转移（与 test_bind.c 同写法） */
    free(doc);

    cg_test_str("product",   cg_app_info(&app, "product"),   "产品名");
    cg_test_str("desc",      cg_app_info(&app, "desc"),      "描述");
    cg_test_str("comments",  cg_app_info(&app, "comments"),  "备注");
    cg_test_str("version",   cg_app_info(&app, "version"),   "1.2.3.4");
    cg_test_str("build",     cg_app_info(&app, "build"),     "20260923");
    cg_test_str("author",    cg_app_info(&app, "author"),    "作者");
    cg_test_str("company",   cg_app_info(&app, "company"),   "公司");
    cg_test_str("copyright", cg_app_info(&app, "copyright"), "版权");
    cg_test_str("address",   cg_app_info(&app, "address"),   "地址");
    cg_test_str("postcode",  cg_app_info(&app, "postcode"),  "邮编");
    cg_test_str("phone",     cg_app_info(&app, "phone"),     "电话");
    cg_test_str("fax",       cg_app_info(&app, "fax"),       "传真");
    cg_test_str("email",     cg_app_info(&app, "email"),     "邮箱");
    cg_test_str("homepage",  cg_app_info(&app, "homepage"),  "主页");
    cg_test_ok("不认识的键返回 NULL", cg_app_info(&app, "没有这个键") == NULL);
    cg_test_ok("NULL 参数不崩", cg_app_info(NULL, "product") == NULL);

    /* 注意口径：cg_doc_free() = cg_doc_release() + free(结构体本身)，
     * 而 app->doc 是**内嵌**在 app 里的（栈上）—— 对它只能用 release，
     * 用 free 就是 free 栈地址（首版实测：堆损坏 0xC0000374）。 */
    cg_doc_release(&app.doc);
}

CG_TEST_SUITE("文档：窗口级事件解析", test_window_events_parse);
CG_TEST_SUITE("文档：程序配置 app.info", test_app_info);
