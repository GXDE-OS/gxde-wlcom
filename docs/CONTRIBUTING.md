
# 如何贡献

1. 将问题提交issues。提交issues不限定格式，而是需要提供一些合理的信息，例如执行了什么操作，你预计会发生什么，实际的现象以及复现问题的步骤。请尝试使用默认配置。如果可以的话，试着做一些[调试](#调试)。

2. 提交补丁完善代码。如果您希望引入重大更改或新功能，请首先在issues中进行讨论。

# 调试

## Backtraces

如果合成器崩溃，可在构建时使用ASAN/UBSAN来回溯

    meson -Db_sanitize=address,undefined build/
    ninja -C build/

## Debug日志

查看客户端debug打印信息，可在启动客户端时添加如下指令打印debug信息

    WAYLAND_DEBUG=1

服务端debug日志默认路径

    $HOME/.log/kylin-wlcom.log

可通过入参直接打印debug信息

    "Usage: kylin-wlcom [options] [command]"
    "  -d, --debug              Enables full logging, including debug information.\n"
    "  -D, --debug <options>    noxwayland or logtostdout.\n"

## 输出

如果你认为damage有问题，可这样运行kylin-wlcom

    WLR_SCENE_DEBUG_DAMAGE=highlight ./kylin-wlcom

若要排除运行时的驱动问题，可按如下方式运行

    WLR_RENDERER=pixman ./kylin-wlcom

## 输入

使用如下指令来显示输入事件

    sudo libinput debug-events

在终端上，可以使用如下指令来分析键盘事件

    xev -event keyboard
    wev -f wl_keyboard:key

# 代码风格

代码风格参照linux内核代码，并使用clang工具进行调整，例如

    clang-format-15 -i src/view/workspace.c

# Commit信息

解释commit的原因和commit本身一样重要。像这样来提交commit信息，

	view: mark mapped before configure 

第一行应该:
- 简要描述提交的原因
- 大多数情况下，前缀为被修改的模块或文件名
- 不使用句号

commit信息标题请控制在74个字符以内。

# 版本升级

通常只有首席维护人员会升级，但为了不忘记任何关键步骤，或在其他人有需要使用的情况下，请遵循如下步骤:

1. 如果合适，请更新 `subprojects/wlroots.wrap` 中的 `revision` ，并且运行
 `git commit -m 'wlroots.wrap: use A.B.C'`
2. 更新发布的 `NEWS.md` 并且运行 `git commit -m 'NEWS.md: update notes on X.Y.Z'`
3. 在 `meson.build` 中更新版本和wlroots库版本(如果需要更新wlroots)，然后运行
  `git commit -m 'build: bump version to X.Y.Z'`
4. 运行 `git tag -a X.Y.Z` ，commit消息的第一行应该是版本号，正文应该是从标题中删除散列字符(#)的 `NEWS.md` 添加，否则这些字符将被git忽略
5. 在仓库中新建一个 'Release' 来发行版本
6. 更新手册页

