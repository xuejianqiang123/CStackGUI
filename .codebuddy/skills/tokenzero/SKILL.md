---
name: tokenzero
description: 输入侧上下文压缩（本地 npm CLI）。把目录树打包成紧凑上下文文件、无损压缩 markdown/纯文本空白、JSON 数组转紧凑表格、分析并估算可省 token。代码块/URL/邮箱/数字/日期/标识符原样保留。触发条件：用户说"打包上下文/pack 项目"、"压缩这个 md/文档喂给 LLM"、"JSON 转表格省 token"、"分析能省多少 token"、"tokenzero"时手动触发。默认不自动触发。
auto_invoke: false
argument-hint: [pack <paths> | compress <file> | json <file> | analyze <paths> | init]
---

# TokenZero —— 输入侧上下文压缩

**输入**：$ARGUMENTS

本地运行的上下文优化工具（Node CLI + 库）。在把文件/目录树/JSON/markdown 发给 LLM **之前**做无损压缩与整理，减少**输入 Token**。完全本地，无需 API key，不调外部 LLM。

## 定位（与 caveman 正交）

- **caveman**：压**输出**自然语言（AI 回复文字）。
- **tokenzero**：压**输入/上下文**（喂给 LLM 的文件）。
- 两者互补，不冲突。本 skill 只管上下文准备，不改 AI 表达风格。

## 核心原则（红线）

1. **无损，不改语义**：只压缩空白与结构，**绝不摘要/改写/改变含义**。
2. **技术实体原样保留**：代码块、URL、邮箱、数字、日期、标识符一字不改。压缩只作用于散文空白/结构。
3. **估算仅供相对参考**：token≈字符数/4，非 Anthropic/OpenAI 真实分词，**不可作计费依据**。
4. **手动触发**：准备上下文是主动操作，默认不自动触发（`auto_invoke: false`）。

## 环境（本项目已确认）

- Node v24 + npx 11 可用（Windows + Git Bash）。**优先走 CLI**，免安装：`npx tokenzero <cmd>`。
- 若离线/无法拉包，降级为"手动模式"：agent 按下述规则直接产出等价结果（见末节）。

## 四大能力（CLI）

### 1. 打包目录树（pack）
读文件夹→应用忽略规则→输出单个紧凑上下文文件，并打印压缩前/后/近似节省。
```
npx tokenzero pack . --out .tokenzero/context.md
```
- `--out <path>`：写文件（否则输出 stdout）
- `--max-bytes <n>`：跳过大于 n 字节文件（默认 524288=512KB）
- `--allow-large`：强制含大文件
- `--no-text` / `--no-json`：跳过文本空白压缩 / JSON 压缩

用法：喂给 LLM 时引用 `@.tokenzero/context.md`。

### 2. 压缩单文件（compress）
安全压缩单个文本/markdown 的空白与结构。**保留不动**：代码块、URL、邮箱、数字、日期、标识符。
```
npx tokenzero compress prompt.md --out compressed.md
```

### 3. JSON 转紧凑表格（json）
结构统一的对象数组 → 表格；否则输出紧凑 JSON。
```
npx tokenzero json data.json
```
示例输出：
```
cols: name | status | owner | priority
Project Alpha | active | Alice | high
Project Beta | paused | Bob | medium
```

### 4. 分析估算（analyze）
输出：总文件数/含/忽略、最大文件、可表格化的 JSON、推荐排除项、预估字符节省。
```
npx tokenzero analyze .
```

## 初始化 / 装为 Skill

```
npx tokenzero init                 # 建 .tokenzero/config.json + .tokenzeroignore
npx tokenzero init --claude-code   # 额外建 .claude/skills/tokenzero/SKILL.md
```
注：本项目已有本 SKILL.md，`init` 仅用于生成 config/ignore，无需再装 Claude 版。

## 安全提示（proxy 命令）

`npx tokenzero proxy` 会起本地中间人代理（127.0.0.1:3000），拦截并改写发往 api.anthropic.com 的请求。文档称 key 透传不落地，但**未经源码审查勿用于生产**。本 skill 默认**不使用 proxy**——高风险，需用户显式要求并先审源码。

## 降级：手动模式（无 npx 时）

agent 直接产出等价结果：
- **pack**：用 list_files+read_file 汇总为一个 md，去多余空行/尾随空白，代码块/URL/路径不动。
- **compress**：只删连续空行、行尾空白、markdown 冗余分隔；代码块/URL/数字/日期/标识符不动。
- **json→表格**：均质对象数组转 `cols:` 管道分隔表；异质则紧凑单行 JSON。
- **analyze**：读目录，报文件数/大文件/可表格化 JSON/预估节省(字符/4)。

## 使用限制

- 只减**输入** Token；不压输出、不压推理。输出压缩用 caveman。
- 节省量对短小上下文不明显；大目录/长文档/大 JSON 场景收益显著。
- 无损压缩，安全用于喂 LLM；但估算数字仅相对参考。

## 输出

- CLI 模式：产出 `.tokenzero/context.md`（pack）/ `compressed.md`（compress）/ 表格文本（json）/ 分析报告（analyze）。
- 手动模式：等价产物直接给出或写入指定路径。无独立报告文件。
