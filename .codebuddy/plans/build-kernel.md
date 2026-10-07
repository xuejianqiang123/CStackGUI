# 把编译内核从 bat 搬进 CStackGUI（唯一实现 = Node 脚本）

## 目标

用户要求：`build-app.bat` 的内部功能集成到 `CStackGUI.exe` 画布里，
`build-app.bat` 只做转发（调 exe 传参），exe 内部直接编译，不再外部调 bat。

顺带根治一个已暴露的问题：`build-app.bat` / `build-runtime.bat` 是**带中文的 UTF-8 bat**，
靠 `chcp 65001 + cmd /c "%~f0"` 重入才能正确解析。画布 F5 走的是
`cmd /d /s /c "chcp 65001 >nul & build-app.bat"`（`designer/electron/main.mjs:586`），
`chcp` 与脚本挤在同一个 cmd 进程里，脚本自带的重入逻辑不生效 ——
在部分机器上表现为**行被从多字节字符中间切断**，后半段被当成命令
（`'lery' is not recognized` 这类）。本机复现不出来（41 行输出 0 报错），
但失败形态与本仓库已记录的坑一致（`tools/gen-launcher.bat` 头部：
cmd 按字节偏移扫描，UTF-8 中文 3 字节/字会漂移）。

**内核搬进 Node 之后，bat 只剩 ASCII 三行，代码页这一整类问题消失。**

## 设计

### 唯一实现：`tools\cgbuild.mjs`（纯 Node，UTF-8，无代码页问题）

```
node tools\cgbuild.mjs <工程名|路径> [--release] [--runtime-only]
```

移植自 `build-app.bat` + `build-runtime.bat`，逐条对搬：

1. 定位项目根（含 `runtime\lib` 的那层；支持 `CSTACKGUI_HOME`）
2. 找工具链 `gcc / ar / windres`：`MINGW` 环境变量 → PATH → 常见安装位；找不到给可执行的修复提示
3. 刷运行时库（原 `build-runtime.bat`）：四层 `*.c` 全编、**头文件时间取最大**让旧 `.o` 失效、
   资源对象 `windres`、`ar rcs` 前**先删旧库**（`ar rcs` 不删已消失成员）
4. 拷运行时库副本到 `<工程>\lib\`，**按内容比较**（`fc /b` 等价物）：内容相同则保留旧时间戳，
   否则"库永远比 exe 新"会让"没变就跳过链接"永不生效
5. rc：工程自带 `<名字>.rc`（画布「程序配置」生成）顶掉运行时通用版；`windres --codepage=65001 -I runtime\res`
6. 增量编 `<工程>\*.c`（时间戳 + 头失效），删掉的源文件自然从链接列表消失
7. 链接：debug / `--release` 加 `-mwindows`
8. 刷新 clangd（`tools\gen-cc.mjs`），没装 node 静默跳过
9. 输出文案与退出码**沿用现状**（`check-format.bat` 与文档里有断言依赖）

### 四个入口，全部转发到内核

| 入口 | 改动 |
|---|---|
| 画布 F5 | `main.mjs` 改为 `spawn(process.execPath, [kernel, appDir])` —— **不 spawn cmd / bat**，`CG_APP_DIR` 不再需要 |
| `build-app.bat` | ASCII 薄壳：`node tools\cgbuild.mjs %*`（找不到 node 给明确提示） |
| `build-runtime.bat` | ASCII 薄壳：`node tools\cgbuild.mjs --runtime-only` |
| 发布包 `packaging\build-app.bat` | 薄壳：`CStackGUI.exe --build %*` → Electron 加**无窗口 CLI 分支**，内部直接调内核 |

`build-all.bat` 继续逐个调 `build-app.bat`（已是薄壳），不改。

## 取舍（已拍板，记录判据）

- **内核放 Node 而不是 Electron 主进程里**：`process.execPath` 在画布里就是 Electron 自带的 node，
  F5 直接 fork 它即可，既"exe 内部编译"，又不需要给 Electron 做 GUI/CLI 双模式；
  `build-app.bat` 也能转发到同一个内核 —— 仍然是**一份实现**。
- **发布包走 `CStackGUI.exe --build`**：需要给 `main.mjs` 加一个 `app.whenReady` 前判断 argv 的
  无窗口分支（跑完 `process.exit(code)`）。代价小，换来发布包里也只有一份实现。
  （另一条路是发布包保留完整 `packaging\build-app.bat`，那就是两份实现，
   重演 `docs/ARCHITECTURE.md` 里写过的"两份脚本迟早各改各的"教训。）
- **`build-runtime.bat` 一起并入**：它和 `build-app.bat` 共享"找 gcc / 增量 / 头文件失效"逻辑，
  留两份必然漂移。`tools\run-tests.bat` 间接走薄壳，行为不变。

## 验收（Validation-First，每条都要能失败）

- [ ] `node tools\cgbuild.mjs gallery`：产出 `examples\gallery\gallery.exe`，与 bat 版产物字节一致
      （反例：故意改坏一个 `.c`，必须编译失败且退出码非 0）
- [ ] 增量：连跑两次，第二次必须打印"没变，跳过"（反例：删掉 `.o` 后必须重编）
- [ ] 改 `runtime\` 后自动刷库（反例：手动删 `build\lib\*.a` 后必须重建）
- [ ] `--release` 出 `-mwindows` 无控制台 exe
- [ ] 画布 F5：起真窗口按 F5，能编译并起 exe（反例：故意写坏 C 代码，输出面板必须显示编译错误）
- [ ] `CStackGUI.exe --build <dir>`：无窗口、退出码正确（反例：不存在的工程 → 非 0 退出码）
- [ ] `build-all.bat` 26 个示例 `--selftest` 全过
- [ ] `tools\run-tests.bat` + `tools\check-format.bat` 全绿
- [ ] `bat-utf8.ps1 -Mode fix`：所有 bat 仍是 UTF-8 无 BOM + CRLF + ASCII 前导
- [ ] **bat 里不再有中文**（这是根治代码页问题的判据，可 grep 断言）

## 回滚

内核与 bat 并存期间：若 `cgbuild.mjs` 出问题，`git checkout` 拿回旧 `build-app.bat` 即可；
内核稳定后再删旧逻辑（旧逻辑就在 git 里，随时 `git show` 取回）。
