/* codegen.ts -- 从 .cgui 生成 C 侧骨架
 *
 * 目标：画布上填了哪些事件名，就生成对应的空函数 + 绑定表，
 * 让业务同学打开就能填逻辑，不用手抄一遍事件名（抄错就是运行时不报错的空绑定）。
 */
import { CG_EVENTS, KIND_MAP, walk, type CgDoc, type CgEvent, type CgNode } from "./model";

export type CallbackUse = {
  name: string;
  widgetId: string;
  widgetType: string;
  event: CgEvent;
};

/** 文档里用到的所有回调，按名字去重 */
export function collectCallbacks(doc: CgDoc): CallbackUse[] {
  const uses: CallbackUse[] = [];
  walk(doc.widgets, (n: CgNode) => {
    for (const ev of CG_EVENTS) {
      const name = n.events?.[ev];
      if (name) uses.push({ name, widgetId: n.id, widgetType: n.type, event: ev });
    }
  });
  return uses;
}

export function uniqueCallbackNames(doc: CgDoc): string[] {
  return [...new Set(collectCallbacks(doc).map((u) => u.name))].sort();
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

export function generateC(doc: CgDoc, baseName: string): GenFile[] {
  const base = sanitizeIdent(baseName);
  const cguiFile = baseName.endsWith(".cgui") ? baseName : `${baseName}.cgui`;
  const uses = collectCallbacks(doc);
  const names = uniqueCallbackNames(doc);

  const header = `/* ${base}_ui_events.h -- 由 CStackGUI 画布生成，请勿手改事件名部分。
 * 界面文件: ${cguiFile}
 * 改事件绑定请回画布里改，再重新导出；业务逻辑写在 ${base}_ui_events.c 里。
 */
#ifndef ${guardName(base)}
#define ${guardName(base)}

#include "cstackgui.h"

/* 回调原型：签名固定，不要改 */
${names.length ? names.map((n) => `void ${n}(cg_widget_t *self, cg_event_t ev, void *user);`).join("\n") : "/* 画布上还没有绑定任何事件 */"}

/* 绑定表：main.c 里直接用，改事件名只需重新导出本文件 */
${names.length ? `#define ${macroName(base)} \\\n${names.map((n) => `    CG_BIND("${n}", ${n}), \\`).join("\n")}` : `#define ${macroName(base)}`}

/* 画布上声明的事件绑定清单（供人查对；运行时以 .cgui 为准）
${uses.length ? uses.map((u) => ` *   ${u.widgetId.padEnd(18)} ${u.event.padEnd(9)} -> ${u.name}`).join("\n") : " *   （空）"}
 */
#endif /* ${guardName(base)} */
`;

  const stubs = `/* ${base}_ui_events.c -- 事件回调实现：业务逻辑写在这里
 *
 * 这份文件是「你的代码」，重新导出不会覆盖它（画布导出时同名文件会跳过）。
 * 每个回调都能拿到：
 *   self 触发事件的那个控件，用 cg_widget_text/cg_widget_checked/cg_widget_value 读它的状态
 *   ev   事件类型（一个函数只挂一个事件时可以直接忽略）
 *   user cg_widget_set_user() 挂上去的业务数据
 * 拿别的控件用 cg_find(cg_app_of(self), "控件id")；改界面用 cg_widget_set_* 系列。
 */
#include "${base}_ui_events.h"

#include <stdio.h>

${
  names.length
    ? names.map((n) => `void ${n}(cg_widget_t *self, cg_event_t ev, void *user)\n{\n    (void)user;\n    printf("[%s] widget=%s event=%s\\n", "${base}", cg_widget_id(self), cg_event_name(ev));\n\n    /* TODO: 在这里写业务逻辑 */\n}\n`).join("\n")
    : `/* 画布上还没有绑定事件。在画布右侧「事件」页签里填上 C 函数名，再重新导出。 */\n`
}`;

  const mainC = `/* main.c -- 应用入口（由 CStackGUI 画布生成）
 *
 * 编译：本目录要放在 CStackGUI 项目的 apps\\ 下，然后在项目根目录跑
 *       build-app.bat ${base}
 * 产物：apps\\${base}\\${base}.exe —— 单个 exe，无运行库依赖，双击就能跑。
 */
#include "${base}_ui_events.h"

#include <stdio.h>
#include <string.h>

static const cg_binding_t k_bindings[] = {
    ${macroName(base)}
    CG_BIND_END
};

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

  const buildBat = `@echo off
rem Build this UI project into a standalone exe.
rem Standard setup: put this folder under CStackGUI\\apps\\, then run
rem   build-app.bat ${base}
rem from the CStackGUI root. If this folder lives elsewhere, set the
rem CSTACKGUI_HOME env var to the CStackGUI project root first.
rem NOTE: keep this file ASCII-only (cmd mis-parses non-ASCII after chcp 65001).
setlocal
if "%CSTACKGUI_HOME%"=="" set CSTACKGUI_HOME=%~dp0..\\..
if not exist "%CSTACKGUI_HOME%\\build-app.bat" (
    echo [fail] build-app.bat not found in "%CSTACKGUI_HOME%"
    echo        Set CSTACKGUI_HOME to the CStackGUI project root, or move this
    echo        folder into CStackGUI\\apps\\.
    exit /b 1
)
call "%CSTACKGUI_HOME%\\build-app.bat" ${base}
`;

  return [
    { name: `${base}_ui_events.h`, content: header },
    { name: `${base}_ui_events.c`, content: stubs },
    { name: "main.c", content: mainC },
    { name: cguiFile, content: `${JSON.stringify(doc, null, 2)}\n` },
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
