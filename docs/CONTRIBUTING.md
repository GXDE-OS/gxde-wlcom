
# 如何贡献

1. 将问题提交issues。提交issues暂时没有固定格式，只需要提供一些合理的信息，例如执行了什么操作，你预计会发生什么，实际的现象以及复现问题的步骤。如果可以的话，试着做一些[调试](#调试)，并附上日志输入，方便问题定位。

2. 提交补丁完善代码。如果您希望引入重大更改或新功能，请首先在issues中进行讨论。

# 调试

## Backtraces

如果合成器崩溃，可在构建时使用ASAN/UBSAN来回溯

    meson setup build -Dbuildtype=debug -Db_sanitize=address,undefined
    ninja -C build

## Debug日志

查看客户端debug打印信息，可在启动客户端时添加如下指令打印debug信息

    WAYLAND_DEBUG=1

服务端日志默认路径

    $HOME/.log/kylin-wlcom.log

可通过入参`-Dlogtostdout`直接打印日志到stdout

    "Usage: kylin-wlcom [options] [command]"
    "  -d, --debug              Enables full logging, including debug information.\n"
    "  -D, --debug <options>    noxwayland or logtostdout.\n"

## 输出

如果显示渲染输出存在问题，可在编译时选择kylin-wlcom的scene实现，进行基本的定位

    meson setup build -Dwlr_scene=true -Dky_scene=false

## 输入

使用如下指令来显示输入事件

    sudo libinput debug-events

在终端上，可以使用如下指令来分析键盘事件

    xev -event keyboard
    wev -f wl_keyboard:key

# 代码风格

代码风格参照linux内核代码，并使用clang工具进行调整，例如

    clang-format -i src/view/workspace.c

# Commit信息

解释commit的原因和commit本身一样重要。像这样来提交commit信息，

	view: mark mapped before configure 

第一行应该:
- 简要描述提交的原因
- 大多数情况下，前缀为被修改的模块或文件名
- 不使用句号

commit信息标题请控制在74个字符以内。

