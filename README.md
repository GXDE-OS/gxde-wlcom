# GXDE Wayland Compositor | GXDE Wayland合成器

---

> 🎉 **测试版本`1.4.0-gxde1`现已发布!!**
>
> 这并不是一个稳定版本，阅读「[发布笔记： 1.4.0-gxde1](./docs/gxde/release_notes/1.4.0-gxde1.md)」以了解详情。



# 关于本项目

GXDE Wayland合成器（亦称`gxde-wlcom`）派生自`kylin-wayland-compositor`，亦称`kylin-wlcom`（以下简称「`kywc`」）是一个基于wlroots编写的wayland合成器。

此仓库由GXDE团队fork并为GXDE适配，作为GXDE Wayland适配计划中备选的合成器选项之一。

该项目使用开源协议`GPL-1.0-or-later`，项目中来源于其他开源项目的文件或代码片段遵守原开源协议要求。



# 功能和特点

## 原版的特点

1. 依赖少，未使用QT或者GTK等图形开发框架。
2. 按需设计应用与合成器之间的协议，目前协议支持情况请参阅「[PROTOCOLS](./docs/PROTOCOLS.md)」。
3. 特效支持，支持常用的窗口动效。
4. 完整的中文输入支持，支持`input-method v2`和`text-input v1/v2/v3`。
5. 快捷键和触摸手势支持，支持键盘快捷键，触摸板和触摸屏手势设置
6. 输入设备支持，支持鼠标、键盘、触摸板、触摸屏、数位板
7. 多语言国际化支持
8. 多后端支持，支持`x11/wayland`嵌套运行，支持`drm`和`fbdev`显示后端



## GXDE做出的修改

1. 修改构建，解决依赖问题
2. 移植DDE Shell/deepin-chameleon主题「云璃」的默认窗体外观
3. 移植`dde-shell`协议 (部分)，并扩展`wlr-layer-shell`排布逻辑，为`deepin-menu`等沿用X11思路的菜单守护进程在Wayland下提供菜单定位支持

### GXDE的一些实验性功能
#### 设置GTK主题

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

#### 强制裁剪圆角
> 强制裁剪圆角与Wlcom所支持的窗口圆角不同，在强制裁剪圆角下，所有CSD（客户端自行装饰的）窗口都会被强制裁剪圆角，圆角大小取决于Wlcom设置的窗口圆角大小（即数值与普通「启用窗口圆角」功能共享）

在用户会话总线，我们提供了`top.gxde.Wlcom.WindowCorner`接口，用于管理两个持久化的DBus配置：
- `ForceRoundCorner`：强制裁剪窗口圆角。
- `ForceRoundCornerExcludeLayerShell`：启用强制裁剪时，不裁剪
  `wlr-layer-shell`表面（例如顶栏、Dock、GXDE控制中心等的窗体）。仅在`ForceRoundCorner`启用时生效。

##### 启用强制圆角裁切

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

##### 启用强制裁剪，但排除`wlr-layer-shell`表面：
```bash
busctl --user call \
  top.gxde.Wlcom.WindowCorner \
  /top/gxde/Wlcom/WindowCorner \
  top.gxde.Wlcom.WindowCorner \
  SetForceRoundCornerExcludeLayerShell b true
```

##### 注意事项
配置修改后立即生效，并写入`~/.config/gxde-wlcom/config.json`的`theme`对象，对应的key分别为`force_round_corner`和`force_round_corner_exclude_layer_shell`。


# 编译

## (EMACS Flymake/clang用户请看) 初始化Flymake/clang
```bash
$ meson setup build
$ ln -sf build/compile_commands.json compile_commands.json
```

然后重新打开`emacs`。

## 依赖

运行时需要使用的库或程序:

- wayland, libinput, xkbcommon
- libseat, libdrm, libsystemd, librsvg-2.0
- cairo, pango, pangocairo, pixman-1
- gbm, json-c, libudev
- xwayland, xcb (optional)
- ninja-build, libdrm-dev, libxkbcommon-dev, libpixman-1-dev, libgbm-dev, libudev-dev, libseat-dev, libinput-dev, libdisplay-info-dev, hwdata, libegl-dev, libgles2-mesa-dev, libxcb1-dev, libxcb-composite0-dev, libxcb-icccm4-dev, libxcb-render0-dev, libxcb-res0-dev, libxcb-ewmh-dev, libxcb-errors-dev, xwayland



## Wlroots问题

无须担心Wlroots，`meson`会自动从https://github.com/GXDE-OS/open-kylin-wlroots.git (我们对Open Kylin版Wlroots的fork) 拉取Open Kylin打过自己补丁的Wlroots，锁定合适的版本并作为子项目构建并静态链接。



为何作为子项目编译？Open Kylin对Wlroots做了大量扩展与修改，并且二进制/devel包名仍然是`wlroots`: 

| 项目        | GXDE自带的Wlroots (25.4) | Open Kylin版本 (0.7.14-ok17) | 是否冲突                                       |
| ----------- | ------------------------ | ---------------------------- | ---------------------------------------------- |
| `.so`二进制 | `libwlroots-0.19.so`     | `libwlroots-0.17.so`         | 侥幸不冲突，但凡有一天这俩版本一旦跟上就会冲突 |
| 头文件      | `/usr/include/wlr`       | `/usr/include/wlr`           | 是，若安装`dev`包将会覆盖                      |



为避免与系统上现有包冲突起见，我们这么做了。



## 手动编译 (命令行)

编译选项见`meson_options.txt`，简单的编译指令如下:
```bash
$ meson setup build -Dbuildtype=debugoptimized
$ ninja -C build
$ meson install -C build --skip-subprojects
```



## 手动编辑 (构建脚本)

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





## 调试

> **注意**: 默认情况下，日志打印到文件`$HOME/.log/kylin-wlcom.log`。



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



# 多语言支持

在`po`目录中，`LINGUAS`文件中加入支持的语言，`POTFILES.in`加入需要翻译的源文件。



然后运行以下命令，更新`pot`文件: 
```bash
$ meson compile gxde-wlcom-pot
```



将重新生成的`pot`文件复制成相应语言的`po`文件，如`zh_CN.po`，并进行翻译。

> **需保证**: `Content-Type: text/plain; charset=UTF-8\n`



# 在GXDE上建立kywc会话

~~请参阅「[./docs/gxde/gxde-wlcom-session.md](./docs/gxde/depreciated/gxde-wlcom-session.md)」，了解如何在GXDE上建立kywc会话。~~

现在GXDE Wlcom会在安装`.deb`包时自动安装会话文件，不再需要手动安装，相关的`.desktop`文件与启动脚本可以在本repo的`data/`下找到。



# 已知问题

请参阅「[KNOWN_ISSUES](./docs/KNOWN_ISSUES.md)」文件，了解已知问题。



# 参与开发

请参阅「[CONTRIBUTING](./docs/CONTRIBUTING.md)」文件，了解贡献所需的信息。



# 原版README

原版README可以在「[./README.original.md](./README.original.md)」下找到。



# 许可

该项目使用开源协议`GPL-1.0-or-later`

项目中来源于其他开源项目的文件或代码片段遵守原开源协议要求。

请参阅`./LICENSES/`文件夹下的许可。

对于每一个源码，也请查看其顶部标明的`SPDX-License-Identifier`。



# 致谢

感谢以下代码提供参考: 

* **Open Kylin Wlcom**: https://gitee.com/openkylin/kylin-wayland-compositor
* **Open Kylin Wlroots**: https://gitee.com/openkylin/wlroots
* **Wlroots**: https://gitlab.freedesktop.org/wlroots/wlroots
* **Sway**: https://github.com/swaywm
* **Wayfire**: https://github.com/wayfire
* **LabWC**: https://github.com/labwc
