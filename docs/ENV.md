# 环境变量

kylin-wlcom合成器支持多个环境变量，可对内部流程进行修改。

## 日志

* *KYWC_LOG_LEVEL*：设置日志等级，可选值为：DEBUG|INFO|WARN|ERROR|FATAL|SILENT

## 显示

* *KYWC_BACKEND*：设置显示后端，可选值为：fbdev
* *KYWC_FB_DEVICES*：设置FBDEV后端输出节点，如/dev/fb0:/dev/fb1

## 渲染

* *KYWC_RENDERER*：设置渲染后端，可选值为：gl，gles2，pixman，vulkan
* *KYWC_EGL_NO_MODIFIERS*：关闭egl中的modifiers支持
* *KYWC_RENDERER_ALLOW_SOFTWARE*：允许使用软件渲染器，如llvmpipe

## 输出

* *KYWC_USE_LAYOUT_MANAGER*：使用合成器内部的屏幕配置管理，默认关闭
* *KYWC_SOFTWARE_GAMMA*：强制使用软件伽马，默认关闭

## 其他

* *XCURSOR_THEME*：设置光标主题 
* *XCURSOR_SIZE*：设置光标大小
