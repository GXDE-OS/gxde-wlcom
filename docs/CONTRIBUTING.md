# 如何贡献

1. 将问题提交issues。提交issues暂时没有固定格式，只需要提供一些合理的信息，例如当前的软硬件信息，
执行了什么操作，你预计会发生什么，实际的现象以及复现问题的步骤。
如果可以的话，试着做一些[调试](#调试)，并附上日志输入，方便问题定位。

2. 提交补丁完善代码。如果您希望引入重大更改或新功能，请首先在issues中进行讨论。

# 调试

## 手动运行

进入tty界面，确保原有的桌面环境管已经关闭。

    sudo systemctl stop lightdm

执行以下命令手动启动合成器

    kylin-wlcom -s ukui-session

此时将启动ukui桌面。

## 运程调试

需要安装tmux，在调试机器tty上启动tmux。
在运程机器上通过ssh连接调试机后，通过`tmux a`命令挂接上调试机。

## 嵌套运行

在已有的图形服务器上，终端启动kylin-wlcom，
此时为嵌入式运行状态，要在此合成器上运行客户端，需加上参数:

    WAYLAND_DISPLAY=[display]

[display]可通过日志查看。 

## Backtraces

如果合成器崩溃，可在构建时使用ASAN/UBSAN来回溯

    meson setup build -Dbuildtype=debug -Db_sanitize=address,undefined
    ninja -C build

如果合成器多次崩溃且崩溃场景存在随机性，可使用valgrind工具进行测试

    valgrind /path/kylin-wlcom ...

## Debug日志

查看客户端debug打印信息，可在启动客户端时添加如下指令打印debug信息

    WAYLAND_DEBUG=1

服务端日志默认路径

    $HOME/.log/kylin-wlcom.log

可通过入参`-Dlogtostdout`直接打印日志到stdout

    "Usage: kylin-wlcom [options] [command]"
    "  -d, --debug              Enables full logging, including debug information.\n"
    "  -D, --debug <options>    noxwayland or logtostdout.\n"

设置debug信息输出到屏幕: 

    ./kylin-wlcom -d -Dlogtostdout

## xwayland

不开启xwayland: 

    ./kylin-wlcom -Dnoxwayland

## ukui程序

在终端启动应用前面加入参数:

    QT_QPA_PLATFORM=wayland QT_QPA_PLATFORMTHEME=ukui 

## 输入

使用如下指令来显示输入事件

    sudo libinput debug-events

在终端上，可以使用如下指令来分析键盘事件

    xev -event keyboard
    wev -f wl_keyboard:key

# 代码风格

代码风格由项目附带的`.clang-format`文件控制，可使用clang-format进行格式化，例如

    clang-format -i src/view/workspace.c

# Commit信息

解释commit的原因和commit本身一样重要。像这样来提交commit信息，

	view: mark mapped before configure 

第一行应该:
- 简要描述提交的原因
- 大多数情况下，前缀为被修改的模块或文件名
- 不使用句号

commit信息标题请控制在合理长度内。

