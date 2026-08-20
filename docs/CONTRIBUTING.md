# 如何贡献

1. 将问题提交issues。提交issues暂时没有固定格式，只需要提供一些合理的信息，例如当前的软硬件信息，
执行了什么操作，你预计会发生什么，实际的现象以及复现问题的步骤。
如果可以的话，试着做一些[调试](#调试)，并附上日志输入，方便问题定位。

2. 提交补丁完善代码。如果您希望引入重大更改或新功能，请首先在issues中进行讨论。

---

请将代码PR至`gxde/testing`分支，测试稳定后将由管理员合并至`gxde/zhuangzhuang`分支并bump。

Please set the merge request destination to `gxde-testing`, and the administrator of the repository will merge your changes to `gxde-zhuangzhuang` once it is proved that your changes are stable. They will also bump version for you.

# 调试

## 手动运行

进入tty界面，确保原有的桌面环境管已经关闭。
```bash
$ sudo systemctl stop lightdm gxdm.service
```

执行以下命令手动启动合成器
```bash
$ gxde-wlcom -s startgxde
```

此时将启动GXDE桌面。

## 运程调试

需要安装tmux，在调试机器tty上启动tmux。
在运程机器上通过ssh连接调试机后，通过`tmux a`命令挂接上调试机。

## 嵌套运行

在已有的图形服务器上，终端启动gxde-wlcom，
此时为嵌入式运行状态，要在此合成器上运行客户端，需加上参数:

```
WAYLAND_DISPLAY=[display]
```

`[display]`可通过日志查看。 

## Backtraces

如果合成器崩溃，可在构建时使用ASAN/UBSAN来回溯
```bash
$ meson setup build -Dbuildtype=debug -Db_sanitize=address,undefined
$ ninja -C build
```

如果合成器多次崩溃且崩溃场景存在随机性，可使用valgrind工具进行测试
```bash
$ valgrind /path/gxde-wlcom ...
```

## Debug日志

查看客户端debug打印信息，可在启动客户端时添加如下指令打印debug信息
```
WAYLAND_DEBUG=1
```

服务端日志默认输出到 stdout，不再写入 `$HOME/.log`；如需落盘调试，可通过入参`-Dlogtofile`写入
```
$HOME/.log/gxde-wlcom.log
```

`-Dlogtostdout` 保留兼容（显式指定 stdout，与默认行为一致）
```bash
Usage: gxde-wlcom [options] [command]
  -d, --debug              Enables full logging, including debug information.\n
  -D, --debug <options>    noxwayland, logtostdout, logtofile or loginmtime.\n
```

设置debug信息输出到屏幕（默认行为）:
```bash
$ ./gxde-wlcom -d
```

如需写入日志文件:
```bash
$ ./gxde-wlcom -d -Dlogtofile
```

## xwayland

不开启xwayland: 
```bash
$ ./gxde-wlcom -Dnoxwayland
```

## ukui程序

在终端启动应用前面加入参数:
```bash
$ QT_QPA_PLATFORM=wayland QT_QPA_PLATFORMTHEME=ukui
```

## 输入

使用如下指令来显示输入事件
```bash
$ sudo libinput debug-events
```

在终端上，可以使用如下指令来分析键盘事件
```bash
$ xev -event keyboard
$ wev -f wl_keyboard:key
```

# 代码风格

代码风格由项目附带的`.clang-format`文件控制，可使用clang-format进行格式化，例如
```bash
$ clang-format -i src/view/workspace.c
```

# Commit信息

解释commit的原因和commit本身一样重要。像这样来提交commit信息，
```
view: mark mapped before configure 
```

第一行应该:
- 简要描述提交的原因
- 大多数情况下，前缀为被修改的模块或文件名
- 不使用句号

commit信息标题请控制在合理长度内。
