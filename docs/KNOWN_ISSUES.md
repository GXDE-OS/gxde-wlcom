# ukui应用显示双标题栏

- ukui应用的装饰是应用自身绘制,没有基于装饰协议,才造成显示双标题栏.

# openkylin中的kylin-virtual-keyboard的悬浮图标显示四个黑角

- 查看kylin-virtual-keyborad及qt xcb源码，qt-xcb setMask函数内部使用xshape扩展设置了区域. wlroots xwm未支持xcb-shape.

# 控制面板无法设置或者设置后在合成器上不生效的选项

- 无法对鼠标指针大小,鼠标样式,鼠标双击间隔时间,强调色进行设置.

# kylin-screenshot无法使用问题

- 该软件使用到kde screencast协议,合成器并未支持此协议.

# 阴影特效

- 没有区分哪些应用不需要绘制,鼠标事件无法穿透阴影区域.
