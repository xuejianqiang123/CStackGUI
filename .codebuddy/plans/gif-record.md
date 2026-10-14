# 实现计划：GIF 录屏（shottool 录屏 + runtime GIF 编码器）

> **状态：Converged（2026-10-14）** —— 编码器有单测铁证、真机录出过 41 帧的 GIF，
> 门禁全绿。下面第六节记的是实现过程中**踩到并修掉的真 bug**，比计划本身更有参考价值。

> 日期：2026-10-14 ｜ 范围：`runtime/src/kit/cg_kit_gif.c`（新增）+ `runtime/src/win32/cg_win_gif.c`（新增）+
> `runtime/include/cstackgui.h` + `examples/shottool/**` + 文档四件 + 单测
> 背景：`examples/shottool/README.md` 的能力边界里"录屏"原列在**明确不做**（当时理由：GIF 编码缺能力），
> 用户 2026-10-14 要求实现，本条计划就是把那一项从"不做"里划掉。

## 结论（一句话）

在 kit 层写一个**纯 C 的 GIF 编码器**（LZW + 固定调色板 + 脏矩形优化，可无头单测），
win32 层用它做"定时抓屏 → 逐帧编码 → 收尾写文件"的录屏 API，shottool 复用它已有的框选 overlay
加一个录制态。**不做**实时预览（每帧 PNG 往返太贵），**不做**八叉树量化（固定调色板先够用）。

## spec（验收口径）

1. `cg_gif_open / cg_gif_add / cg_gif_close / cg_gif_abort`：把一串 24 位 RGB 帧写成动画 GIF，
   循环播放、每帧可带延时；尺寸/帧数/延时参数非法时干净失败（返回 0/NULL，不留半成品文件）。
2. `cg_gif_rec_start / cg_gif_rec_stop / cg_gif_rec_info`：录一块**屏幕区域**成 GIF。
   停止时给路径就落盘、给 NULL 就丢弃；`--selftest`（没窗口）下三个函数都不崩、返回失败。
3. 单测：**用 GDI+ 当独立解码器**把写出的 GIF 读回来，逐帧比对像素（合成帧 → 编码 → 解码 → 一致），
   再断言文件头 / 尺寸 / 帧数 / 循环扩展在字节级正确。
4. 示例：shottool 多一个「录屏」入口（动作 + 快捷键 + 面板按钮），走原有框选 overlay，
   录制态有工具条（计时 / 帧数 / 停止 / 取消），产物落在用户数据目录、文件名带时间戳。
5. 门禁：`build-app shottool` 零警告；`shottool --selftest` 五项全 0；`build-all` 全过；
   `run-tests`（含新增 test_gif.c）过；`check-format`（支持库条数同步）过；`check-layers`
   （新文件分层与基名唯一）过；`ARCHITECTURE.md` 文件清单同步。
6. 真机：录一小段（区域内拖动鼠标产生变化），停后用 GDI+ / 看图确认：
   帧数 ≈ 时长×fps、画面内容对、文件大小合理（脏矩形优化下静止画面应该很小）。

## 步骤

- **P2a kit 层编码器** `runtime/src/kit/cg_kit_gif.c`
  - GIF 字节流：头 `GIF89a` / 逻辑屏描述 / 全局色表 / Netscape 循环扩展 /
    每帧（图形控制扩展 GCE + 图像描述符 + LZW 子块）/ 尾 `0x3B`
  - 调色板：**固定** 216 网页色（6×6×6）+ 39 级灰阶 + 1 个透明索引 = 256；
    就近色用 15 位（5-5-5）**缓存表**懒算，避免每像素 256 次距离运算
  - LZW：哈希字典（key = `prefix<<8 | k`）+ 变长码（9→12 位，满则发 clear 重置）+
    位打包（LSB 优先）+ 255 字节子块
  - **脏矩形优化**：与上一帧比，只编码变化的包围盒；框内没变的像素走透明索引；
    整帧没变则跳过该帧（日志记一笔）
- **P2b win32 层录屏** `runtime/src/win32/cg_win_gif.c`
  - `cg_gif_rec_start`：记区域 + fps，挂一次性定时器（`cg_set_timer`），回调里抓一帧、
    喂编码器、再挂下一次（复用"一次性定时器循环"的既有套路）
  - 抓屏：`BitBlt` 进 **DIB section**（`CreateDIBSection`，直接拿到像素指针，比
    `GetDIBits` 少一次拷贝）
  - `cg_gif_rec_stop(path)`：停表 → `cg_gif_close` 落盘 / `cg_gif_abort` 丢弃
  - `cg_gif_rec_info`：帧数 + 已录毫秒（示例的工具条要显示）
- **P2c 示例** `examples/shottool/main.c` + `shottool.cgui`
  - 新动作 `rec`（accel `Ctrl+Alt+G`）+ 面板按钮「录屏」+ fps 下拉（5/10/15/20，默认 10）
  - 框选复用 `MODE_SELECT`；松手后进 `MODE_REC`（窗口 = 选区 + 工具条，工具条在选区**下方**，
    与标注态同一招：录的是选区矩形，工具条不在画面里）
  - 工具条：计时 / 帧数 / 「停止」「取消」；停止 = 存图 + 回面板 + 气泡；取消 = 丢弃 + 回面板
  - 产物名 `Rec<日期时间>.gif`，落用户数据目录；日志逐条记（开始 / 帧数 / 大小 / 失败原因）
- **P2d 文档**：README（能力边界把"录屏"从"不做"里划掉、加一节用法、关键接口）、
  `教学使用说明.md` §6/§7、`designer/src/supportlib.ts`（收新 API）、`docs/ARCHITECTURE.md`
  （runtime 文件清单）
- **P3 测试与门禁**：`tests/unit/test_gif.c`（合成帧 round-trip + 字节级断言）；
  跑 §spec 5 的门禁清单；真机录一段并核对 §spec 6
- **P4 审查**：`.codebuddy/code-reviews/` 落报告，修红项
- **P5 提交**：代码 + 文档**合成一条**（文档跟着代码走，见 git-commit-style）

## 验证（可复制的命令）

```bat
build-app.bat shottool
examples\shottool\shottool.exe --selftest examples\shottool\shottool.cgui
tools\run-tests.bat                :: 含新的 test_gif.c
tools\check-format.bat
tools\check-layers.mjs
node tools\check-examples.mjs --docs
build-all.bat
```

## 六、实现过程踩到并修掉的真 bug（按发现顺序）

这几个都不是"计划里没想到"，是**写错** —— 记下来免得下次重犯：

1. **图像描述符少写了 packed 字节。** 我按 `put_u16(desc+7, h)` 占了 `desc[7..8]`，
   又把 `desc[8]=0` 当 packed 写（packed 实际在**第 10 字节** `desc[9]`），
   于是描述符只有 9 字节、后面的 LZW 最小码长被解码器当成 packed 读 —— 整个文件报废。
   顺带 `desc[9]` 还越界写（缓冲只声明了 9 字节）。**判据**：GDI+ 直接拒读这个文件。
2. **`FrameDimensionTime` 的 GUID 后 8 字节我从记忆里编错了。** 写成
   `82 A6 83 1A 2A FB 4E 82`，权威值是 **{6aedbd6d-3fb5-418a-83a6-7f45229dc872}**
   （取自 `[System.Drawing.Imaging.FrameDimension]::Time`）。症状极迷惑：
   选第 0 帧成功、选第 1/2 帧只回一个含糊的 `Win32Error(7)`，看不出是 GUID 不对。
   **教训**：GUID / CLSID 这类常量绝不能凭记忆写，要从权威来源取。
3. **收尾时先改名、后关编码器。** 编码器的 `FILE*` 还开着，`MoveFile` 被系统拒绝；
   就算跨盘复制能读走，拿到的也只是已 flush 的那一截 —— 实测报"收尾失败"、
   目标文件只有前 4096 字节、GDI+ 读出来帧数少了一大半。**必须先 fclose 再改名。**
4. **录制期间窗口盖住了要录的区域。** 窗口是置顶的，铺在选区上之后抓屏抓到的是
   我们自己的窗口底 —— 录出来的 300x300 只有 **28 种颜色**，而同一区域的真桌面
   **4729 种颜色**。改成：录制时窗口收成**只有工具条那一行**、摆在选区**下方**。
   修完后同样一次录制得到 **13 帧（跳过 28 帧无变化）、300x300、24943 字节**。

## 七、验证记录（真机，坐标基准对得上时才采信）

| 项 | 证据 |
|---|---|
| 录制窗口位置 | `862x46 @ (600,500)` —— 工具条那一行，正好在选区（600,200 300x300）下方 ✓ |
| 抓帧链路 | 41 次定时器回调（≈10fps × 5s），其中 **28 帧因无变化被跳过**、13 帧真正写入 |
| 成品 | `Rec20261015_000217.gif` **24943 字节**，GDI+ 独立报 **300x300 / 13 帧** ✓ |
| 停止与落盘 | 日志「录屏结束：13 帧（跳过 28 帧无变化），24943 字节 -> …」，面板回到 616x479 ✓ |
| 单测（编码器） | 合成 3 帧 → GDI+ 逐帧逐像素**完全一致**；GDI+ 报的帧数 = 3；脏矩形只写变化块（末帧 12x10 @ 40,30）✓ |
| 脏矩形/跳过 | 整帧没变时 `cg_gif_add` 返回 2；连续两次同帧 → 文件里只有 2 帧 ✓ |

## 回滚

新增两个文件 + 改四个既有文件，没有数据迁移、没有格式变更：
`git revert --no-commit <本条提交>` 即可整体回退；示例的 `.cgui` 只是多一个 action 与几个控件。

## 不能改（本轮禁区）

- **不动** LZW 的既有消费者：本次是全新文件，没有存量 GIF 代码可破坏
- **不动** `cg_set_timer` 的语义（一次性；循环由回调再挂实现）
- **不新增工作线程**（`check-layers` 规则 8：线程只许在 cg_serial/cg_net）
- **不动** `examples/base/**` 与门禁脚本（今天的 base 修复已收口，别再掺和）
- **不引入**新的链接依赖（GDI+ 已在链接列表里；编码器本身零依赖）
