# kylin-wayland-compositor

kylin-wayland-compositor或kylin-wlcom（以下简称kywc）是一个基于wlroots编写的wayland合成器。

目前积极开发中，并作为默认显示服务器随openKylin系统发布。

该项目使用开源协议GPL-1.0-or-later，项目中来源于其他开源项目的文件或代码片段遵守原开源协议要求。


## 功能和特点

1. 依赖少，未使用QT或者GTK等图形开发框架。

2. 按需设计应用与合成器之间的协议，目前协议支持情况[PROTOCOLS]。

3. 特效支持，支持常用的窗口动效。

4. 完整的中文输入支持，支持input-method v2和text-input v1/v2/v3。

5. 快捷键和触摸手势支持，支持键盘快捷键，触摸板和触摸屏手势设置

6. 输入设备支持，支持鼠标、键盘、触摸板、触摸屏、数位板

7. 多语言国际化支持

8. 多后端支持，支持x11/wayland嵌套运行，支持drm和fbdev显示后端

## 编译

运行时需要使用的库或程序:

- wlroots, wayland, libinput, xkbcommon
- libseat, libdrm, libsystemd, librsvg-2.0
- cairo, pango, pangocairo, pixman-1
- gbm, json-c, libudev
- xwayland, xcb (optional)

依赖安装可通过apt进行（配置了deb-src源）

    apt build-dep kylin-wayland-compositor

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

## 多语言支持

在`po`目录中，`LINGUAS`文件中加入支持的语言，`POTFILES.in`加入需要翻译的源文件。

然后运行以下命令，更新`pot`文件：
```
    meson compile kylin-wayland-compositor-pot
```

将重新生成的`pot`文件复制成相应语言的`po`文件，如`zh_CN.po`，并进行翻译。

> 保证："Content-Type: text/plain; charset=UTF-8\n"

## 已知问题

请参阅[KNOWN_ISSUES]文件，了解已知问题。

## 参与开发

请参阅[CONTRIBUTING]文件，了解向kywc贡献所需的信息。

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

[PROTOCOLS]: docs/PROTOCOLS.md
[KNOWN_ISSUES]: docs/KNOWN_ISSUES.md
[CONTRIBUTING]: docs/CONTRIBUTING.md
