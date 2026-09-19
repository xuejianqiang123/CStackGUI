---
name: c-function-comment
description: 本项目 C 函数的 Doxygen 注释格式。写新 C 函数、改动函数签名/参数/返回值，或审查 C 代码注释规范时必须使用。
argument-hint: [文件路径或函数名]
---

# C 函数 Doxygen 注释格式

**输入**：$ARGUMENTS

本项目 C 函数一律使用下面这种 Doxygen 块注释（用户指定的格式），放在函数定义上方：

```c
/**
 * @brief 读一个文件到堆内存
 * @param path 文件路径
 * @return char* UTF-8 原文，失败返回 NULL（调用方负责 free）
 */
char *cg_read_file(const char *path);
```

## 规则

- `@brief`：一句话说清函数**干什么**，不加句号、不写"这个函数用于"这种废话
- `@param`：**每个参数一行**，`@param 名字 说明`；说明写语义（单位、所有权、取值范围），不要复述类型
  - 无参数的函数不写 `@param`（也不要写"@param 无"）
  - 出参、入出参在说明里写清，如 `@param err 解析失败时写错误信息`
- `@return`：`@return 类型 说明`；说明里写清**取值含义**（0 成功 / -1 失败 / NULL 表示什么），
  内存所有权要写明（谁 free）
- 回调函数（`cg_event_fn` 签名）同样适用，`@param user` 写清是谁挂进去的业务数据
- `static` 内部函数同样适用；头文件声明已有完整注释时，定义处可只留 `@brief` 一行
- **注释与实现不一致时以实现为准**，顺手把注释改对

## 检查清单

- [ ] 新函数带 `@brief` / `@param`（逐个参数）/ `@return`
- [ ] `@return` 写了取值含义与内存所有权，不是复述类型
- [ ] 改了函数签名是否同步更新注释
- [ ] `.cgui` 相关函数的注释里写清对应的字段语义（画布侧 `model.ts` 要对得上）

## 输出

带规范注释的 C 代码（`runtime/`、`examples/`、`apps/` 下的 `.c` / `.h`）。
