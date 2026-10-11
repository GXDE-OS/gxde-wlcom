# 集成的第三方库信息
## Wlroots
* **上游**: https://github.com/GXDE-OS/open-kylin-wlroots （GXDE对openKylin的wlroots `0.17.4-ok`分支的fork）
* **分支**: `gxde/testing`
* **版本**: `0.17.5-gxde2`
* **提交ID**: `950dbeb6cc3701832b3623fdd4bc51c9a9a37f6e`
* **提交日期**: Sun Sep 6 17:46:58 2026 -0500
* **拉取日期**: Sat Oct 3 2026 +0800
* **许可证**: [MIT License](./wlroots/LICENSE)
* **备注**: 此前以meson子项目`subprojects/wlroots.wrap`锁定在同一revision引入。它带有wlcom的XWayland要的`_NET_WM_STATE`扩展（sticky、skip_taskbar、skip_pager、demands_attention、`wlr_xwayland_get_xwm_connection`等），保留了`precommit`，并backport了主线wlroots 0.20+的部分功能。它的包名仍然是`wlroots`，所以由[cmake/wlroots.cmake](../cmake/wlroots.cmake)构建到构建目录里并静态链接，不会安装到系统中。
