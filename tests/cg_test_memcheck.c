/**
 * @file    cg_test_memcheck.c
 * @brief   --memcheck 模式的内存记账：链接期拦截 malloc/calloc/realloc/free
 * @details 只在 tools\run-tests.bat --memcheck 时编译进测试程序（其余时候这个文件整个是空的）。
 *
 *          为什么不用 ASan / valgrind：本机 MinGW-w64 没有 libasan（实测
 *          "ld: cannot find -lasan"），Windows 上也没有 valgrind 可用。
 *          用 ld 的 --wrap 拦截就够抓"漏 free"这类问题：测试程序（含静态链接进来的
 *          运行时库）自己调用的 malloc/free 会被转到 __wrap_* 上。
 *
 *          局限（所以要允许"判定不可信"）：
 *          - 只看得到测试程序自己发起的分配；系统 DLL 内部自己的分配看不到；
 *          - 若某处用了本文件没拦的分配函数，计数会失真。
 *          因此 cg_test_main.c 遇到"释放比分配多"时不硬判泄漏，只提示不可信。
 */
#ifdef CG_TEST_MEMCHECK

#include <stddef.h>

/**
 * @brief 链接器换名后的真实 malloc（供包装函数转调）
 * @param size 要分配的字节数
 * @return void* 系统分配结果
 */
void *__real_malloc(size_t size);
/**
 * @brief 链接器换名后的真实 calloc（供包装函数转调）
 * @param count 元素个数
 * @param size 每个元素的字节数
 * @return void* 系统分配结果
 */
void *__real_calloc(size_t count, size_t size);
/**
 * @brief 链接器换名后的真实 realloc（供包装函数转调）
 * @param ptr 要调整大小的块
 * @param size 新的大小（字节）
 * @return void* 系统分配结果
 */
void *__real_realloc(void *ptr, size_t size);
/**
 * @brief 链接器换名后的真实 free（供包装函数转调）
 * @param ptr 要释放的块
 * @return void 无返回值
 */
void  __real_free(void *ptr);
/**
 * @brief 链接器换名后的真实字符串复制（供包装函数转调）
 * @param s 要复制的字符串
 * @return char* 系统分配结果
 */
char *__real__strdup(const char *s);

/** 当前存活块数：分配 +1，释放 -1（free(NULL) 不算，与分配侧对称） */
static long s_live;

/**
 * @brief 当前存活堆块数（供测试运行器逐套件取快照）
 * @return int 当前存活的堆块数
 */
int cg_test_mem_live(void)
{
    return (int)s_live;
}

/**
 * @brief 拦 malloc：存活块 +1 后再转给真实 malloc
 * @param size 要分配的字节数
 * @return void* 真实 malloc 的结果，失败为 NULL
 */
void *__wrap_malloc(size_t size)
{
    s_live++;
    return __real_malloc(size);
}

/**
 * @brief 拦 calloc：存活块 +1 后再转给真实 calloc
 * @param count 元素个数
 * @param size 每个元素的字节数
 * @return void* 真实 calloc 的结果，失败为 NULL
 */
void *__wrap_calloc(size_t count, size_t size)
{
    s_live++;
    return __real_calloc(count, size);
}

/**
 * @brief 拦 realloc：只有原指针为 NULL 时才当一次新分配
 * @param ptr 要调整大小的块，为 NULL 时等价于一次新分配
 * @param size 新的大小（字节）
 * @return void* 真实 realloc 的结果，失败为 NULL
 * @note 原指针非 NULL 时是"原地替换"（旧块释放、新块分配），净值 0；
 *       失败时原块还在，同样净值 0。只有原指针为 NULL 才等于一次新分配
 */
void *__wrap_realloc(void *ptr, size_t size)
{
    if (!ptr) s_live++;
    return __real_realloc(ptr, size);
}

/**
 * @brief 拦 free：非 NULL 才把存活块 -1，再转给真实 free
 * @param ptr 要释放的块，可为 NULL
 * @return void 无返回值
 */
void __wrap_free(void *ptr)
{
    if (ptr) s_live--;
    __real_free(ptr);
}

/**
 * @brief 拦字符串复制：入参非 NULL 才把存活块 +1，再转给真实实现
 * @param s 要复制的字符串，可为 NULL
 * @return char* 真实实现的结果，失败为 NULL
 */
char *__wrap__strdup(const char *s)
{
    if (s) s_live++;
    return __real__strdup(s);
}

#endif /* CG_TEST_MEMCHECK */
