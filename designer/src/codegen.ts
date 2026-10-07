/* codegen.ts -- 从 .cgui 生成 C 侧骨架
 *
 * 目标：画布上填了哪些事件名，就生成对应的空函数 + 绑定表，
 * 让业务同学打开就能填逻辑，不用手抄一遍事件名（抄错就是运行时不报错的空绑定）。
 */
/* 桶导入**带 .js 后缀**：codegen 是唯一会被 Node 直接 import 的模型层消费者
 * （端到端探针要真跑 generateC 去造工程），而 Node 的 ESM 不认"目录导入"——
 * 编译产物里留一个 `./model` 会报 ERR_UNSUPPORTED_DIR_IMPORT。
 * 与 model/ 内部那条"相对导入带 .js"的约定同一个道理。 */
import {
  CG_EVENTS, EVENT_LABEL, KIND_MAP, WINDOW_EVENTS, serializeDoc, walk,
  type CgDoc, type CgEvent, type CgInfo, type CgNode,
} from "./model.js";

export type CallbackUse = {
  name: string;
  widgetId: string;
  widgetType: string;
  event: CgEvent;
};

/** 动作（actions）里登记的回调。它不是"某个控件的事件"，所以单独收一遍：
 *  它和事件回调进的是**同一张** CG_BIND 表，导出时漏了就会让用户一跑 --selftest 就红。 */
export type ActionUse = {
  /** CG_BIND 的名字（动作的 handler） */
  name: string;
  /** 动作名（顶层 actions 的键），用来写注释和 cg_action_name */
  action: string;
  /** 动作的显示文字（写注释用，可空） */
  text: string;
};

/** 文档里用到的所有回调（含窗口级事件，它们挂在 app.events 上，伪控件 id 是 __window） */
export function collectCallbacks(doc: CgDoc): CallbackUse[] {
  const uses: CallbackUse[] = [];
  walk(doc.widgets, (n: CgNode) => {
    for (const ev of CG_EVENTS) {
      const name = n.events?.[ev];
      if (name) uses.push({ name, widgetId: n.id, widgetType: n.type, event: ev });
    }
  });
  /* 窗口事件写在 app.events 里，不走 widgets */
  for (const ev of WINDOW_EVENTS) {
    const name = doc.app.events?.[ev];
    if (name) uses.push({ name, widgetId: "__window", widgetType: "window", event: ev });
  }
  return uses;
}

/** 动作表里有回调名的动作（handler 没填的动作会被运行时忽略，不需要骨架） */
export function collectActionHandlers(doc: CgDoc): ActionUse[] {
  const out: ActionUse[] = [];
  for (const [action, a] of Object.entries(doc.actions ?? {})) {
    if (a.handler) out.push({ name: a.handler, action, text: a.text ?? "" });
  }
  return out;
}

/** 需要进 CG_BIND 表（= 要生成空函数骨架）的全部名字：事件回调 + 动作回调 */
export function uniqueCallbackNames(doc: CgDoc): string[] {
  return [
    ...new Set([
      ...collectCallbacks(doc).map((u) => u.name),
      ...collectActionHandlers(doc).map((u) => u.name),
    ]),
  ].sort();
}

/** 画布上「还没绑定事件」的控件（给面板做检查用） */
export function widgetsWithoutEvents(doc: CgDoc): CgNode[] {
  const out: CgNode[] = [];
  walk(doc.widgets, (n: CgNode) => {
    if (n.type === "label" || n.type === "box" || n.type === "separator") return;
    if (!n.events || !Object.keys(n.events).length) out.push(n);
  });
  return out;
}

/** C 标识符合法化：on-ok.click -> on_ok_click */
export function sanitizeIdent(name: string): string {
  const s = name.replace(/[^0-9a-zA-Z_]/g, "_").replace(/^([0-9])/, "_$1");
  return s || "on_event";
}

const macroName = (base: string) => `${sanitizeIdent(base).toUpperCase()}_BINDINGS`;
const guardName = (base: string) => `${sanitizeIdent(base).toUpperCase()}_UI_EVENTS_H`;

export type GenFile = { name: string; content: string };

/** 骨架语言：C 还是 C++（MinGW-w64 自带 g++，C++ 工程照样编出单个 exe） */
export type GenLang = "c" | "cpp";

/** 回调实现文件名（`<工程>_ui_events.c` / `.cpp`） */
export const implFileName = (base: string, lang: GenLang) => `${base}_ui_events.${lang === "cpp" ? "cpp" : "c"}`;
/** 入口文件名（`main.c` / `main.cpp`） */
export const mainFileName = (lang: GenLang) => (lang === "cpp" ? "main.cpp" : "main.c");

/** 回调名 -> 「控件id 的<事件中文名>」/「动作 xxx」描述（多处共用时列出全部） */
function callbackBriefs(uses: CallbackUse[], actions: ActionUse[]): Record<string, string> {
  const byName: Record<string, string[]> = {};
  for (const u of uses) {
    const label = EVENT_LABEL[u.event];
    const where = u.widgetType === "window" ? "主窗口" : u.widgetId;
    (byName[u.name] ??= []).push(`${where} 的${label}`);
  }
  for (const a of actions) {
    (byName[a.name] ??= []).push(a.text ? `动作 ${a.action}（${a.text}）` : `动作 ${a.action}`);
  }
  return Object.fromEntries(Object.entries(byName).map(([k, v]) => [k, v.join("、")]));
}

/** 程序版本号：画布填的「版本号」+「创建号」拼成四段，如 1.0 + 0.0 -> 1.0.0.0 */
export function fullVersion(info: CgInfo | undefined): string {
  const parts: string[] = [];
  const push = (s: string | undefined) => {
    for (const raw of (s ?? "").split(/[.,]/)) {
      const t = raw.trim();
      if (t === "") continue;
      const n = Number(t);
      parts.push(Number.isFinite(n) && n >= 0 ? String(Math.round(n)) : "0");
    }
  };
  push(info?.version);
  push(info?.build);
  while (parts.length < 4) parts.push("0");
  return parts.slice(0, 4).join(".");
}

/** 版本信息里的字符串要转义反斜杠和引号，否则 rc 编译不过 */
const rcStr = (s: string) => s.replace(/\\/g, "\\\\").replace(/"/g, '\\"');

/**
 * 生成 <工程>.rc：图标 + 清单 + 版本信息。
 * build-app.bat 见到它就编译它、并用它替换通用的 cstackgui_res.o，
 * 于是每个业务 exe 都带上画布「程序配置」里填的名字 / 版本 / 作者。
 */
function rcFile(base: string, doc: CgDoc): GenFile {
  const info = doc.app.info ?? {};
  const ver = fullVersion(info);
  const comma = ver.replace(/\./g, ",");
  /* 图标路径：画布选了就用它的（相对工程目录，windres 靠 -I 工程目录找得到），
   * 没选就用运行时自带的那个（靠 -I runtime\res 找到）。 */
  const icon = rcStr(doc.app.icon && doc.app.icon.trim() ? doc.app.icon.trim() : "cstackgui.ico");

  const values: [string, string][] = [
    ["CompanyName", info.company || info.author || ""],
    ["FileDescription", info.desc || ""],
    ["FileVersion", ver],
    ["InternalName", base],
    ["LegalCopyright", info.copyright || ""],
    ["OriginalFilename", `${base}.exe`],
    ["ProductName", info.product || base],
    ["ProductVersion", ver],
    ["Comments", info.comments || ""],
    ["Author", info.author || ""],
    ["Address", info.address || ""],
    ["PostalCode", info.postcode || ""],
    ["Phone", info.phone || ""],
    ["Fax", info.fax || ""],
    ["Email", info.email || ""],
    ["HomePage", info.homepage || ""],
  ];
  const body = values
    .filter(([, v]) => v !== "")
    .map(([k, v]) => `      VALUE "${k}", "${rcStr(v)}"`)
    .join("\n");

  const content = `/* ${base}.rc -- 程序图标 + v6 控件清单 + 版本信息
 * 由 CStackGUI 画布生成（工具栏「程序配置」对话框），改内容请回画布改后重新导出。
 * build-app.bat 用 windres 编译它：文件是 UTF-8，所以带 --codepage=65001。
 */
#pragma code_page(65001)

#include <windows.h>

1 ICON "${icon}"

/* v6 通用控件清单：少了它标签页/进度条是老式外观 */
1 RT_MANIFEST "cstackgui.manifest"

VS_VERSION_INFO VERSIONINFO
  FILEVERSION    ${comma}
  PRODUCTVERSION ${comma}
  FILEFLAGSMASK  VS_FFI_FILEFLAGSMASK
  FILEFLAGS      0
  FILEOS         VOS_NT_WINDOWS32
  FILETYPE       VFT_APP
  FILESUBTYPE    VFT2_UNKNOWN
BEGIN
  BLOCK "StringFileInfo"
  BEGIN
    BLOCK "080404B0"
    BEGIN
${body}
    END
  END
  BLOCK "VarFileInfo"
  BEGIN
    VALUE "Translation", 0x804, 1200
  END
END
`;
  return { name: `${base}.rc`, content };
}

/**
 * 生成一套 C / C++ 骨架。
 * @param doc 界面文档
 * @param baseName 工程名（导出目录名 = exe 名，必须是 ASCII 标识符）
 * @param lang 语言：C 模板给 `.c` 三件套，C++ 模板给 `.cpp` 三件套（界面文件 / .rc / build.bat 两边一样）
 * @returns 待落盘的文件清单
 * @note 语言只在**首次导出**（工程目录里还没有骨架）时由用户选；之后一律沿用工程里已有的那套，
 *       换语言会在同一个工程里落下两套源文件（`main.c` + `main.cpp`），链接就是重复定义。
 */
export function generateC(doc: CgDoc, baseName: string, lang: GenLang = "c"): GenFile[] {
  const base = sanitizeIdent(baseName);
  const cpp = lang === "cpp";
  const implFile = implFileName(base, lang);
  const mainFile = mainFileName(lang);
  const cguiFile = baseName.endsWith(".cgui") ? baseName : `${baseName}.cgui`;
  const uses = collectCallbacks(doc);
  const actionUses = collectActionHandlers(doc);
  const names = uniqueCallbackNames(doc);
  const briefs = callbackBriefs(uses, actionUses);
  /* 动作回调的两条固定语义（见 cstackgui.h 的「动作（actions）」一节）：
   * self 恒为主窗口伪控件、ev 恒为 click —— 注释里写清楚，用户才不会去猜"从哪个入口点的" */
  const actionNames = new Set(actionUses.map((a) => a.name));

  /* 单个回调的完整 Doxygen（self/ev/user 三参数固定） */
  const cbDoc = (n: string) =>
    `/**
* @brief ${briefs[n] ?? n}
* @param self ${actionNames.has(n) ? "主窗口伪控件（cg_widget_id(self) 是 \"__window\"）" : "触发事件的控件"}
* @param ev ${actionNames.has(n) ? "恒为 CG_EVENT_CLICK" : "事件类型"}
* @param user 挂在控件上的业务数据（cg_widget_set_user 挂进去的）
* @return void 回调无返回值
*/`;

  /* C / C++ 共用同一个头：函数的语言链接必须和定义处一致。
     绑定表里的 cg_event_fn（cstackgui.h 里）声明在 extern "C" 块中，
     所以不管是 C 工程还是 C++ 工程，回调都按 C 链接给出——这里也只是把 C 侧那句
     "#ifdef __cplusplus" 如实写下来，纯 C 编译时它整段不存在。 */
  const header = `/**
 * @file    ${base}_ui_events.h
 * @brief   事件回调原型与绑定表（由 CStackGUI 画布生成，改事件绑定请回画布改后重新导出）
 * @details 界面文件: ${cguiFile}；业务逻辑写在 ${implFile} 里
 */
#ifndef ${guardName(base)}
#define ${guardName(base)}

#include "cstackgui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 回调原型：签名固定，不要改 */
${names.length ? names.map((n) => `${cbDoc(n)}\nvoid ${n}(cg_widget_t *self, cg_event_t ev, void *user);`).join("\n\n") : "/* 画布上还没有绑定任何事件 */"}

/* 绑定表：${mainFile} 里直接用，改事件名只需重新导出本文件 */
${names.length ? `#define ${macroName(base)} \\\n${names.map((n) => `    CG_BIND("${n}", ${n}), \\`).join("\n")}` : `#define ${macroName(base)}`}

/* 画布上声明的事件绑定清单（供人查对；运行时以 .cgui 为准）
${uses.length ? uses.map((u) => ` *   ${u.widgetId.padEnd(18)} ${u.event.padEnd(9)} -> ${u.name}`).join("\n") : " *   （空）"}
 */

/* 画布上声明的动作（actions）清单：菜单项/控件用 "action" 引用它们，
 * 一个动作的回调就是下面这些函数（self 是主窗口伪控件，用 cg_action_name(self) 认动作）
${actionUses.length ? actionUses.map((a) => ` *   ${a.action.padEnd(18)} ${(doc.actions?.[a.action]?.accel ?? "").padEnd(12)} -> ${a.name}`).join("\n") : " *   （空）"}
 */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ${guardName(base)} */
`;

  /* 一个回调的骨架函数体。动作回调没有"哪个控件、哪个事件"可打印，
     改成打印被激活的动作名（这正是 cg_action_name 的用途）。
     C++ 模板里函数前那句 extern "C" 不是装饰：绑定表要的是 C 函数指针
     （cg_event_fn 声明在 cstackgui.h 的 extern "C" 块里），C++ 名字修饰过的函数放不进 CG_BIND。 */
  const stubFn = (n: string) => {
    const head = `${cbDoc(n)}\n${cpp ? "extern \"C\" " : ""}void ${n}(cg_widget_t *self, cg_event_t ev, void *user)\n{\n`;
    return actionNames.has(n)
      ? `${head}    (void)ev;\n    (void)user;\n    printf("[%s] action=%s\\n", "${base}", cg_action_name(self));\n\n    /* TODO: 在这里写业务逻辑 */\n}\n`
      : `${head}    (void)user;\n    printf("[%s] widget=%s event=%s\\n", "${base}", cg_widget_id(self), cg_event_name(ev));\n\n    /* TODO: 在这里写业务逻辑 */\n}\n`;
  };

  const stubs = `/**
* @file    ${implFile}
 * @brief   事件回调实现：业务逻辑写在这里
 * @details 这份文件是「你的代码」：重新导出**只补不改** —— 画布上新绑的事件会自动补上桩
 *          （不补的话 .h 里已经声明、${mainFile} 的绑定表也引用了它，链接会 undefined reference），
 *          而你写过的任何一行都不会被动。
${cpp ? " *          本工程是 **C++ 模板**：回调按 extern \"C\" 给出（绑定表要 C 函数指针，成员\n *          函数和 C++ 名字修饰的函数都放不进去）；函数体里照样能写 C++ —— std::string /\n *          std::vector / 类都行，参考 examples\\cppdemo。" : ""}
 *          每个回调都能拿到：
 *          self 触发事件的那个控件，用 cg_widget_text/cg_widget_checked/cg_widget_value 读它的状态
 *          ev   事件类型（一个函数只挂一个事件时可以直接忽略）
 *          user cg_widget_set_user() 挂上去的业务数据
 *          拿别的控件用 cg_find(cg_app_of(self), "控件id")；改界面用 cg_widget_set_* 系列。
 *          动作（actions）的回调是同一套签名，但 self 恒为主窗口伪控件、ev 恒为 click，
 *          用 cg_action_name(self) 取回是哪个动作被激活（一个函数挂多个动作时靠它分辨）。
 */
#include "${base}_ui_events.h"

#include <stdio.h>

${
  names.length
    ? names.map(stubFn).join("\n\n")
    : `/* 画布上还没有绑定事件。在画布右侧「事件」页签里填上 C 函数名，再重新导出。 */\n`
}`;

  const mainHead = `/* @generated-by-cstackgui:${mainFile} —— 画布产物标记。
   这行的用处是让导出/编译脚本认出"这是画布生成的工程"（没有它的算手写工程：
   只同步 .cgui，代码一律不碰）。**注意：重新导出并不会覆盖这个文件**（怕抹掉你自己加的东西），
   所以业务逻辑请写在 ${implFile} 里 —— 那个文件重新导出时只会补上缺的空函数，
   你写过的任何一行都不会被动。 */
/**
 * @file    ${mainFile}
 * @brief   应用入口（由 CStackGUI 画布生成）
 * @details 编译：本目录要放在 CStackGUI 项目的 apps\\ 下，然后在项目根目录跑
 *          build-app.bat ${base}
 *          产物：apps\\${base}\\${base}.exe —— 单个 exe，无运行库依赖，双击就能跑。
 *          界面查找顺序（运行时）：exe 旁边的同名 .cgui → exe 内嵌的界面资源
 *          （编译时由 build-app 把 .cgui 作为 PE 资源编进去）。
 */
#include "${base}_ui_events.h"
`;

  const mainC = `${mainHead}
#include <stdio.h>
#include <string.h>

static const cg_binding_t k_bindings[] = {
    ${macroName(base)}
    CG_BIND_END
};

/**
 * @brief 程序入口：组装启动参数（绑定表、自动找 .cgui、--selftest 分支）
 * @param argc 参数个数
 * @param argv 命令行参数（可带 .cgui 路径或 --selftest）
 * @return int 进程退出码（0 正常）
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc;

    memset(&desc, 0, sizeof(desc));
    desc.bindings = k_bindings;
    desc.dblclick_suppress_click = 0;   /* 1 = 双击时不再回调 click */
    desc.auto_locate_cgui = 1;          /* 双击 exe 也能跑：自动去 exe 附近找 .cgui */

    /* --selftest：不开窗口，只校验 .cgui 能否解析、事件名是否都登记过 */
    if (argc > 1 && strcmp(argv[1], "--selftest") == 0) {
        desc.cgui_path = (argc > 2) ? argv[2] : "${cguiFile}";
        return cg_app_selftest(&desc);
    }

    desc.cgui_path = (argc > 1) ? argv[1] : "${cguiFile}";
    return cg_app_run(&desc);
}
`;

  /* C++ 版入口：和 C 版一一对齐（同一套 desc 字段、同一套界面查找顺序），
     只是用 C++ 自己的写法 —— `desc{}` 清零、`std::string` 比命令行。
     刻意不在这里示范 STL 用法：那是 examples\cppdemo 的事，入口越短越好懂。 */
  const mainCpp = `${mainHead}
#include <string>

static const cg_binding_t k_bindings[] = {
    ${macroName(base)}
    CG_BIND_END
};

/**
 * @brief 程序入口：组装启动参数（绑定表、自动找 .cgui、--selftest 分支）
 * @param argc 参数个数
 * @param argv 命令行参数（可带 .cgui 路径或 --selftest）
 * @return int 进程退出码（0 正常）
 */
int main(int argc, char **argv)
{
    cg_app_desc_t desc{};                   /* C++ 的 {} 一次性把整个结构清零 */

    desc.bindings = k_bindings;
    desc.dblclick_suppress_click = 0;   /* 1 = 双击时不再回调 click */
    desc.auto_locate_cgui = 1;          /* 双击 exe 也能跑：自动去 exe 附近找 .cgui */

    /* --selftest：不开窗口，只校验 .cgui 能否解析、事件名是否都登记过 */
    const bool selftest = argc > 1 && std::string(argv[1]) == "--selftest";
    desc.cgui_path = (argc > (selftest ? 2 : 1)) ? argv[selftest ? 2 : 1] : "${cguiFile}";
    return selftest ? cg_app_selftest(&desc) : cg_app_run(&desc);
}
`;

  /* 界面**不**往工程里生成 C 源码：单文件发布改成"把 .cgui 作为 PE 资源编进 exe"
     （见 designer\electron\cgbuild.mjs），运行时在所有文件位置都找不到时才读它。
     这样手写工程（examples/*）也能单文件发布，工程目录也不会多出几百 KB 的数组文件。 */

  /* 导出的 build.bat 与仓库里手写的 bat 同一套规则（见 tools\bat-utf8.ps1）：UTF-8 无 BOM
     + CRLF，开头一段纯 ASCII 前导先切代码页、再把同一个文件在新进程里重跑一遍。cmd 解析
     bat 用的是"启动这个 cmd 进程时"的控制台代码页，直接把中文写进 UTF-8 的 bat 又在同一个
     进程里切页的话，行定位会错位（注释后半句被当命令执行）；有了前导，标记行以下写中文是
     安全的 —— Node 本来也只能写 UTF-8，所以导出脚本也可以带中文。 */
  const buildBat = `@echo off
rem ---- ASCII prelude: keep everything above the next marker ASCII-only ----
rem cmd decodes a .bat with the console code page captured when it started, so a
rem UTF-8 file must be (re)entered by a process whose console is already 65001;
rem otherwise the Chinese lines below get cut in the middle and run as commands.
for /f "tokens=2 delims=:" %%c in ('chcp') do set "CG_CP=%%c"
if not "%CG_CP: =%"=="65001" (
    chcp 65001 >nul
    cmd /c "%~f0" %*
    exit /b %errorlevel%
)
rem ---- UTF-8 content starts here: Chinese is fine below ----
rem Build this UI project into a standalone exe.
rem The folder can live anywhere: the script walks up from here to find the
rem CStackGUI root (the one holding build-app.bat + runtime\\lib), so the
rem project may sit under CStackGUI\\apps\\, beside CStackGUI.exe, or in any
rem folder of your own; CSTACKGUI_HOME can also point at the root directly.
setlocal enabledelayedexpansion
rem No goto / call :label in this file: cmd looks a label up by byte offset and
rem Chinese text below the prelude would make that scan drift (see tools\\bat-utf8.ps1).
if "%CSTACKGUI_HOME%"=="" (
    set "SEARCH=%~dp0"
    for /L %%%%i in (1,1,10) do (
        if not defined CSTACKGUI_HOME if exist "!SEARCH!build-app.bat" if exist "!SEARCH!runtime\\lib\\libcstackgui.a" set "CSTACKGUI_HOME=!SEARCH!"
        if not defined CSTACKGUI_HOME set "SEARCH=!SEARCH!..\\"
    )
)
if "%CSTACKGUI_HOME:~-1%" NEQ "\\" set "CSTACKGUI_HOME=%CSTACKGUI_HOME%\\"
if "%CSTACKGUI_HOME%"=="\\" (
    echo [fail] cannot find the CStackGUI root ^(a folder with build-app.bat + runtime\\lib^).
    echo        Walked up from: %~dp0
    echo        Put this folder beside CStackGUI.exe, under CStackGUI\\apps\\, or set
    echo        CSTACKGUI_HOME to the CStackGUI root.
    exit /b 1
)
rem build-app.bat takes an absolute project dir, so renaming or moving this
rem folder still rebuilds.
call "%CSTACKGUI_HOME%build-app.bat" "%~dp0."
`;

  return [
    { name: `${base}_ui_events.h`, content: header },
    { name: implFile, content: stubs },
    { name: mainFile, content: cpp ? mainCpp : mainC },
    /* 与画布「保存」同一套序列化（cleanNode 去默认值字段），保证导出即保存、两者字节一致 */
    { name: cguiFile, content: serializeDoc(doc) },
    /* 程序图标 + 版本信息：build-app.bat 见到这个 rc 就用它，版本信息不再是通用的 */
    rcFile(base, doc),
    { name: "build.bat", content: buildBat.replace(/\n/g, "\r\n") },
  ];
}

/** 面板里的概览文案 */
export function describeDoc(doc: CgDoc) {
  let count = 0;
  let containers = 0;
  walk(doc.widgets, (n) => {
    count += 1;
    if (KIND_MAP[n.type]?.container) containers += 1;
  });
  return { count, containers, callbacks: uniqueCallbackNames(doc).length };
}
