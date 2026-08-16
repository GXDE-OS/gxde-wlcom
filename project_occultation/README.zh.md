# Project Occultation
## 简介
GXDE OS的Wayland会话早已从`gxde-kwin`转至`GXWM`，不过我们是由于更新破坏依赖而被迫这么做的，但我们还是相应尽可能的带回原版体验。

然而，要重现原版的体验是很难的，由其是UI/动效这一块。于是我们的下一个主意是复用原版`gxde-kwin`的UI。

实际上，架构如下：
1. Project Occultation集成了部分`gxde-kwin`的源码，大多数是UI文件。
2. Project Occultation的子项目`Exoskeleton`，实现了最小的薄垫片以模拟KWin环境，供UI跑起来。
    * 在模拟中，底层功能当然都是由`GXWM`的API提供的
    * 垫片与WM之间通过`libkwyc`交流

## 许可证
此项目使用多种许可证，请检查子文件夹的README/LICENSE（如果有的话）。
