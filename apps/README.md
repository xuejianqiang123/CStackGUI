# apps\ —— 你自己的界面工程放这里

画布（`CStackGUI.exe`）点「导出 C 骨架」时，默认就导到这个目录下的 `apps\<名字>\`。

```
apps\<名字>\
  名字.cgui            画布存出来的界面
  名字_ui_events.c     ← 你要写业务逻辑的文件（重新导出不会覆盖它）
  名字_ui_events.h     生成物
  main.c               生成物
  build-app.bat        生成物：本工程的编译入口
  lib\libcstackgui.a   编译时自动放的运行时库副本
  名字.exe             编译产物，双击即跑
```

在项目根目录编译：

```bat
build-app.bat <名字>
```

**注意**：仓库自带的两个示例不在这个目录，它们在 `examples\` 下
（`examples\hello` 是基础控件，`examples\gallery` 是控件陈列室）。
这个目录是留给你的，别把它当成示例目录。
