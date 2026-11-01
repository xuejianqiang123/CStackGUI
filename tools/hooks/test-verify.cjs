/**
 * @file    test-verify.cjs
 * @brief   verify-on-stop 判据回归：三态结果 / 基线不被超时覆盖 / 只报回归
 * @details 为什么要有它：
 *
 *   2026-11-01 修掉的那个缺陷 —— 某次 40s 超时把门禁记成 false，而回归判据是
 *   `prev === true && now === false`，于是**一次超时吃掉下一次真回归** ——
 *   是"跑起来才看得见、跑起来也未必看得见"的那一类：它只在特定时序下失效，
 *   而失效表现是**什么都不发生**（hook 静默放行）。
 *
 *   所以把判据抽成纯函数直接测，不依赖工作区脏净、不依赖真实 git 状态。
 *   判据写错的代价是整条收工验证变成橡皮图章，而没有任何输出会因此变红。
 *
 * 用法：node tools\hooks\test-verify.cjs
 * 退出码：0 = 全过；1 = 有失败
 */
const { mergeGateState, isRegression } = require("./verify-on-stop.cjs");

let pass = 0;
const fails = [];

function check(name, actual, want) {
  if (JSON.stringify(actual) === JSON.stringify(want)) pass++;
  else fails.push(`[fail] ${name}\n    实际：${JSON.stringify(actual)}\n    应为：${JSON.stringify(want)}`);
}

/* —— mergeGateState：判不出来不许覆盖基线 —— */
check(
  "超时(null)不覆盖既有绿基线",
  mergeGateState({ a: true }, "a", { ok: null }),
  { a: true }
);
check(
  "超时(null)不凭空写出一条记录",
  mergeGateState({}, "a", { ok: null }),
  {}
);
check(
  "确证红(false)必须写进去",
  mergeGateState({ a: true }, "a", { ok: false }),
  { a: false }
);
check(
  "确证绿(true)写进去",
  mergeGateState({ a: false }, "a", { ok: true }),
  { a: true }
);
check(
  "不动其它门禁的记录",
  mergeGateState({ a: true, b: false }, "b", { ok: null }),
  { a: true, b: false }
);

/* —— isRegression：只有「确证绿 → 确证红」才算本次引入 —— */
check("上次绿、这次红 ⇒ 报", isRegression({ a: true }, "a", { ok: false }), true);
check("上次绿、这次判不出来 ⇒ 不报", isRegression({ a: true }, "a", { ok: null }), false);
check("上次红、这次还红 ⇒ 不报（既有红灯不背锅）", isRegression({ a: false }, "a", { ok: false }), false);
check("上次没有记录、这次红 ⇒ 不报（没有基线可比）", isRegression({}, "a", { ok: false }), false);
check("上次绿、这次还绿 ⇒ 不报", isRegression({ a: true }, "a", { ok: true }), false);

if (fails.length === 0) {
  console.log(`[ok] verify-on-stop 判据全过：${pass} 条`);
  process.exit(0);
}
console.log(fails.join("\n"));
console.log(`\n[fail] verify-on-stop 判据：${fails.length} 条不成立（${pass} 条通过）`);
process.exit(1);
