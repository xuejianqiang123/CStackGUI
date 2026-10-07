/**
 * @file    cgbuild.c
 * @brief   编译内核：把一个界面工程编成独立 exe（本工程编译逻辑的**唯一实现**）
 * @details 为什么是 C 而不是 bat / Node：
 *
 *   - bat：cmd 用"cmd 自己启动时"的控制台代码页解码批处理，UTF-8 中文会被从
 *     多字节字符中间切断、后半段被当成命令执行（`'lery' is not recognized`）。
 *     而且客户机器上代码页不可控。
 *   - Node：客户只装 MinGW，不能要求 Node.js。
 *   - C：客户只要有 MinGW 就能用。内核自己也是 gcc 一行编出来的（自举）。
 *
 *   四个入口全部转发到本程序，实现只有一份：
 *     - 项目根 build-app.bat        （ASCII 薄壳，必要时先自举编内核）
 *     - 画布 F5                     直接 spawn 本 exe，不经 cmd / bat
 *     - CStackGUI.exe --build <工程> 无窗口 CLI，发布包的 build-app.bat 转发到它
 *     - build-runtime.bat            ASCII 薄壳，--runtime-only
 *
 * 用法：cgbuild.exe [工程名|路径] [--release] [--runtime-only]
 * 环境变量：
 *   MINGW                    MinGW-w64 的 bin 目录（默认 C:\mingw64\bin）
 *   CSTACKGUI_HOME           项目根；缺省时从本 exe 所在目录往上找 runtime\
 *   CSTACKGUI_SKIP_RUNTIME=1 跳过刷新运行时库（build-all.bat 已刷过一次时用）
 * 退出码：0 成功；非 0 失败（不建窗口、不暂停）
 *
 * 构建：gcc -O2 -o build\cgbuild.exe tools\cgbuild.c
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <direct.h>
#include <windows.h>

#define MAXP 1024
#define MAXF 256

static char ROOT[MAXP];

/* ------------------------------------------------------------------ 小工具 */

static void die(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fputs("[fail] ", stderr);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
  exit(1);
}

/** 带上限的字符串拷贝（不用 snprintf：它对"源长度未知"会报 -Wformat-truncation） */
static void scopy(char *dst, size_t cap, const char *src) {
  size_t n;
  if (!cap) return;
  n = strlen(src);
  if (n >= cap) n = cap - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

/** a/b —— 只做拼接，不做规范化（所有路径都是我们自己拼出来的）。
 *  允许 out == a：snprintf 的 out 带 restrict，自拼接必须是先在临时区拼好。 */
static void jp(char *out, const char *a, const char *b) {
  if (out == a) {
    char t[MAXP];
    int n = snprintf(t, sizeof t, "%s/%s", a, b);
    if (n >= MAXP) die("path too long: %s / %s", a, b);
    memcpy(out, t, (size_t)n + 1);
    return;
  }
  if (snprintf(out, MAXP, "%s/%s", a, b) >= MAXP) die("path too long: %s / %s", a, b);
}

static int is_dir(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}
static int exists(const char *p) {
  struct stat st;
  return stat(p, &st) == 0;
}
static double mt(const char *p) {
  struct stat st;
  return stat(p, &st) == 0 ? (double)st.st_mtime : -1e300;
}

/** 递归建目录（_mkdir 只建一层） */
static void mkdir_p(const char *dir) {
  char tmp[MAXP];
  snprintf(tmp, sizeof tmp, "%s", dir);
  for (char *p = tmp + 3; *p; p++) {          /* 跳过盘符 "C:/" */
    if (*p == '/' || *p == '\\') {
      char c = *p;
      *p = '\0';
      _mkdir(tmp);
      *p = c;
    }
  }
  _mkdir(tmp);
}

static void mkdir_for(const char *file) {
  char tmp[MAXP], *slash;
  snprintf(tmp, sizeof tmp, "%s", file);
  slash = strrchr(tmp, '/');
  if (!slash) slash = strrchr(tmp, '\\');
  if (slash) { *slash = '\0'; mkdir_p(tmp); }
}

/** 列出 dir 下以 ext 结尾的文件（排序后返回），ext 形如 ".c" */
static int list_ext(const char *dir, const char *ext, char out[MAXF][MAXP]) {
  DIR *d;
  struct dirent *e;
  int n = 0, i, j;
  if (!(d = opendir(dir))) return 0;
  while ((e = readdir(d)) && n < MAXF) {
    const char *nm = e->d_name;
    size_t ln = strlen(nm), el = strlen(ext);
    if (ln <= el || _stricmp(nm + ln - el, ext) != 0) continue;
    jp(out[n], dir, nm);
    n++;
  }
  closedir(d);
  for (i = 1; i < n; i++) {                    /* 插入排序：保证顺序稳定 */
    char tmp[MAXP];
    snprintf(tmp, sizeof tmp, "%s", out[i]);
    for (j = i - 1; j >= 0 && strcmp(out[j], tmp) > 0; j--) snprintf(out[j + 1], MAXP, "%s", out[j]);
    snprintf(out[j + 1], MAXP, "%s", tmp);
  }
  return n;
}

/** 路径最后一段（同时认 / 和 \），尾部斜杠由调用方先去掉 */
static void base_name(const char *p, char *out) {
  const char *b = strrchr(p, '\\');
  const char *b2 = strrchr(p, '/');
  if (!b) b = b2;
  if (b2 && b2 > b) b = b2;
  scopy(out, MAXP, b ? b + 1 : p);
}

static double newest_of(char files[MAXF][MAXP], int n) {
  double t = -1e300;
  for (int i = 0; i < n; i++) { double m = mt(files[i]); if (m > t) t = m; }
  return t;
}

/** 跑一条命令，输出透传；非 0 退出码直接终止（返回码就是内核的返回码） */
static void run(const char *cmd) {
  int rc;
  fflush(stdout);
  if ((rc = system(cmd)) != 0) exit(rc == -1 ? 1 : rc);
}

/** 内容相同就不覆盖 —— 覆盖会把目标时间刷成"现在"，
 *  于是它永远比 exe 新，"没变就跳过"的判定就永不生效。 */
static void copy_if_changed(const char *src, const char *dst) {
  FILE *a, *b;
  if (!exists(src)) die("missing generated file: %s", src);
  mkdir_for(dst);
  if (exists(dst) && (a = fopen(src, "rb")) && (b = fopen(dst, "rb"))) {
    int same = 1;
    for (;;) {
      char ba[65536], bb[65536];
      size_t na = fread(ba, 1, sizeof ba, a), nb = fread(bb, 1, sizeof bb, b);
      if (na != nb || (na && memcmp(ba, bb, na) != 0)) { same = 0; break; }
      if (!na) break;
    }
    fclose(a); fclose(b);
    if (same) return;
  }
  {
    char cmd[MAXP * 2 + 64];
    snprintf(cmd, sizeof cmd, "copy /y \"%s\" \"%s\" >nul", src, dst);
    run(cmd);
  }
}

/* ------------------------------------------------------------------ 工具链 */

static void setup_toolchain(void) {
  const char *mingw = getenv("MINGW");
  if (!mingw || !*mingw) mingw = "C:\\mingw64\\bin";
  /* 把 MinGW 的 bin 顶到 PATH 最前面。
   * 不能用 cmd 的 `set "PATH=...;%PATH%"`（system() 是一条新 cmd，改不动本进程），
   * 也不能给 _putenv 传固定大小的缓冲 —— PATH 本身可能就超过 1 KB。
   * 这里现拼一块再交给 SetEnvironmentVariableA（它负责拷贝），子进程继承。 */
  {
    const char *old = getenv("PATH");
    size_t need = strlen(mingw) + 1 + (old ? strlen(old) : 0) + 1;
    char *newp = (char *)malloc(need);
    if (!newp) die("out of memory while extending PATH");
    snprintf(newp, need, "%s;%s", mingw, old ? old : "");
    if (!SetEnvironmentVariableA("PATH", newp)) die("cannot prepend MINGW to PATH");
    free(newp);
  }
  if (system("where gcc >nul 2>nul") != 0 || system("where windres >nul 2>nul") != 0 ||
      system("where ar >nul 2>nul") != 0)
    die("gcc/ar/windres not found. Install MinGW-w64, or set the MINGW env var to its bin dir,\n"
        "        e.g.:  set MINGW=D:\\mingw64\\bin\n        (looked in: %s and the rest of PATH)", mingw);
}

/** 本 exe 往上找带 runtime\include\cstackgui.h 的那一层 */
static void find_root(void) {
  const char *env = getenv("CSTACKGUI_HOME");
  char exe[MAXP], dir[MAXP], probe[MAXP], *slash;
  if (env && *env) { snprintf(ROOT, sizeof ROOT, "%s", env); return; }
  if (!GetModuleFileNameA(NULL, exe, sizeof exe)) die("cannot locate cgbuild.exe");
  snprintf(dir, sizeof dir, "%s", exe);
  for (;;) {
    slash = strrchr(dir, '\\');
    if (!slash) slash = strrchr(dir, '/');
    if (!slash) break;
    *slash = '\0';
    jp(probe, dir, "runtime/include/cstackgui.h");
    if (exists(probe)) { snprintf(ROOT, sizeof ROOT, "%s", dir); return; }
  }
  die("cannot find the CStackGUI root (a dir holding runtime\\). Set CSTACKGUI_HOME.");
}

/* ------------------------------------------------------------------ 运行时库 */

static void refresh_clangd(void) {
  char gen[MAXP], cmd[MAXP * 2];
  jp(gen, ROOT, "tools/gen-cc.mjs");
  if (!exists(gen)) return;
  snprintf(cmd, sizeof cmd, "where node >nul 2>nul && node \"%s\" >nul 2>nul", gen);
  system(cmd);                                 /* 没装 node 静默跳过 */
}

static void build_runtime(void) {
  char objdir[MAXP], libdir[MAXP], obj[MAXP], rc[MAXP], ico[MAXP], reso[MAXP], lib[MAXP];
  char hdrs[MAXF][MAXP], objs[MAXF][MAXP];
  int nobjs = 0, changed = 0;
  double hdrT;
  const char *layers[] = {"core", "win32", "skin", "kit"};
  int i, l;

  jp(objdir, ROOT, "build/obj");
  jp(libdir, ROOT, "build/lib");
  mkdir_p(objdir);
  mkdir_p(libdir);

  /* 改头文件必须让所有目标文件失效：这些头声明了整个库赖以构建的结构体布局与宏 */
  int nh = list_ext("runtime/include", ".h", hdrs);
  hdrT = newest_of(hdrs, nh);
  for (l = 0; l < 4; l++) {
    char d[MAXP], tmp[MAXF][MAXP];
    jp(d, ROOT, "runtime/src");
    jp(d, d, layers[l]);
    int n = list_ext(d, ".h", tmp);
    double t = newest_of(tmp, n);
    if (t > hdrT) hdrT = t;
  }

  for (l = 0; l < 4; l++) {
    char d[MAXP], files[MAXF][MAXP];
    jp(d, ROOT, "runtime/src");
    jp(d, d, layers[l]);
    int n = list_ext(d, ".c", files);
    for (i = 0; i < n; i++) {
      char cmd[MAXP * 2];
      const char *base = strrchr(files[i], '\\');
      const char *base2 = strrchr(files[i], '/');
      if (!base) base = base2;
      if (base2 && base2 > base) base = base2;
      base = base ? base + 1 : files[i];
      {
        char stem[MAXP];
        scopy(stem, sizeof stem, base);
        stem[strlen(stem) - 2] = '\0';         /* 去掉 ".c" */
        jp(obj, objdir, stem);
        strncat(obj, ".o", MAXP - strlen(obj) - 1);
      }
      snprintf(objs[nobjs++], MAXP, "%s", obj);
      if (mt(obj) > mt(files[i]) && mt(obj) > hdrT) continue;
      printf("  CC runtime/src/%s/%s\n", layers[l], base);
      snprintf(cmd, sizeof cmd,
               "gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime/include "
               "-Iruntime/src/core -Iruntime/src/win32 -Iruntime/src/skin -Iruntime/src/kit "
               "-c \"%s\" -o \"%s\"", files[i], obj);
      run(cmd);
      changed = 1;
    }
  }
  if (!nobjs) die("no runtime sources under %s/runtime/src", ROOT);

  jp(rc, ROOT, "runtime/res/cstackgui.rc");
  jp(ico, ROOT, "runtime/res/cstackgui.ico");
  jp(reso, objdir, "cstackgui_res.o");
  if (!(mt(reso) > mt(rc) && mt(reso) > mt(ico))) {
    char cmd[MAXP * 2];
    printf("  RC runtime/res/cstackgui.rc\n");
    snprintf(cmd, sizeof cmd, "windres --include-dir=runtime/res -O coff \"%s\" -o \"%s\"", rc, reso);
    run(cmd);
    changed = 1;
  }

  jp(lib, libdir, "libcstackgui.a");
  if (!exists(lib)) changed = 1;
  if (!changed) {
    printf("  (runtime up to date, skipped compile)\n");
  } else {
    char cmd[MAXP * 8];
    int off;
    printf("  AR build/lib/libcstackgui.a\n");
    /* 先删旧库再打包：`ar rcs` 只增替、**不会删除**已消失的成员 ——
       删掉/改名某个 .c 之后重编，旧 .o 仍留在库里，可能被链进最终 exe（静默出错）。 */
    if (exists(lib)) remove(lib);
    off = snprintf(cmd, sizeof cmd, "ar rcs \"%s\"", lib);
    for (i = 0; i < nobjs; i++) off += snprintf(cmd + off, sizeof cmd - off, " \"%s\"", objs[i]);
    off += snprintf(cmd + off, sizeof cmd - off, " \"%s\"", reso);
    if (off >= (int)sizeof cmd) die("command line too long (ar)");
    run(cmd);
    printf("[ok] build/lib/libcstackgui.a\n");
  }
  refresh_clangd();
}

/* ------------------------------------------------------------------ 一个工程 */

/** dir 下第一个子目录（apps\ / examples\ 各取第一个，与 build-app.bat 的口径一致） */
static int first_subdir(const char *dir, char *out) {
  DIR *d;
  struct dirent *e;
  if (!(d = opendir(dir))) return 0;
  while ((e = readdir(d))) {
    char p[MAXP];
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    jp(p, dir, e->d_name);
    if (is_dir(p)) { snprintf(out, MAXP, "%s", p); closedir(d); return 1; }
  }
  closedir(d);
  return 0;
}

static void resolve_app(const char *arg, char *out) {
  char cand[MAXP], apps[MAXP], ex[MAXP];
  const char *a = arg;
  if (!a || !*a) a = getenv("CG_APP_DIR");
  if (!a || !*a) {
    jp(apps, ROOT, "apps");
    jp(ex, ROOT, "examples");
    if (first_subdir(apps, out)) return;
    if (first_subdir(ex, out)) return;
    die("no project given and none found under apps\\ or examples\\");
  }
  jp(cand, ROOT, "apps"); jp(cand, cand, a);
  if (is_dir(cand)) { snprintf(out, MAXP, "%s", cand); return; }
  jp(cand, ROOT, "examples"); jp(cand, cand, a);
  if (is_dir(cand)) { snprintf(out, MAXP, "%s", cand); return; }
  if (is_dir(a)) { snprintf(out, MAXP, "%s", a); return; }
  jp(cand, ROOT, a);
  if (is_dir(cand)) { snprintf(out, MAXP, "%s", cand); return; }
  die("project \"%s\" not found (looked in apps\\, examples\\, and as a path)", a);
}

static void build_app(const char *appdir, int release) {
  char name[MAXP], out[MAXP], libdir[MAXP], lib[MAXP], reso[MAXP], rcsrc[MAXP], rtrc[MAXP];
  char appres[MAXP], hdrs[MAXF][MAXP], srcs[MAXF][MAXP], objs[MAXF][MAXP];
  int i, n, nobjs = 0;
  double hdrT;
  int has_src;

  /* exe 名 = 工程目录名的最后一段（先去掉可能存在的尾部斜杠） */
  {
    char t[MAXP];
    snprintf(t, sizeof t, "%s", appdir);
    while (strlen(t) && (t[strlen(t) - 1] == '/' || t[strlen(t) - 1] == '\\')) t[strlen(t) - 1] = '\0';
    base_name(t, name);
  }
  snprintf(out, sizeof out, "%s/%s.exe", appdir, name);
  jp(libdir, appdir, "lib");
  mkdir_p(libdir);

  /* 发布包里没有 runtime\src：它不编运行时源码，只链接预编译好的 runtime\lib */
  {
    char probe[MAXP];
    jp(probe, ROOT, "runtime/src");
    has_src = is_dir(probe);
  }
  if (has_src) {
    jp(lib, ROOT, "build/lib/libcstackgui.a");
    jp(reso, ROOT, "build/obj/cstackgui_res.o");
    if (!getenv("CSTACKGUI_SKIP_RUNTIME") || strcmp(getenv("CSTACKGUI_SKIP_RUNTIME"), "1") != 0) {
      printf("[info] refreshing runtime library\n");
      build_runtime();
    }
  } else {
    jp(lib, ROOT, "runtime/lib/libcstackgui.a");
    jp(reso, ROOT, "runtime/lib/cstackgui_res.o");
  }

  /* 往工程目录里留一份运行时库的拷贝：这样整个目录被挪走之后还能自己重编。
     资源对象里没有代码符号，链接器不会主动从静态库里取它 —— 必须显式链接，
     否则 exe 拿不到 v6 通用控件（页签/进度条会退回老外观）。 */
  { char d[MAXP]; jp(d, libdir, "libcstackgui.a"); copy_if_changed(lib, d); }
  { char d[MAXP]; jp(d, libdir, "cstackgui_res.o"); copy_if_changed(reso, d);
    snprintf(appres, sizeof appres, "%s", d); }

  /* 工程自带的 <名字>.rc（画布「程序配置」生成）会顶掉运行时那份通用版本。
     它是 UTF-8（含中文字符串），所以加 --codepage=65001。 */
  snprintf(rcsrc, sizeof rcsrc, "%s/%s.rc", appdir, name);
  jp(rtrc, ROOT, "runtime/res/cstackgui.rc");
  if (exists(rcsrc)) {
    char o[MAXP];
    snprintf(o, sizeof o, "%s/%s_res.o", libdir, name);
    if (!(mt(o) > mt(rcsrc) && mt(o) > mt(rtrc))) {
      char cmd[MAXP * 3];
      printf("  RC %s.rc\n", name);
      snprintf(cmd, sizeof cmd, "windres --codepage=65001 -I \"%s\" -I \"%s/runtime/res\" -O coff \"%s\" -o \"%s\"",
               appdir, ROOT, rcsrc, o);
      run(cmd);
    }
    snprintf(appres, sizeof appres, "%s", o);
  }

  /* 增量编译：工程内的头文件和公开头 cstackgui.h 变新同样会触发重编，
     避免悄悄链上旧代码。删掉的源文件自然从链接列表里消失。 */
  hdrT = newest_of(hdrs, list_ext(appdir, ".h", hdrs));
  {
    char pub[MAXP];
    jp(pub, ROOT, "runtime/include/cstackgui.h");
    if (mt(pub) > hdrT) hdrT = mt(pub);
  }
  n = list_ext(appdir, ".c", srcs);
  if (!n) die("no .c sources found in %s", appdir);
  for (i = 0; i < n; i++) {
    char obj[MAXP], stem[MAXP], cmd[MAXP * 2];
    const char *b = strrchr(srcs[i], '\\');
    const char *b2 = strrchr(srcs[i], '/');
    if (!b) b = b2;
    if (b2 && b2 > b) b = b2;
    b = b ? b + 1 : srcs[i];
    scopy(stem, sizeof stem, b);
    stem[strlen(stem) - 2] = '\0';
    snprintf(obj, sizeof obj, "%s/%s.o", libdir, stem);
    snprintf(objs[nobjs++], MAXP, "%s", obj);
    if (mt(obj) > mt(srcs[i]) && mt(obj) > hdrT) continue;
    printf("  CC %s\n", srcs[i]);
    snprintf(cmd, sizeof cmd,
             "gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime/include -c \"%s\" -o \"%s\"",
             srcs[i], obj);
    run(cmd);
  }

  /* 链接的代价远低于一个不匹配的 exe，所以总是链接（宁可多链一次，
     也不能悄悄链上旧库）。 */
  {
    char cmd[MAXP * 8];
    int off;
    printf("  LD %s%s%s\n", out, release ? " (release, no console)" : " (debug, with console)", "");
    off = snprintf(cmd, sizeof cmd,
                   "gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Iruntime/include %s -o \"%s\"",
                   release ? "-mwindows" : "", out);
    for (i = 0; i < nobjs; i++) off += snprintf(cmd + off, sizeof cmd - off, " \"%s\"", objs[i]);
    off += snprintf(cmd + off, sizeof cmd - off, " \"%s\" \"%s/libcstackgui.a\"", appres, libdir);
    off += snprintf(cmd + off, sizeof cmd - off,
                    " -lcomctl32 -luser32 -lgdi32 -lshell32 -lgdiplus -lcomdlg32 -luuid -lole32 -loleaut32 -lws2_32 -lstdc++");
    if (off >= (int)sizeof cmd) die("command line too long (link)");
    run(cmd);
  }

  printf("[ok] %s\n", out);
  printf("\n");
  printf("    run it (double-click works):   %s\n", out);
  printf("    self-check without window:     %s --selftest\n", out);
  printf("    rebuild this project:          build-app.bat %s\n", name);
  printf("    在画布里编译：                 打开本工程的 .cgui，按 F5（画布就是调上面这一条）\n");
  refresh_clangd();
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
  int release = 0, runtime_only = 0;
  const char *arg = NULL;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--release")) release = 1;
    else if (!strcmp(argv[i], "--runtime-only")) runtime_only = 1;
    else if (argv[i][0] != '-') arg = argv[i];
  }

  find_root();
  if (chdir(ROOT) != 0) die("cannot chdir to %s", ROOT);
  setup_toolchain();

  if (runtime_only) { build_runtime(); return 0; }
  {
    char app[MAXP];
    resolve_app(arg, app);
    build_app(app, release);
  }
  return 0;
}
