# 怎么运行wlcom?

进入tty界面,确保会话管理器已经关闭.

    执行 sudo systemctl stop lightdm;
    执行 kylin-wlcom -s ukui-session;

启动合成器.此时将ukui桌面启动,就可以正常使用openkylin上的一些功能.

在openkylin下直接启动kylin-wlcom;

    终端执行 kylin-wlcom;

此时为嵌入式运行状态,要在此合成器上运行客户端,需加上参数:

    WAYLAND_DISPLAY=[display].

[display]通过export查看. 

# 怎么调试wlcom?

## 检查内存泄漏

构建时添加参数:

    meson setup build -Db_sanitize=address,undefined
    ninja -C build

## 打印

设置debug信息输出到屏幕: 

    ./kylin-wlcom -d -Dlogtostdout.

不开启xwayland: 

    ./kylin-wlcom -Dnoxwayland

# wlcom支持哪些窗口特效?

wlcom当前支持最大化,最大化还原,最小化,阴影等窗口特效.

# 在没有启动ukui-session时,怎么启动ukui应用并加载ukui主题?

在终端启动应用前面加入参数:

    QT_QPA_PLATFORM=wayland WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORMTHEME=ukui 

# 支持的快捷键有哪些?

| combination              | action
| ------------------------ | ------
| `Win`-`h`                | window minimized
| `Alt`-`space`            | window menu
| `Alt`-`F4`               | window closed
| `Alt`-`F7`               | window move
| `Alt`-`F8`               | window resize
| `Alt`-`F10`              | window maximized
| `win`-`up`               | window snap edge up
| `win`-`down`             | window snap edge down
| `win`-`left`             | window snap edge left
| `win`-`right`            | window snap edge right
| `Ctrl`-`Alt`-`Left`      | switch to left workspace
| `Ctrl`-`Alt`-`Right`     | switch to right workspace
| `Ctrl`-`Alt`-`Up`        | switch to up workspace
| `Ctrl`-`Alt`-`Down`      | switch to down workspace

# wlcom右键菜单支持哪些功能?

在服务端装饰上按下鼠标右键，将弹出菜单支持更多操作
| 右键菜单         | 子菜单
| ---------------- | ------
| Minimize(N)      |
| Maximize(X)      |
| Fullscreen(F)    |
| Close(C)         |
| More(M)          | Move(M) <br> Resize(R) <br> Keep-Above(A) <br> Keep-Below(B)

# wlcom提供哪些dbus支持?

支持openKylin相关的dbus接口
- org.freedesktop.login1.Manager
- org.kde.KGlobalAccel
- org.kde.kglobalaccel.Component
- org.kde.KGlobalSettings
- org.kde.KWin.InputDeviceManager
- org.kde.KWin.InputDevice

wlcom提供的dbus接口
| interface name              | method
| --------------------------- | ------
| com.kylin.Wlcom             | SetLogLevel <br> PrintConfig
| com.kylin.Wlcom.Output      | ListAllOutputs
| com.kylin.Wlcom.Input       | ListAllInputs <br> MapToOutput <br> ChangeSeat <br> SetSendEventsMode <br> EnableTapToClick                                  <br> SetPointerSpeed <br> EnableNaturalScroll <br> EnableLeftHand <br> SetRepeatInfo
| com.kylin.Wlcom.Plugin      | ListAllPlugins <br> PrintPluginInfo <br> PrintPluginConfig <br> LoadPlugin <br> EnablePlugin                                 <br> SetPluginOption
| com.kylin.Wlcom.Theme       | ListAllThemes <br> currentTheme <br> SetTheme
