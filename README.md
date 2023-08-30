# kylin-wayland-compositor

## 介绍

### 基本情况

kylin-wayland-compositor(以下简称kywc)是一个基于wlroots的wayland合成器，目前已完成基本功能，正在适配openKylin系统。

### 特点

1. 依赖少

不依赖任何成熟的桌面环境，比如KDE、GNOME；

不依赖任何UI工具包，比如QT或者GDK。只需要一些基础的库，例如pixman、libdrm、xkbcommon、cairo、librsvg等。

2. 定制协议更便捷

可以按需设计应用与合成器之间的协议。合成器原生支持定制协议，减少因协议库更新不同步或者版本冲突带来的问题。

3. 特效支持

支持最大化最小化等特效，以插件形式加载。

### 功能支持情况

kywc完成了一个在openKylin系统上的预览版，能够进行一些基本常用的操作，支持ukui-session运行。

在功能方面支持: 键鼠与触摸板、窗口焦点管理、窗口移动管理、窗口尺寸管理、窗口层级管理、工作区管理、坐标管理、服务端与客户端装饰等。

在系统设置方面支持: 光标设置、显示器设置、主题设置。

在动画、装饰性渲染支持: 最大化、最小化。

### 后续计划

1. 适配openKylin系统。

2. 支持更多X应用兼容。

## 使用

### 依赖

运行时需要使用的库或程序:

- wlroots, wayland, libinput, xkbcommon
- libxml2, libseat, libudev, libdrm, libsystemd, librsvg-2.0
- cairo, pango, pangocairo, pixman-1
- gbm, pkg-config, hwdata, json-c, dl, msgfmt
- xwayland, xcb (optional)

编译时需要使用的库或程序:

- meson, ninja, gcc/clang
- wayland-protocols

### 编译

简单的编译指令:

    meson build/
    ninja -C build/

### 打包

发行版将能在openKylin社区中查询到。

### 使用方法

#### openKylin使用

在openKykin上使用kywc合成器时，可直接拉起使用 ./kylin-wlcom；
此时启动客户端需加上 `WAYLAND_DISPLAY=wayland-1` 参数。

或者先关闭openKylin上的 lightdm 服务，
然后关闭当前的 kwin_wayland 合成器，
最后拉起 kywc 即可。

#### 参数说明

    "Usage: kylin-wlcom [options] [command]"
    "  -h, --help               Show help message and quit.\n"
    "  -d, --debug              Enables full logging, including debug information.\n"
    "  -D, --debug <options>    noxwayland or logtostdout.\n"
    "  -s, --session <process>  Run session on startup\n"
    "  -v, --version            Show the version number and quit.\n"
    "  -V, --verbose            Enables more verbose logging.\n"

### 调试方法

#### 编译参数

添加内存泄露检测

    meson build/ -Db_sanitize=address,undefined

#### 打印

通过入参改变debug打印等级，见 [参数说明](#参数说明) 。

#### 日志文件

日志文件路径

    $HOME/.log/kylin-wlcom.log

### 已知问题

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




[wlroots]: https://gitlab.freedesktop.org/wlroots/wlroots
[sway]: https://github.com/swaywm
[wayfire]: https://github.com/wayfire

[KNOWN_ISSUES]: docs/KNOWN_ISSUES.md
[CONTRIBUTING]: docs/CONTRIBUTING.md
[FAQ]: docs/FAQ.md