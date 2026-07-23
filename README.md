![](./docs/img/readme-header.png)

![GitHub contributors](https://img.shields.io/github/contributors/GXDE-OS/gxde-wlcom) ![GitHub Release](https://img.shields.io/github/v/release/GXDE-OS/gxde-wlcom) ![Static Badge](https://img.shields.io/badge/license-GPL--1.0--or--later-orange) ![Static Badge](https://img.shields.io/badge/made_with-love-red)

<div align="center">
  <!--
  <a href="https://github.com/othneildrew/Best-README-Template">
    <img src="images/logo.png" alt="Logo" width="80" height="80">
  </a>
  -->

  <h3 align="center">GXDE Wayland合成器</h3>

  <p align="center">
    派生自Kylin Wayland Window Compositor的又一个wlroots系wayland合成器
    <br />
    <a href="https://gitee.com/GXDE-OS/gxde-wlcom/tree/gxde/testing/docs"><strong>查看文档 »</strong></a>
    <br />
    <br />
    <a href="https://gitee.com/GXDE-OS/gxde-wlcom/tags">查看往期版本</a>
    &middot;
    <a href="https://gitee.com/GXDE-OS/gxde-wlcom/issues">报告Bug</a>
    &middot;
    <a href="https://gitee.com/GXDE-OS/gxde-wlcom/issues">请求新功能</a>
  </p>

</div>

## 关于本项目

GXDE Wayland 合成器（亦称 `gxde-wlcom`）是基于 `wlroots` 开发的 Wayland 合成器，其原始代码派生自 `kylin-wayland-compositor`。（以下简称 `kywc`）

本仓库由 GXDE OS 团队fork，并在原项目基础上针对 GXDE OS 进行适配与优化，当前作为 GXDE OS Wayland 会话的默认合成器进行开发与维护。

该项目以开源协议 **GPL-1.0-or-later** 发布，项目中引用或包含的来自其他开源项目的文件及代码片段，均遵照其原始许可证要求进行使用。



### 优点

1. 依赖少，未引入Qt或者GTK等图形框架（内部的QML文件vendor自`deepin-kwin`，仅用作移植参考，实际构建`.deb`包时通过` -Dexamples=false`参数跳过）
2. 按需设计应用与合成器之间的协议，目前协议支持情况请参阅「[PROTOCOLS](./docs/PROTOCOLS.md)」。
3. 特效支持，支持常用的窗口动效。
4. 完整的中文输入支持，支持`input-method v2`和`text-input v1/v2/v3`。
5. 快捷键和触摸手势支持，支持键盘快捷键，触摸板和触摸屏手势设置。
6. 输入设备支持，支持鼠标、键盘、触摸板、触摸屏、数位板。
7. 多语言国际化支持。
8. 多后端支持，支持`x11/wayland`嵌套运行，支持`drm`和`fbdev`显示后端。

### GXDE做出的修改

1. 修改构建，解决依赖问题。
2. 移植DDE Shell/deepin-chameleon主题「云璃」的默认窗体外观。
3. 移植`dde-shell`协议，并扩展`wlr-layer-shell`排布逻辑，为`deepin-menu`等沿用X11思路的菜单守护进程在Wayland下提供菜单定位支持。
4. Cherry pick了上游Wlroots的一些更新。
5. 自动安装`gxde-wlcom`会话与`startgxde_wlcom`启动脚本至系统。
6. 修复了原版Wlcom（截至我们Fork时的版本）在GXDE OS上`layer-shell`表面无法吸附至屏幕顶端的问题。
7. 提供了新接口允许设置GXDE主题。
8. 提供了新接口允许控制GTK标题栏上最小化/最大化/关闭按钮的可见性。（默认为全部可见）
9. 提供了一个接口，允许用户强制裁剪所有CSD（客户端自行装饰的）窗口，使其拥有圆角。用户亦可允许合成器跳过对`layer-shell`表面（这些表面通常包含GXDE顶栏、Dock、GXDE控制中心等）圆角的裁剪。强制裁剪圆角为不稳定功能。
10. 为原来Wlcom的一些功能做了一些alias, 供GXDE控制中心使用（详见[这里](./docs/gxde/manual/dbus.md)）。
11. 参考`deepin-kwin`移植了Deepin风格的多任务视图。



### 依赖项

运行时需要使用的库或程序:

- wayland, libinput, xkbcommon
- libseat, libdrm, libsystemd, librsvg-2.0
- cairo, pango, pangocairo, pixman-1, glib-2.0, gio-2.0
- gbm, json-c, libudev
- xwayland, xcb (可选)



编译时需要的库或程序：

- ninja-build, libdrm-dev, libxkbcommon-dev, libpixman-1-dev, libgbm-dev, libudev-dev, libseat-dev, libinput-dev, libdisplay-info-dev, hwdata, libegl-dev, libgles2-mesa-dev, libxcb1-dev, libxcb-composite0-dev, libxcb-icccm4-dev, libxcb-render0-dev, libxcb-res0-dev, libxcb-ewmh-dev, libxcb-errors-dev, xwayland



### Wlroots问题

无须担心Wlroots，`meson`会自动从https://github.com/GXDE-OS/open-kylin-wlroots.git (我们对Open Kylin版Wlroots的fork) 拉取Open Kylin打过自己补丁的Wlroots，锁定合适的版本并作为子项目构建并静态链接。



为何作为子项目编译？Open Kylin对Wlroots做了大量扩展与修改，并且二进制/devel包名仍然是`wlroots`: 

| 项目        | GXDE自带的Wlroots (25.4) | Open Kylin版本 (0.7.14-ok17) | 是否冲突                                       |
| ----------- | ------------------------ | ---------------------------- | ---------------------------------------------- |
| `.so`二进制 | `libwlroots-0.19.so`     | `libwlroots-0.17.so`         | 侥幸不冲突，但凡有一天这俩版本一旦跟上就会冲突 |
| 头文件      | `/usr/include/wlr`       | `/usr/include/wlr`           | 是，若安装`dev`包将会覆盖                      |



为避免与系统上现有包冲突起见，我们这么做了。



## 开始上手

### 编译

#### (EMACS Flymake/clang用户请看) 初始化Flymake/clang

```bash
$ meson setup build
$ ln -sf build/compile_commands.json compile_commands.json
```

然后重新打开`emacs`。

#### 手动编译 (命令行)

编译选项见`meson_options.txt`，简单的编译指令如下:

```bash
$ meson setup build -Dbuildtype=debugoptimized
$ ninja -C build
$ meson install -C build --skip-subprojects
```



#### 手动编辑 (构建脚本)

构建脚本位于[./build-deb](./build-deb), 这是个shell脚本，用于在调试时生成安装包，便于在调试机器上轻松部署与卸载。



首先先修改脚本权限: 

```bash
$ chmod a+x ./build-deb
```



然后以下上参数帮助:

```bash
用法: ./build-deb <选项>

选项:
  -b, --binary          仅构建二进制包（默认行为）
    -d, --install-deps    先安装构建依赖(读 debian/control)，再构建
    -c, --clean           仅清理构建产物后退出
    -h, --help            打印帮助信息
```



初次编译建议执行:

```bash
$ ./build-deb -d    # 安装依赖并构建
```



以后就可以不用安装依赖了:

```bash
$ ./build-deb    # 直接构建
```



构建完成后清理中间产物: 

```bash
$ ./build-deb -c
```



### 使用

> **注意**: 默认情况下，日志打印到文件`$HOME/.log/kylin-wlcom.log`。



#### 基础使用

程序参数如下: 

```bash
Usage: kylin-wlcom [options] [command]
  -h, --help               Show help message and quit.\n
  -d, --debug              Enables full logging, including debug information.\n
  -D, --debug <options>    noxwayland or logtostdout.\n
  -s, --session <process>  Run session on startup\n
  -v, --version            Show the version number and quit.\n
  -V, --verbose            Enables more verbose logging.\n
```



通过`-D`参数可以方便运行时调试, 支持参数如下: 

```bash
-Dnoxwayland    关闭xwayland支持
-Dlogtostdout   将日志打印到stdout
-Dloginmtime    使用monotonic time输出日志
```



#### 在GXDE上建立kywc会话

~~请参阅「[./docs/gxde/gxde-wlcom-session.md](./docs/gxde/depreciated/gxde-wlcom-session.md)」，了解如何在GXDE上建立kywc会话。~~

现在GXDE Wlcom会在安装`.deb`包时自动安装会话文件，不再需要手动安装，相关的`.desktop`文件与启动脚本可以在本repo的`data/`下找到。



#### GXDE版本的特殊功能

##### 多任务视图

按`Win+S`打开或关闭多任务视图。当前实现提供：

- 根据`com.deepin.wrap.gnome.desktop.background`的`picture-uri`设置显示桌面及工作区壁纸预览；
- 带抗锯齿圆角和3px活动高亮线的工作区预览；
- 显示普通及最小化窗口的实时缩略图；
- 关闭窗口、切换窗口置顶状态；
- 添加、删除、切换工作区；
- 将窗口拖入其他工作区，释放后保持多任务视图开启；
- 拖拽工作区预览以重新排序工作区。

Deepin KWin的原始实现和资源保存在
`src/vendor/dkwin/multitask/upstream/`，Wlcom适配层位于
`src/vendor/dkwin/multitask/wlcom_multitask.c`。

GXDE菜单中的「多任务视图」启动器也可直接使用，无需修改桌面文件。
`gxde-wlcom`会在用户会话总线上提供`com.deepin.wm`，并将
`/com/deepin/wm`上的`PerformAction(1)`直接连接到同一个原生多任务视图
开关。接口契约与验证方法见
[multitasking-launcher-interface.md](./docs/gxde/manual/multitasking-launcher-interface.md)。

##### 设置GTK主题

在用户会话总线，我们提供了`top.gxde.Wlcom.Theme`接口，可以通过其`SetGTK`方法，设置已安装的主题。

GNOME与UKUI的主题设置会同步更改，更改应该立即可见。

以下是使用示例，您需要把`主题名称`换为本机真实存在的主题。

```bash
busctl --user call \
  top.gxde.Wlcom.Theme \
  /top/gxde/Wlcom/Theme \
  top.gxde.Wlcom.Theme \
  SetGTK s "主题名称"
```

该方法接收一个字符串参数并返回boolean。返回`true`代表所有可用设置项均已成功写入。

##### 设置GTK窗口按钮的可见性

`top.gxde.Wlcom.WindowBtn`接口用于设置GTK窗口的最小化、最大化和关闭按钮是否显示。使用三个boolean参数对应这三个按钮是否显示。

以下是例子 --

设置最小化/最大化/关闭按钮都需要显示：

```bash
busctl --user call \
  top.gxde.Wlcom.WindowBtn \
  /top/gxde/Wlcom/WindowBtn \
  top.gxde.Wlcom.WindowBtn \
  SetGtkDecorationButtons bbb true true true
```

查询当前设置：

```bash
busctl --user call \
  top.gxde.Wlcom.WindowBtn \
  /top/gxde/Wlcom/WindowBtn \
  top.gxde.Wlcom.WindowBtn \
  GetGtkDecorationButtons
```

##### 强制裁剪圆角 （不稳定）

> 强制裁剪圆角与Wlcom所支持的窗口圆角不同，在强制裁剪圆角下，所有CSD（客户端自行装饰的）窗口都会被强制裁剪圆角，圆角大小取决于Wlcom设置的窗口圆角大小（即数值与普通「启用窗口圆角」功能共享）

在用户会话总线，我们提供了`top.gxde.Wlcom.WindowCorner`接口，用于管理两个持久化的DBus配置：

- `ForceRoundCorner`：强制裁剪窗口圆角。
- `ForceRoundCornerExcludeLayerShell`：启用强制裁剪时，不裁剪
  `wlr-layer-shell`表面（例如顶栏、Dock、GXDE控制中心等的窗体）。仅在`ForceRoundCorner`启用时生效。

###### 启用强制圆角裁切

```bash
busctl --user call \
  top.gxde.Wlcom.WindowCorner \
  /top/gxde/Wlcom/WindowCorner \
  top.gxde.Wlcom.WindowCorner \
  SetForceRoundCorner b true
```

将上述命令中的`true`改为`false`即可关闭相应配置。可通过以下方法查询
当前值：

```bash
busctl --user call \
  top.gxde.Wlcom.WindowCorner \
  /top/gxde/Wlcom/WindowCorner \
  top.gxde.Wlcom.WindowCorner \
```

###### 启用强制裁剪，但排除`wlr-layer-shell`表面：

```bash
busctl --user call \
  top.gxde.Wlcom.WindowCorner \
  /top/gxde/Wlcom/WindowCorner \
  top.gxde.Wlcom.WindowCorner \
  SetForceRoundCornerExcludeLayerShell b true
```

##### 注意事项

配置修改后立即生效，并写入`~/.config/gxde-wlcom/config.json`的`theme`对象，对应的key分别为`force_round_corner`和`force_round_corner_exclude_layer_shell`。



##   多语言支持

在`po`目录中，`LINGUAS`文件中加入支持的语言，`POTFILES.in`加入需要翻译的源文件。

然后运行以下命令，更新`pot`文件:

\```bash

$ meson compile gxde-wlcom-pot

\```



## 里程碑

- [ ] 添加「显示桌面」支持

- [ ] 完成对Treeland协议的完整兼容



## 参与开发

请参阅「[CONTRIBUTING](./docs/CONTRIBUTING.md)」文件，了解贡献所需的信息。

### GXDE Wlcom的贡献者们

*(注：不知道为何很多原版KYWC的贡献者没有显示，您可以在[此处](https://gitee.com/openkylin/kylin-wayland-compositor/contributors?ref=debian%2Funstable)找到原版KYWC贡献者的信息）*

<a href="https://github.com/othneildrew/Best-README-Template/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=GXDE-OS/gxde-wlcom" alt="contrib.rocks image" />
</a>



## 许可证

该项目使用开源协议`GPL-1.0-or-later`

项目中来源于其他开源项目的文件或代码片段遵守原开源协议要求。

请参阅`./LICENSES/`文件夹下的许可。

对于每一个源码，也请查看其顶部标明的`SPDX-License-Identifier`。



# 致谢

感谢以下代码与模板提供参考:

* **Treeland**: https://github.com/linuxdeepin/treeland
* **Treeland Protocols**: https://github.com/linuxdeepin/treeland-protocols
* **Open Kylin Wlcom**: https://gitee.com/openkylin/kylin-wayland-compositor
* **Open Kylin Wlroots**: https://gitee.com/openkylin/wlroots
* **Deepin KWin**: https://github.com/linuxdeepin/deepin-kwin
* **Wlroots**: https://gitlab.freedesktop.org/wlroots/wlroots
* **Sway**: https://github.com/swaywm
* **Wayfire**: https://github.com/wayfire
* **LabWC**: https://github.com/labwc
* **Best Readme Template**: https://github.com/othneildrew/Best-README-Template
