#!/usr/bin/env python3
"""专家团编制校验：teams.toml（单一事实源）↔ 磁盘目录 ↔ INDEX 的 ROSTER 块，三处必须一致。

为什么要有它：
  物理分层之后，"哪个技能属于哪个团"同时出现在三个地方 ——
    ① `_registry/teams.toml`（改编制只改这里）
    ② 磁盘 `skills/<team.dir>/<skill>/`
    ③ `INDEX.md` 的 ROSTER 块与分类总览表
  三者任何一处漏改都不会让编译变红，只会让"点将点不到"或"技能凭空消失"。

判据：
  A. TOML 可解析；每个技能**恰好**属于一个团；lead 必须是本团成员
  B. 磁盘：每个登记技能的 `SKILL.md` 存在；反向，磁盘上的技能目录都被登记
  C. 团队目录内**不得**有 SKILL.md（技能扫描遇到它就停止下钻，会吞掉整团）
  D. 顶层只允许 INDEX.md / _registry / expert-teams / 各团队目录
  E. INDEX 的 ROSTER 块与 TOML 一致（`--fix` 可重新渲染）
  F. `expert-teams` §1 八团总表里手抄的成员列表 == TOML（2026-11-01 加）

用法：
  python tools\\check_roster.py         校验
  python tools\\check_roster.py --fix   用 TOML 重写 INDEX 的 ROSTER 块
退出码：0 = 一致；1 = 有漂移
"""
import re
import sys
import tomllib
from pathlib import Path

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

ROOT = Path(__file__).resolve().parent.parent
SKILLS = ROOT / ".codebuddy" / "skills"
REG = SKILLS / "_registry"
TEAMS_TOML = REG / "teams.toml"
INDEX = SKILLS / "INDEX.md"
ENTRY = "expert-teams"          # 唯一入口，不属于任何团

bad = []
dirs_of = {}      # team code -> 目录名（模块级：收尾那行汇报要用）


def fail(where, msg):
    bad.append(f"[fail] {where}：{msg}")


def main():
    if not TEAMS_TOML.exists():
        fail("teams.toml", "缺编制事实源：.codebuddy/skills/_registry/teams.toml")
        return

    data = tomllib.loads(TEAMS_TOML.read_text(encoding="utf-8"))
    teams = data.get("teams", [])
    if not teams:
        fail("teams.toml", "没有 [[teams]] 条目")
        return

    owner = {}          # skill -> team code
    for t in teams:
        code = t.get("code", "?")
        d = t.get("dir", "")
        skills = t.get("skills", [])
        dirs_of[code] = d
        if not d:
            fail("teams.toml", f"{code} 缺 dir")
            continue
        if t.get("lead") and t["lead"] not in skills:
            fail("teams.toml", f"{code} 的 lead `{t['lead']}` 不在本团 skills 里")
        for s in skills:
            if s in owner:
                fail("teams.toml", f"`{s}` 同时属于 {owner[s]} 与 {code} —— 一个技能只能归一个团")
            owner[s] = code

    # B. 磁盘正向：登记的技能必须真的在对应目录下
    for s, code in owner.items():
        p = SKILLS / dirs_of[code] / s / "SKILL.md"
        if not p.exists():
            fail("磁盘", f"`{s}` 登记在 {code}（{dirs_of[code]}/），但 {p.relative_to(ROOT)} 不存在")

    # D. 顶层只允许：INDEX.md / _registry / expert-teams / 各团队目录
    allowed_dirs = set(dirs_of.values()) | {ENTRY, "_registry"}
    on_disk = set()
    for e in SKILLS.iterdir():
        if e.is_file():
            if e.name != "INDEX.md":
                fail("磁盘", f"skills/ 顶层出现了非预期文件 {e.name}")
            continue
        if e.name not in allowed_dirs:
            fail("磁盘", f"skills/ 顶层出现了非预期目录 `{e.name}`（只允许 INDEX.md / _registry / {ENTRY} / 各团队目录）")
            continue
        # C. 团队目录内不得有 SKILL.md
        if e.name in dirs_of.values() and (e / "SKILL.md").exists():
            fail("磁盘", f"团队目录 `{e.name}/` 里放了 SKILL.md —— 技能扫描会停止下钻、把整团吞成一个技能")
        if e.name in dirs_of.values() and not (e / "README.md").exists():
            fail("磁盘", f"团队目录 `{e.name}/` 缺 README.md（团队说明一律用 README）")
        for sub in e.iterdir():
            if sub.is_dir():
                on_disk.add(sub.name)
                if not (sub / "SKILL.md").exists():
                    fail("磁盘", f"`{e.name}/{sub.name}/` 没有 SKILL.md")

    # B. 磁盘反向：磁盘上的技能目录都被登记
    for s in sorted(on_disk - {ENTRY} - set(owner)):
        fail("teams.toml", f"磁盘上有技能 `{s}`，但没登记进任何团")

    # E. INDEX 的 ROSTER 块
    text = INDEX.read_text(encoding="utf-8") if INDEX.exists() else ""
    want = "\n".join(f"{t['code']}|{','.join(t.get('skills', []))}" for t in teams)
    if "--fix" in sys.argv and not bad:
        start, end = "<!-- ROSTER:BEGIN", "ROSTER:END -->"
        i, j = text.find(start), text.find(end)
        if i >= 0 and j > i:
            head = text[: i + len(start)]
            tail = text[j:]
            text = head + "\n" + want + "\n" + tail
            INDEX.write_text(text, encoding="utf-8")
            print("[fix] 已用 teams.toml 重写 INDEX.md 的 ROSTER 块")
        else:
            fail("INDEX.md", "找不到 ROSTER 块，无法 --fix")
        return

    got = [l for l in text.splitlines() if l[:1] == "T" and "|" in l]
    if not got:
        fail("INDEX.md", "缺 ROSTER 块（<!-- ROSTER:BEGIN … ROSTER:END -->）")
    elif "\n".join(got) != want:
        for a, b in zip(got, want.splitlines()):
            if a != b:
                fail("INDEX.md", f"ROSTER 与 teams.toml 不一致：\n    现在：{a}\n    应为：{b}")
        if len(got) != len(want.splitlines()):
            fail("INDEX.md", f"ROSTER 有 {len(got)} 行，teams.toml 有 {len(want.splitlines())} 个团")

    # F. expert-teams §1 总表：手抄的成员列表必须与 TOML 一致。
    #    为什么加这条：总表是"选团"那一刻唯一会被读的表，但它手抄自 teams.toml ——
    #    加 / 删 / 换团时漏改不会让编译或任何别的门禁变红，只会让点将点不到、或点到已走的技能。
    #    为什么不直接渲染这张表：渲染会覆盖手写措辞（使命 / 何时点将都是人写的），
    #    而校验同样能消灭"静默腐化"，代价小一个量级。
    et = SKILLS / ENTRY / "SKILL.md"
    if not et.exists():
        fail("expert-teams", "缺 SKILL.md")
    else:
        et_rows = {}
        for line in et.read_text(encoding="utf-8").splitlines():
            if not line.startswith("|"):
                continue
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if len(cells) < 4:
                continue
            for t in teams:
                if f"**{t['code']}" in cells[0]:
                    et_rows[t["code"]] = set(re.findall(r"`([^`]+)`", cells[3]))
        for t in teams:
            code = t["code"]
            want_s = set(t.get("skills", []))
            if code not in et_rows:
                fail("expert-teams/SKILL.md", f"§1 总表里找不到 {code} 那一行（表结构变了？）")
            elif et_rows[code] != want_s:
                miss = sorted(want_s - et_rows[code])
                extra = sorted(et_rows[code] - want_s)
                fail(
                    "expert-teams/SKILL.md",
                    f"§1 的 {code} 成员与 teams.toml 不一致：少 {miss} 多 {extra}",
                )

    # 总数口径（INDEX 顶部那句）
    total = len(owner) + 1
    for line in text.splitlines():
        if "技能总数" in line and "**" in line:
            num = line.split("**")[1]
            if num.isdigit() and int(num) != total:
                fail("INDEX.md", f"技能总数写 **{num}**，实际 {total}（{len(owner)} 成员 + 1 入口）")

    if not bad:
        print(f"[ok] 编制成立：{len(dirs_of)} 个团 / {len(owner)} 个成员技能 + 1 入口；TOML ↔ 磁盘 ↔ INDEX ROSTER ↔ expert-teams 总表 四处一致")


main()

if bad:
    print("\n".join(bad))
    print(f"\n[fail] 编制不成立：{len(bad)} 处")
    sys.exit(1)
