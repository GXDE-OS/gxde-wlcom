# GXDE Wlroots

## Introduction | 简介

GXDE Wlroots is a fork of [Open Kylin Wlroots](https://gitee.com/openkylin/wlroots), the aim is that to provide support for using Open Kylin's Wayland Compositor (you can call it `KYWC` or `wlcom`, you may find our fork on [GXDE-OS/open-kylin-wlcom](https://gitee.com/GXDE-OS/open-kylin-wlcom)) on GXDE OS.



GXDE Wlroot派生自[Open Kylin Wlroots](https://gitee.com/openkylin/wlroots)，目标是为在GXDE OS上使用Open Kylin的Wayland合成器——也就是`KYWC`，或者说`wlcom`，GXDE使用的版本由其派生，可以在「[GXDE-OS/open-kylin-wlcom](https://gitee.com/GXDE-OS/open-kylin-wlcom)」找到——提供支持。



## GXDE's Modification | GXDE做出的修改

Due to some of the XWayland needs, the current version is locked to update to `0.17.4-ok17`. The upstream can be found at: [openkylin/wlroots commit id `0a66efbca565a5e56ad677ccf4174df01a23d5a8`](https://gitee.com/openkylin/wlroots/commit/0a66efbca565a5e56ad677ccf4174df01a23d5a8).

出于`wlcom`现版本某些XWayland的需要，当前GXDE的版本锁定在`0.17.4-ok17`，上游位于[openkylin/wlroots的提交ID`0a66efbca565a5e56ad677ccf4174df01a23d5a8`](https://gitee.com/openkylin/wlroots/commit/0a66efbca565a5e56ad677ccf4174df01a23d5a8)。



We have also patched the current, we have patch some of features of Wlroots v0.20+ to the current version. You may find the upstream of the code we referred at [wlroots/wlroots commit id `f141edcd0233d86d9e9aab605b2b6f0803be5e98`](https://gitlab.freedesktop.org/wlroots/wlroots/-/commit/f141edcd0233d86d9e9aab605b2b6f0803be5e98).

我们亦对当前版本做出了一些修补，我们向其中加入了Wlroots v0.20+以来的一些新功能。您可以在[wlroots/wlroots的提交ID`f141edcd0233d86d9e9aab605b2b6f0803be5e98`](https://gitlab.freedesktop.org/wlroots/wlroots/-/commit/f141edcd0233d86d9e9aab605b2b6f0803be5e98)找到我们参考的源码。



### File Added | 新增文件
> **Note**: `wlr_linux_drm_syncobj_v1.c` is the subset of the upstream's implementation, with additional debugging mechanisms.

> **注意**: `wlr_linux_drm_syncobj_v1.c`是上游实现的子集，在此之上加入了一些调试机制。




| Added file | Purpose |
| ----- | ----- |
| `include/wlr/render/drm_syncobj.h` | DRM synchronization timeline's API, which includs timeline creation, importing, exporting, signaling, and waiting. |
| `include/render/drm_syncobj_merger.h` | Timeline merger that combines multiple inputs into a single output. |
| `include/wlr/types/wlr_linux_drm_syncobj_v1.h` | Implementation of the `linux-drm-syncobj-v1` Wayland protocol. |
| `render/drm_syncobj.c` | Core timeline implementation. |
| `render/drm_syncobj_merger.c` | Merger implementation. |
| `types/wlr_linux_drm_syncobj_v1.c` | Protocol surface state management. |



| 新增文件 | 作用 |
| ----- | ----- |
| `include/wlr/render/drm_syncobj.h` | DRM同步时间线的API，包括`timeline`的创建、导入、导出、信号与等待 |
| `include/render/drm_syncobj_merger.h` | 时间线合并器，将多输入合并为单输出 |
| `include/wlr/types/wlr_linux_drm_syncobj_v1.h` | `linux-drm-syncobj-v1`Wayland协议实现 |
| `render/drm_syncobj.c` | 时间线核心实现 |
| `render/drm_syncobj_merger.c` | 合并器实现 |
| `types/wlr_linux_drm_syncobj_v1.c` | 协议surface state管理 |




### File Modified | 修改文件
* **`meson.build`**: Adds feature detection for `eventfd` and `linux_sync_file` ・ 添加`eventfd`与`linux_sync_file`的特性检测。
* **`protocol/meson.build`**: Introduces the `linux-drm-syncobj-v1.xml` protocol ・ 引入`linux-drm-syncobj-v1.xml`协议。
* **`render/meson.build`**: Builds the newly added render-related files ・ 用于编译新增的渲染相关文件。
* **`types/meson.build`**: Builds the protocol implementation ・ 用于编译协议实现的相关文件。





## Original README | 原版README

> **NOTE**: The original README is written fully in English, and I translated it myself and the translation quality may be poor.
>
> **注意**: 原版README是纯英文的，这是我自行翻译的，翻译质量可能比较糟糕。



Pluggable, composable, unopinionated modules for building a [Wayland]
compositor; or about 60,000 lines of code you were going to write anyway.

可插拔、可组合、无强制设计取向的模块，为建造「[Wayland]合成器」准备，节省您六万行代码的工作。



- wlroots provides backends that abstract the underlying display and input
  hardware, including KMS/DRM, libinput, Wayland, X11, and headless backends,
  plus any custom backends you choose to write, which can all be created or
  destroyed at runtime and used in concert with each other.

- wlroots为您提供显示和输入硬件的抽象后端，包括 KMS/DRM、libinput、Wayland、X11和无头后端，

  更可加入您写的任何自定义后端。所有的这些都可以在运行时创建或销毁，并可以相互配合使用。

  

- wlroots provides unopinionated, mostly standalone implementations of many
  Wayland interfaces, both from wayland.xml and various protocol extensions.
  We also promote the standardization of portable extensions across
  many compositors.

- wlroots为您提供许多Wayland接口的不预设架构的（且大多数是独立的）实现，既有wayland.xml接口，更包含各大协议扩展。我们还致力于推动跨合成器可移植扩展的标准化。



- wlroots provides several powerful, standalone, and optional tools that
  implement components common to many compositors, such as the arrangement of
  outputs in physical space.
- wlroots为您提供了一些强大的、独立的、可选的工具以实现许多合成器都拥有的通用功能，例如在物理空间中排列输出。



- wlroots provides an Xwayland abstraction that allows you to have excellent
  Xwayland support without worrying about writing your own X11 window manager
  on top of writing your compositor.
- wlroots提供了一个XWayland抽象层，您可以享受出色的XWyland支持而无需担心在「编写合成器」的工作之外还得自己开发一个X11窗管以支持那些「传统」的X11程序。



- wlroots provides a renderer abstraction that simple compositors can use to
  avoid writing GL code directly, but which steps out of the way when your
  needs demand custom rendering code.
- wlroots提供了一个渲染器抽象层，要制作一个简易的合成器，您可以直接使用这个抽象层以省去直接编写GL代码的工作。即便后期您需要自定义的渲染代码，这个抽象层也不会碍事。



wlroots implements a huge variety of Wayland compositor features and implements
them *right*, so you can focus on the features that make your compositor
unique. By using wlroots, you get high performance, excellent hardware
compatibility, broad support for many wayland interfaces, and comfortable
development tools - or any subset of these features you like, because all of
them work independently of one another and freely compose with anything you want
to implement yourself.

wlroots**正确地**实现了种类繁多的Wayland合成器功能，因此您可以专注于编写哪些让您自己的合成器与众不同的特色功能。使用wlroots，您可以享受到高性能的表现、出色的硬件兼容性、对多种Wayland接口的广泛支持与便捷的开发工具 —— 由于它们都是独立实现，您亦可以裁剪出其中任何子集，并且与您自己实现的各种功能自由组合。



Check out our [wiki] to get started with wlroots. Join our IRC channel:
[#wlroots on Libera Chat].

要开始上手，请参考我们的[WIKI](https://gitlab.freedesktop.org/wlroots/wlroots/-/wikis/Getting-started)。



A variety of [wrapper libraries] are available for using it with your favorite
programming language.

wlroots拥有众多[封装库](https://gitlab.freedesktop.org/wlroots/wlroots/-/wikis/Projects-which-use-wlroots#wrapper-libraries)可供您使用您偏好的语言调用wlroots。



### Building | 构建

Install dependencies ・ 安装如下依赖:

* meson
* wayland
* wayland-protocols
* EGL and GLESv2 (optional, for the GLES2 renderer · 可选，GLES2渲染器相关)
* Vulkan loader, headers and glslang (optional, for the Vulkan renderer · 可选，Vulkan渲染器相关)
* libdrm
* GBM (optional, for the GBM allocator · 可选，GBM分配器相关)
* libinput (optional, for the libinput backend · 可选，libinput后端依赖)
* xkbcommon
* udev (optional, for the session)
* pixman
* [libseat] (optional, for the session · 可选，会话相关)
* [hwdata] (optional, for the DRM backend · 可选，DRM后端相关)
* [libdisplay-info] (optional, for the DRM backend · 可选，DRM后端相关)
* [libliftoff] (optional, for the DRM backend · 可选，DRM后端相关)

If you choose to enable X11 support · 如果您选择开启X11支持:

* xwayland (build-time only, optional at runtime · 构建时必要，运行时可选)
* libxcb
* libxcb-render-util
* libxcb-wm
* libxcb-errors (optional, for improved error reporting · 可选，用于更好的错误报告)

Run these commands · 运行以下指令:
```bash
$ meson setup build/
$ ninja -C build/
```

Install like so · 参考下方的指令以安装:
```bash
$ sudo ninja -C build/ install
```




### Contributing | 贡献

See [CONTRIBUTING.md].

请参阅[CONTRIBUTING.md].



[Wayland]: https://wayland.freedesktop.org/
[wiki]: https://gitlab.freedesktop.org/wlroots/wlroots/-/wikis/Getting-started
[#wlroots on Libera Chat]: https://web.libera.chat/gamja/?channels=#wlroots
[wrapper libraries]: https://gitlab.freedesktop.org/wlroots/wlroots/-/wikis/Projects-which-use-wlroots#wrapper-libraries
[libseat]: https://git.sr.ht/~kennylevinsen/seatd
[hwdata]: https://github.com/vcrhonek/hwdata
[libdisplay-info]: https://gitlab.freedesktop.org/emersion/libdisplay-info
[libliftoff]: https://gitlab.freedesktop.org/emersion/libliftoff
[CONTRIBUTING.md]: https://gitlab.freedesktop.org/wlroots/wlroots/-/blob/master/CONTRIBUTING.md