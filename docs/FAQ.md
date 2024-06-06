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

    ./kylin-wlcom -d -Dlogtostdout

不开启xwayland: 

    ./kylin-wlcom -Dnoxwayland

# wlcom支持哪些窗口特效?

wlcom当前支持最大化,最大化还原,最小化,阴影等窗口特效.

# 在没有启动ukui-session时,怎么启动ukui应用并加载ukui主题?

在终端启动应用前面加入参数:

    QT_QPA_PLATFORM=wayland WAYLAND_DISPLAY=wayland-0 QT_QPA_PLATFORMTHEME=ukui 
