/**
 * @file    test_json.c
 * @brief   cg_json.c 的单元测试：取值兜底、字符串转义、颜色字面量、错误路径
 * @details 为什么盯 JSON：.cgui 就是 JSON，解析器一旦"悄悄吃掉半个字段"，表现是界面
 *          莫名其妙少东西或尺寸不对，很难反查。所以除了正常路径，这里把"该报错的输入"
 *          也钉住（尾随逗号、括号不闭合、尾随垃圾），免得以后有人把严格校验放松掉。
 */
#include "cg_test.h"
#include "cg_json.h"

#include <string.h>

/**
 * @brief 基本类型、嵌套取值、类型不符时的兜底值
 * @return void 无返回值
 */
static void test_json_values(void)
{
    char      err[128];
    cj_val_t *v;

    cg_test_note("---- 基本类型与取值助手 ----");
    err[0] = '\0';
    v = cj_parse("{\"a\":1,\"b\":\"x\",\"c\":true,\"d\":null,\"e\":[1,2,3],\"f\":{\"g\":2.5}}",
                 err, sizeof(err));
    cg_test_ok("解析混合对象成功", v != NULL);
    if (!v) return;

    cg_test_int("cj_int 取整数", cj_int(cj_get(v, "a"), -1), 1);
    cg_test_str("cj_str 取字符串", cj_str(cj_get(v, "b"), "?"), "x");
    cg_test_int("cj_bool 取 true", cj_bool(cj_get(v, "c"), 0), 1);
    cg_test_int("cj_bool 遇 null 用默认值", cj_bool(cj_get(v, "d"), 7), 7);
    cg_test_int("cj_count 数组长度", (int)cj_count(cj_get(v, "e")), 3);
    cg_test_int("cj_count 对非容器返回 0", (int)cj_count(cj_get(v, "a")), 0);
    cg_test_int("cj_at 取数组第 3 项", cj_int(cj_at(cj_get(v, "e"), 2), -1), 3);
    cg_test_int("cj_at 越界给默认值", cj_int(cj_at(cj_get(v, "e"), 99), -1), -1);
    cg_test_int("cj_at 对非数组返回 NULL", cj_at(cj_get(v, "a"), 0) == NULL, 1);

    cg_test_note("---- 取值助手对类型不符的处理 ----");
    cg_test_ok("嵌套对象类型正确", cj_get(v, "f") != NULL && cj_get(v, "f")->type == CJ_OBJ);
    cg_test_int("cj_int 对 2.5 四舍五入", cj_int(cj_get(cj_get(v, "f"), "g"), -1), 3);
    cg_test_int("cj_str 对数字用默认值", strcmp(cj_str(cj_get(v, "a"), "def"), "def") == 0, 1);
    cg_test_int("cj_int 对非数字串用默认值", cj_int(cj_get(v, "b"), 42), 42);
    cg_test_int("cj_get 缺键返回 NULL", cj_get(v, "没有的键") == NULL, 1);
    cg_test_int("cj_get 对 NULL 返回 NULL", cj_get(NULL, "a") == NULL, 1);
    cg_test_int("cj_at 对 NULL 返回 NULL", cj_at(NULL, 0) == NULL, 1);
    cj_free(v);

    cg_test_note("---- 字符串形式的数字 / 布尔（画布有时写成字符串） ----");
    v = cj_parse("{\"n\":\"3.5\",\"t\":\"true\",\"z\":\"0\",\"m\":\"maybe\"}", err, sizeof(err));
    cg_test_ok("解析字符串数字成功", v != NULL);
    if (v) {
        cg_test_int("cj_num 认 \"3.5\"", (long long)(cj_num(cj_get(v, "n"), -1) * 10), 35);
        cg_test_int("cj_bool 认 \"true\"", cj_bool(cj_get(v, "t"), -1), 1);
        cg_test_int("cj_bool 认 \"0\"", cj_bool(cj_get(v, "z"), -1), 0);
        cg_test_int("cj_bool 认不出的串用默认值", cj_bool(cj_get(v, "m"), -1), -1);
        cj_free(v);
    }
}

/**
 * @brief 转义、Unicode 转义、代理对、字节序标记
 * @return void 无返回值
 */
static void test_json_strings(void)
{
    char      err[128];
    cj_val_t *v;

    cg_test_note("---- 字符串转义与编码 ----");
    v = cj_parse("{\"q\":\"a\\\"b\\\\c\\nd\",\"zh\":\"\\u4e2d\\u6587\",\"emo\":\"\\ud83d\\ude00\"}",
                 err, sizeof(err));
    cg_test_ok("解析带转义的字符串成功", v != NULL);
    if (v) {
        cg_test_str("引号 / 反斜杠 / 换行", cj_str(cj_get(v, "q"), "?"), "a\"b\\c\nd");
        cg_test_str("\\uXXXX 解成中文", cj_str(cj_get(v, "zh"), "?"), "中文");
        cg_test_str("代理对解成 4 字节 UTF-8", cj_str(cj_get(v, "emo"), "?"), "\xF0\x9F\x98\x80");
        cj_free(v);
    }

    cg_test_note("---- 空串与 BOM ----");
    v = cj_parse("{\"s\":\"\"}", err, sizeof(err));
    cg_test_ok("解析空串字段成功", v != NULL);
    if (v) {
        cg_test_str("空串读回空串（不是 NULL 崩掉）", cj_str(cj_get(v, "s"), "?"), "");
        cj_free(v);
    }
    v = cj_parse("\xEF\xBB\xBF{\"a\":1}", err, sizeof(err));
    cg_test_ok("带 UTF-8 BOM 的文件能解析", v != NULL);
    if (v) {
        cg_test_int("BOM 不影响第一个字段", cj_int(cj_get(v, "a"), -1), 1);
        cj_free(v);
    }
}

/**
 * @brief 颜色字面量的各种写法
 * @return void 无返回值
 */
static void test_json_colors(void)
{
    char      err[128];
    cj_val_t *v;

    cg_test_note("---- 颜色字面量 ----");
    v = cj_parse("{\"a\":\"#F3EDF7\",\"b\":\"#F0C\",\"c\":16711680,\"d\":\"#80FF0000\",\"e\":\"#GGGGGG\"}",
                 err, sizeof(err));
    cg_test_ok("解析颜色字段成功", v != NULL);
    if (!v) return;

    cg_test_int("cj_color 认 #RRGGBB", (long long)cj_color(cj_get(v, "a"), 0), 0xF3EDF7);
    cg_test_int("cj_color 认 #RGB 并展开", (long long)cj_color(cj_get(v, "b"), 0), 0xFF00CC);
    cg_test_int("cj_color 认十进制数", (long long)cj_color(cj_get(v, "c"), 0), 0xFF0000);
    cg_test_int("cj_color 认 8 位含透明度写法", (long long)cj_color(cj_get(v, "d"), 0), 0x80FF0000u);
    cg_test_int("cj_color 遇非法字符用默认色", (long long)cj_color(cj_get(v, "e"), 0x123456), 0x123456);
    cg_test_int("cj_color 遇 NULL 用默认色", (long long)cj_color(NULL, 0x123456), 0x123456);
    cj_free(v);
}

/**
 * @brief 该报错的输入必须报错，且要写错误信息
 * @return void 无返回值
 */
static void test_json_errors(void)
{
    char err[256];

    cg_test_note("---- 该拒绝的输入（严格 JSON，不容错） ----");
    err[0] = '\0';
    cg_test_int("尾随逗号被拒绝", cj_parse("{\"a\":1,}", err, sizeof(err)) == NULL, 1);
    cg_test_int("失败时写了错误信息", err[0] != '\0', 1);
    cg_test_int("括号不闭合被拒绝", cj_parse("{\"a\":1", err, sizeof(err)) == NULL, 1);
    cg_test_int("空文本被拒绝", cj_parse("", err, sizeof(err)) == NULL, 1);
    cg_test_int("NULL 文本被拒绝", cj_parse(NULL, err, sizeof(err)) == NULL, 1);
    cg_test_int("字符串没闭合被拒绝", cj_parse("{\"a\":\"x}", err, sizeof(err)) == NULL, 1);
    cg_test_int("对象后面有垃圾被拒绝", cj_parse("{\"a\":1} 尾巴", err, sizeof(err)) == NULL, 1);
    cg_test_int("键后面缺冒号被拒绝", cj_parse("{\"a\" 1}", err, sizeof(err)) == NULL, 1);

    cg_test_note("---- 合法但不像 .cgui 的输入由上层判定 ----");
    {
        cj_val_t *v = cj_parse("[1,2]", err, sizeof(err));
        cg_test_int("顶层数组是合法 JSON（cg_doc_parse 才该拒绝它）", v != NULL, 1);
        cj_free(v);
    }
    {
        cj_val_t *v = cj_parse("{}", err, sizeof(err));
        cg_test_int("空对象能解析", v != NULL, 1);
        cg_test_int("空对象长度 0", (int)cj_count(v), 0);
        cj_free(v);
    }
    {
        cj_val_t *v = cj_parse("{\"a\":\"\\uZZZZ\"}", err, sizeof(err));
        cg_test_int("坏 \\u 转义被拒绝", v == NULL, 1);
        cj_free(v);
    }
}

CG_TEST_SUITE("JSON 取值与兜底", test_json_values);
CG_TEST_SUITE("JSON 字符串与编码", test_json_strings);
CG_TEST_SUITE("JSON 颜色字面量", test_json_colors);
CG_TEST_SUITE("JSON 错误路径", test_json_errors);
