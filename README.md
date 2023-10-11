# kylin-wayland-compositor


kylin-wayland-compositor或kylin-wlcom（以下简称kywc）是一个基于wlroots编写的wayland合成器，目前积极开发中，正在适配openKylin系统。

该项目使用开源协议Mulan PSL v2，项目中来源于其他开源项目的文件或代码片段遵守原开源协议要求。


## 特点

1. 依赖少，未使用QT或者GTK进行UI编写。只需要一些基础的库，例如pixman、cairo、pango、librsvg等。

2. 按需设计应用与合成器之间的协议，可方便快捷增添协议，减少因协议库更新不同步或者版本冲突带来的问题。

3. 特效支持，支持最大化最小化等特效，以插件形式加载。

4. 完整的中文输入支持，支持input-method v2和text-input v1/v3，支持input popup，支持chromium/electron应用

5. 快捷键和触摸手势支持，支持键盘快捷键，触摸板和触摸屏手势设置

6. 输入设备支持，支持鼠标、键盘、触摸板、触摸屏、数位板

7. 多语言国际化支持

## 功能支持

kywc完成了一个在openKylin系统上的预览版，能够进行一些基本常用的操作，支持ukui-session运行。

可支持基本的窗口管理功能，输入设备设置，显示输入设置等，支持基本的窗口特效。

## 后续计划

1. 功能完善，继续适配openKylin系统

2. x11应用场景兼容

3. 性能优化

4. 硬件适配

## 编译

运行时需要使用的库或程序:

- wlroots, wayland, libinput, xkbcommon
- libseat, libdrm, libsystemd, librsvg-2.0
- cairo, pango, pangocairo, pixman-1
- gbm, pkg-config, hwdata, json-c, msgfmt
- xwayland, xcb (optional)

编译时需要使用的库或程序:

- meson, ninja, gcc/clang

编译选项见`meson_options.txt`，简单的编译指令:
```
    meson setup build -Dbuildtype=debugoptimized
    ninja -C build
    meson install -C build --skip-subprojects
```

程序参数如下：

    "Usage: kylin-wlcom [options] [command]"
    "  -h, --help               Show help message and quit.\n"
    "  -d, --debug              Enables full logging, including debug information.\n"
    "  -D, --debug <options>    noxwayland or logtostdout.\n"
    "  -s, --session <process>  Run session on startup\n"
    "  -v, --version            Show the version number and quit.\n"
    "  -V, --verbose            Enables more verbose logging.\n"

通过-D参数可以方便运行时调试, 支持参数如下：
```
    -Dnoxwayland    关闭xwayland支持
    -Dlogtostdout   将日志打印到stdout
    -Dloginmtime    使用monotonic time输出日志
```

默认情况下，日志打印到文件`$HOME/.log/kylin-wlcom.log`。

## openKylin使用

增加ppa，安装kylin-wayland-compositor即可。
如需要特效，则还需安装kylin-wayland-compositor-effects。

ppa地址如下：
```
    deb http://ppa.build.openkylin.top/kylinsoft/wayland-next/openkylin yangtze main
    deb-src http://ppa.build.openkylin.top/kylinsoft/wayland-next/openkylin yangtze main
```
安装结束后，注销系统，在登陆界面，选择`Kylin Wlcom`。

## 已知问题

请参阅[KNOWN_ISSUES]文件，了解已知问题。

## 参与开发

请参阅[CONTRIBUTING]文件，了解向kywc贡献所需的信息。

## FAQ

请参阅[FAQ]文件，了解更多。

## 致谢

感谢以下代码提供参考：

[wlroots]

[sway]

[wayfire]

[labwc]


[wlroots]: https://gitlab.freedesktop.org/wlroots/wlroots
[sway]: https://github.com/swaywm
[wayfire]: https://github.com/wayfire
[labwc]: https://github.com/labwc

[KNOWN_ISSUES]: docs/KNOWN_ISSUES.md
[CONTRIBUTING]: docs/CONTRIBUTING.md
[FAQ]: docs/FAQ.md


[wlroots]: https://gitlab.freedesktop.org/wlroots/wlroots
[sway]: https://github.com/swaywm
[wayfire]: https://github.com/wayfire

[KNOWN_ISSUES]: docs/KNOWN_ISSUES.md
[CONTRIBUTING]: docs/CONTRIBUTING.md
[FAQ]: docs/FAQ.md
