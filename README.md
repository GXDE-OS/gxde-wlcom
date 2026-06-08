# Open Kylin Wayland Compositor (GXDE Forked Version)

---

# 关于本项目

`kylin-wayland-compositor`，亦称`kylin-wlcom`（以下简称「`kywc`」）是一个基于wlroots编写的wayland合成器。

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



# 编译

## 依赖

运行时需要使用的库或程序:

- wayland, libinput, xkbcommon
- libseat, libdrm, libsystemd, librsvg-2.0
- cairo, pango, pangocairo, pixman-1
- gbm, json-c, libudev
- xwayland, xcb (optional)
- ninja-build, libdrm-dev, libxkbcommon-dev, libpixman-1-dev, libgbm-dev, libudev-dev, libseat-dev, libinput-dev, libdisplay-info-dev, hwdata, libegl-dev, libgles2-mesa-dev, libxcb1-dev, libxcb-composite0-dev, libxcb-icccm4-dev, libxcb-render0-dev, libxcb-res0-dev, libxcb-ewmh-dev, libxcb-errors-dev, xwayland



## Wlroots问题

无须担心Wlroots，`meson`会自动从https://gitee.com/GXDE-OS/open-kylin-wlroots.git (我们对Open Kylin版Wlroots的fork) 拉取Open Kylin打过自己补丁的Wlroots，锁定合适的版本并作为子项目构建并静态链接。



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
$ meson compile kylin-wayland-compositor-pot
```



将重新生成的`pot`文件复制成相应语言的`po`文件，如`zh_CN.po`，并进行翻译。

> **需保证**: `Content-Type: text/plain; charset=UTF-8\n`



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