# Third Party Library Vendor Information
## Wlroots
* **Upstream**: https://github.com/GXDE-OS/open-kylin-wlroots (GXDE fork of openKylin's wlroots `0.17.4-ok` branch)
* **Branch**: `gxde/testing`
* **Version**: `0.17.5-gxde2`
* **Commit ID**: `950dbeb6cc3701832b3623fdd4bc51c9a9a37f6e`
* **Commit date**: Sun Sep 6 17:46:58 2026 -0500
* **Clone date**: Sat Oct 3 2026 +0800
* **License**: [MIT License](./wlroots/LICENSE)
* **Note**: Previously pulled in as the Meson subproject `subprojects/wlroots.wrap` at the same revision. It carries the XWayland `_NET_WM_STATE` extensions wlcom needs (sticky, skip_taskbar, skip_pager, demands_attention, `wlr_xwayland_get_xwm_connection`, …), keeps `precommit`, and backports parts of wlroots 0.20+. Its package is still named `wlroots`, so it is built by [cmake/wlroots.cmake](../cmake/wlroots.cmake) into the build directory and linked statically instead of being installed.
