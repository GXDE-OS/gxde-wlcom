![](./docs/img/readme-header.png)

![GitHub contributors](https://img.shields.io/github/contributors/GXDE-OS/gxde-wlcom) ![GitHub Release](https://img.shields.io/github/v/release/GXDE-OS/gxde-wlcom) ![Static Badge](https://img.shields.io/badge/license-GPL--3.0--or--later-orange) ![Static Badge](https://img.shields.io/badge/made_with-love-red)

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

该项目以开源协议 **GPL-3.0-or-later** 发布，项目中引用或包含的来自其他开源项目的文件及代码片段，均遵照其原始许可证要求进行使用。



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



### treeland-protocols 0.5.9 与 personalization 协议

**这一节关系到整个桌面能否启动，改动`protocols/treeland-personalization-manager-v1.xml`前请务必读完。**

上游在`treeland-protocols` 0.5.9（提交`8576b9c`，2026-06-16）中删除了`get_wallpaper_context`请求和`treeland_personalization_wallpaper_context_v1`接口，理由是该功能已迁移至`xdg-desktop-portal`。问题在于**这次删除没有bump版本号**——manager接口前后都是`version="2"`，接口名也没变，提交信息自己写的就是`Influence: Broken change`。

Wayland的opcode是按XML中的出现顺序排的，少一个请求，后面全部错位一格：

| 客户端发出                        | opcode | 布局不一致的服务端会执行            |
| --------------------------------- | ------ | ----------------------------------- |
| 0.5.9客户端的`get_cursor_context`  | 1      | 0.5.8服务端的`get_wallpaper_context` |
| 0.5.8客户端的`get_appearance_context` | 4   | 0.5.9服务端的`destroy`               |

由于两种布局同名、同版本，服务端在`bind`时拿不到任何信号去区分对面是哪一种，**无法同时兼容**。一旦对不上，所有DTK程序（`libdtkgui`/`libdtk6gui`，也就是几乎全部GXDE程序）会在启动时被合成器以`wl_display error`杀掉，桌面直接起不来。

因此本仓库vendor的XML必须与**系统上DTK编译时所用的那一份**保持wire一致，而不是跟着上游master走。当前仓库内的版本对应 **0.5.8（含wallpaper context）**。

#### 何时需要切换

真正的引爆点不是`apt upgrade treeland-protocols`——XML只是编译期输入，升级协议包本身不改变任何已编译好的客户端。**引爆点是`libdtkgui`/`libdtk6gui`等被按0.5.9重新编译的那一刻**，那时合成器必须同步切换，早一步晚一步都会让桌面无法登录。

判断依据（任选其一）：

```bash
# 1. 看系统协议包版本
dpkg -l treeland-protocols

# 2. 直接看DTK是否还引用wallpaper context，这一条才是决定性的
strings -a /usr/lib/x86_64-linux-gnu/libdtk6gui.so.* | grep -c treeland_personalization_wallpaper_context
#   >0 : DTK仍按0.5.8编译，必须保留wallpaper context
#    0 : DTK已按0.5.9编译，必须删除wallpaper context
```

#### 运行时开关（无需重新编译）

合成器把两种布局都编译在了同一个二进制里，启动时选一个：0.5.8布局用的是wayland-scanner生成的方法表，0.5.9布局则在运行时从同一张表里挑出`{0,2,3,4,5}`（跳过`get_wallpaper_context`）拼出来，因此两条路径同源、不会各自漂移。**前提是vendor的XML保持0.5.8超集**——真正切到0.5.9后这个开关连同wallpaper实现一起删掉即可。

默认行为是**自动探测**：启动时扫描已安装的`libdtk*gui`（Qt5与Qt6两套都查，含multiarch路径），看它们是否还引用`treeland_personalization_wallpaper_context_v1`。这比看协议包版本准，因为XML只是编译期输入。探测不到DTK时（构建chroot、精简安装）退回去看`/usr/share/treeland-protocols`，再不行默认0.5.8。

理论上不需要强制设定，但如果非得要，设环境变量即可，改`startgxde_wlcom`里一行然后重新登录：

```bash
export GXDE_WLCOM_PERSONALIZATION=058   # 或 059；亦接受 0.5.8 / 0.5.9
```

确认当前选了哪个（该行是`INFO`级，默认`WARN`不显示，需`-V`或`KYWC_LOG_LEVEL=INFO`）：

```bash
grep Personalization ~/.log/gxde-wlcom.log | tail -1
# (Treeland Shim) Personalization: probed 4 DTK libraries, wallpaper context referenced -> using the 0.5.8 layout
# (Treeland Shim) Personalization: layout forced to 0.5.9 by GXDE_WLCOM_PERSONALIZATION
```

若探测发现DTK5与DTK6**不一致**（一个已按0.5.9重编、另一个还没有），会打一条`ERROR`并选择0.5.8。这种混合状态下没有任何一种选择能让两边都活，只能把落后的那个包补编译完，或用环境变量指定优先保谁。

#### 彻底移除兼容代码

日常切换用上面的开关就够了，本节是**等GXDE彻底转向0.5.9之后**清理死代码的做法（此后合成器不再能服务0.5.8客户端）。

撤销`17585154`（`fix: DTK program crashes due to lacking wallpaper support in treeland personalization MGR`）即可，无需其他改动：

```bash
git revert <运行时开关的提交>   # 必须先撤，它依赖wallpaper相关符号
git revert 17585154
ninja -C build
```

**顺序不能反。** 运行时开关的代码引用了`get_wallpaper_context`/`manager_get_wallpaper_context`等符号，先撤`17585154`会留下一堆悬空引用。

`17585154`只动了两个文件（XML加回wallpaper context、`treeland_personalization.c`加回其实现），撤销后XML即回到与上游master逐字节相同的状态，与0.5.9包wire一致。若日后rebase/squash导致hash失效，等价的手工步骤是：

1. 用系统上的新版覆盖vendor的XML：
   ```bash
   cp /usr/share/treeland-protocols/treeland-personalization-manager-v1.xml protocols/
   ```
2. 删除`src/view/treeland_personalization.c`中的运行时开关：`enum personalization_layout`、manager里的`layout`/`interface_059`/`requests_059`、`manager_implementation_059`与`manager_impl_059`、`layout_derive_059`、`file_contains`、`dtk_lib_patterns`、`layout_from_env`、`layout_detect`、`layout_is_059`，以及`treeland_personalization_manager_create`里的探测段落；`personalization_manager_bind`与`wl_global_create`改回直接使用生成的`treeland_personalization_manager_v1_interface`和`manager_impl`。
3. 删除同一文件中的wallpaper context实现：`wallpaper_*`系列函数、`wallpaper_impl`、`manager_get_wallpaper_context`、`manager_impl`中的`.get_wallpaper_context`、`personalization_context`里的`wallpaper`子结构、manager里的`wallpaper_contexts`与`wallpaper_metadata`及其初始化/释放。文件里另有`BLEND_MODE_WALLPAPER`相关的两处，属于window context的blend mode，与本节无关，**不要删**。
4. `ninja -C build`，编译期若有遗漏会直接报错。

**不要`git revert b9dafa79`。** 那个提交引入的是*整套*personalization支持（window/cursor/font/appearance四类context），撤销它会连窗口圆角、模糊、标题栏控制一起丢掉。撤销后合成器不再广播该global，客户端会自行回退、不会崩溃，但功能全部消失，属于因噎废食。

顺带一提，b9dafa79当初vendor的XML本身**就是**0.5.9布局（与上游master逐字节相同），只是当时系统上的DTK还是按0.5.8编译的，才导致了错位。所以撤销`17585154`实际上就是把该文件还原回b9dafa79时的状态。

#### 改动XML后必须做的校验

描述文字随便改，但wire布局（接口、请求/事件顺序、参数类型、`type="destructor"`、`since`、version）必须与目标版本完全一致。改完请比对：

```bash
python3 - <<'EOF' protocols/treeland-personalization-manager-v1.xml /usr/share/treeland-protocols/treeland-personalization-manager-v1.xml
import sys, xml.etree.ElementTree as ET
def wire(p):
    out = []
    for i in ET.parse(p).getroot().findall('interface'):
        out.append(('IFACE', i.get('name'), i.get('version')))
        for kind in ('request', 'event'):
            for op, m in enumerate(i.findall(kind)):
                args = [(a.get('type'), a.get('interface')) for a in m.findall('arg')]
                out.append((kind, i.get('name'), op, m.get('name'), m.get('type'), m.get('since'), args))
    return out
a, b = wire(sys.argv[1]), wire(sys.argv[2])
print("WIRE IDENTICAL" if a == b else "WIRE MISMATCH:\n" + "\n".join(
    f"  ours={x}\n  upst={y}" for x, y in zip(a, b) if x != y))
EOF
```

再嵌套跑一遍确认真机行为，这一步能在污染真实会话之前抓到问题：

```bash
WAYLAND_DISPLAY=wayland-0 ./build/gxde-wlcom   # 对外暴露 wayland-1
WAYLAND_DISPLAY=wayland-1 gxde-terminal        # 任一DTK程序，能起来即为正常
```

同样的比对建议对`protocols/`下其余treeland协议一并执行——它们目前与系统包一致。



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

按`Meta+S`打开或关闭多任务视图。当前实现提供：

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

##### 显示桌面
Wayland会话中，`gxde-wlcom`直接持有`com.deepin.wm`，并兼容
`GetIsShowDesktop()`和`SetShowDesktop(bool)`。接口与`Meta+D`共用
`view_manager_show_desktop()`状态机，因此只恢复由本次“显示桌面”操作
最小化的窗口。X11会话仍由原来的`deepin-wm`处理；本包不安装或替换
`deepin-daemon`的`desktop-toggle`。

##### 切换窗口
可以通过`Alt + Tab`或者`Alt + Shift + Tab`唤起窗口切换器，其外观模仿`Deepin KWin`。

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

```bash
$ meson compile gxde-wlcom-pot
```



## 里程碑

- [x] 添加「显示桌面」支持

- [ ] 完成对Treeland协议的完整兼容



## 参与开发

请参阅「[CONTRIBUTING](./docs/CONTRIBUTING.md)」文件，了解贡献所需的信息。

对于MR，请将代码PR至`gxde/testing`分支，测试稳定后将由管理员合并至`gxde/zhuangzhuang`分支并bump。

### GXDE Wlcom的贡献者们

*(注：不知道为何很多原版KYWC的贡献者没有显示，您可以在[此处](https://gitee.com/openkylin/kylin-wayland-compositor/contributors?ref=debian%2Funstable)找到原版KYWC贡献者的信息）*

<a href="https://github.com/GXDE-OS/gxde-wlcom/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=GXDE-OS/gxde-wlcom" alt="contrib.rocks image" />
</a>



## 许可证

该项目使用开源协议`GPL-3.0-or-later`，详见「[COPYING](./COPYING)」。

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
