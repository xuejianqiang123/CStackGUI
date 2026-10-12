/**
 * @file    bench-inline.c
 * @brief   A/B micro-benchmark for cross-TU small-function inlining (ns/call).
 * @details Measures cgw_is_t / cgw_to_colorref / cgw_scale_px call cost before and
 *          after moving them into cg_win_internal.h as static inline, plus a loop
 *          control case that must stay flat (guards against measurement noise).
 *          Build (from repo root):
 *            gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -I runtime/include ^
 *                -I runtime/src/core -I runtime/src/win32 -I runtime/src/skin -I runtime/src/kit ^
 *                -c tools/bench-inline.c -o build/_bench_inline.o
 *            gcc -O2 -o build/_bench_inline.exe build/_bench_inline.o build/lib/libcstackgui.a ^
 *                -lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus -lcomdlg32 -luuid ^
 *                -lole32 -loleaut32 -lws2_32 -lstdc++
 *          Run the identical line before and after the change; report min-of-3 ns/call.
 */
#include "cg_win_internal.h"

static volatile unsigned g_sink;

static double now_ns(void)
{
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    return (double)cnt.QuadPart * 1e9 / (double)freq.QuadPart;
}

#define BENCH_N 30000000u
#define ROUNDS  3u

/* runtime-varying state so constant folding cannot collapse the loop */
static unsigned lcg_next(unsigned *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s;
}

/* char* (not const char*): cg_widget::type is char*, assignment must stay warning-free */
static char *k_types[4] = { "button", "edit", "label", "slider" };

static double case_loop(void)
{
    unsigned s = 12345u, acc = 0;
    double t0 = now_ns();
    for (unsigned i = 0; i < BENCH_N; i++) {
        unsigned r = lcg_next(&s);
        acc += k_types[r & 3u][0];
    }
    double t1 = now_ns();
    g_sink = acc;
    return (t1 - t0) / (double)BENCH_N;
}

static double case_is_t(void)
{
    cg_widget_t w;
    unsigned s = 23456u, acc = 0;
    memset(&w, 0, sizeof w);
    double t0 = now_ns();
    for (unsigned i = 0; i < BENCH_N; i++) {
        unsigned r = lcg_next(&s);
        w.type = k_types[r & 3u];
        acc += (unsigned)cgw_is_t(&w, k_types[(r >> 3) & 3u]);
    }
    double t1 = now_ns();
    g_sink = acc;
    return (t1 - t0) / (double)BENCH_N;
}

static double case_colorref(void)
{
    unsigned s = 34567u, acc = 0;
    double t0 = now_ns();
    for (unsigned i = 0; i < BENCH_N; i++) {
        unsigned r = lcg_next(&s);
        acc += cgw_to_colorref(r & 0xFFFFFFu);
    }
    double t1 = now_ns();
    g_sink = acc;
    return (t1 - t0) / (double)BENCH_N;
}

static double case_scale(void)
{
    cg_app_t app;
    unsigned s = 45678u, acc = 0;
    memset(&app, 0, sizeof app);
    app.scale = 125;
    double t0 = now_ns();
    for (unsigned i = 0; i < BENCH_N; i++) {
        unsigned r = lcg_next(&s);
        acc += (unsigned)cgw_scale_px(&app, (int)(r & 1023u) - 512);
    }
    double t1 = now_ns();
    g_sink = acc;
    return (t1 - t0) / (double)BENCH_N;
}

typedef double (*case_fn)(void);

static double best_of(case_fn fn)
{
    double best = -1.0;
    for (unsigned k = 0; k < ROUNDS; k++) {
        double v = fn();
        if (best < 0.0 || v < best) best = v;
    }
    return best;
}

int main(void)
{
    printf("bench-inline  N=%u rounds=%u (min ns/call)\n", BENCH_N, ROUNDS);
    printf("%-14s %10s\n", "case", "ns/call");
    printf("%-14s %10.3f\n", "loop(ctrl)", best_of(case_loop));
    printf("%-14s %10.3f\n", "cgw_is_t", best_of(case_is_t));
    printf("%-14s %10.3f\n", "cgw_to_colorref", best_of(case_colorref));
    printf("%-14s %10.3f\n", "cgw_scale_px", best_of(case_scale));
    printf("sink=%u\n", g_sink);
    return 0;
}
