# protocol

wayland协议支持情况
> https://wayland.app/protocols/

## Wayland core
> https://gitlab.freedesktop.org/wayland/wayland.git

| 协议名称               | 支持情况 | 说明       |
| :--------------------- | :------: | :--------- |
| wl_compositor          |    6     |            |
| wl_shm                 |    1     |            |
| wl_data_device_manager |    3     |            |
| wl_shell               |    -     | deprecated |
| wl_seat                |    8     | 9          |
| wl_output              |    4     |            |
| wl_subcompositor       |    1     |            |

## Wayland protocols
> https://gitlab.freedesktop.org/wayland/wayland-protocols.git

### stable

| 协议名称        | 支持情况 | 说明  |
| :-------------- | :------: | :---- |
| wp_presentation |    1     | check |
| wp_viewporter   |    1     |       |
| xdg_wm_base     |    5     | check |

### staging

| 协议名称                          | 支持情况 | 说明  |
| :-------------------------------- | :------: | :---- |
| wp_content_type_manager_v1        |    no    | 1     |
| wp_cursor_shape_manager_v1        |    no    | 1     |
| wp_drm_lease_device_v1            |    no    | 1     |
| ext_foreign_toplevel_list_v1      |    no    | 1     |
| ext_idle_notifier_v1              |    1     |       |
| ext_session_lock_manager_v1       |    no    | 1     |
| wp_fractional_scale_manager_v1    |    1     | check |
| wp_security_context_manager_v1    |    no    | 1     |
| wp_single_pixel_buffer_manager_v1 |    no    | 1     |
| wp_tearing_control_manager_v1     |    no    | 1     |
| xdg_activation_v1                 |    no    | 1     |
| xwayland_shell_v1                 |    1     |       |

### unstable

| 协议名称                                  | 支持情况 | 说明        |
| :---------------------------------------- | :------: | :---------- |
| zwp_fullscreen_shell_v1                   |    no    | 1           |
| zwp_idle_inhibit_manager_v1               |    1     |             |
| zwp_input_method_context_v1               |    no    |             |
| zwp_input_timestamps_manager_v1           |    1     |             |
| zwp_keyboard_shortcuts_inhibit_manager_v1 |    no    | 1           |
| zwp_linux_dmabuf_v1                       |    4     | check       |
| zwp_linux_explicit_synchronization_v1     |    no    | 2           |
| zwp_pointer_constraints_v1                |    no    | 1           |
| zwp_pointer_gestures_v1                   |    3     |             |
| zwp_primary_selection_device_manager_v1   |    1     |             |
| zwp_relative_pointer_manager_v1           |    no    | 1           |
| zwp_tablet_manager_v1                     |    -     | v2          |
| zwp_tablet_manager_v2                     |    1     |             |
| zwp_text_input_v1                         |    -     | 1 v3        |
| zwp_text_input_v3                         |    1     |             |
| zxdg_decoration_manager_v1                |    1     |             |
| zxdg_exporter_v1                          |    no    | 1           |
| zxdg_exporter_v2                          |    no    | 1           |
| zxdg_output_manager_v1                    |    3     |             |
| xdg_shell                                 |    -     | xdg_wm_base |
| zxdg_shell_v6                             |    -     | xdg_wm_base |
| zwp_xwayland_keyboard_grab_manager_v1     |    no    | 1           |

## wlr
> https://gitlab.freedesktop.org/wlroots/wlroots.git

| 协议名称                         | 支持情况 | 说明  |
| :------------------------------- | :------: | :---- |
| zwlr_data_control_manager_v1     |    2     |       |
| zwlr_export_dmabuf_manager_v1    |    1     | check |
| zwlr_foreign_toplevel_manager_v1 |    3     | check |
| zwlr_gamma_control_manager_v1    |    no    | 1     |
| zwlr_input_inhibit_manager_v1    |    no    | 1     |
| zwlr_layer_shell_v1              |    4     |       |
| zwlr_output_manager_v1           |    4     |       |
| zwlr_output_power_manager_v1     |    1     |       |
| zwlr_screencopy_manager_v1       |    3     |       |
| zwlr_virtual_pointer_v1          |    2     |       |

## kde
> https://invent.kde.org/libraries/plasma-wayland-protocols.git

| 协议名称                                       | 支持情况 | 说明  |
| :--------------------------------------------- | :------: | :---- |
| org_kde_kwin_appmenu_manager                   |    no    | 1     |
| org_kde_kwin_blur_manager                      |    1     | check |
| org_kde_kwin_contrast_manager                  |    no    | 2     |
| org_kde_kwin_dpms_manager                      |    1     |       |
| org_kde_kwin_fake_input                        |    no    | 4     |
| org_kde_kwin_idle                              |    1     |       |
| kde_lockscreen_overlay_v1                      |    no    | 1     |
| kde_output_device_v2                           |    2     | 3     |
| kde_output_management_v2                       |    2     | 4     |
| kde_output_order_v1                            |    no    | 2     |
| kde_primary_output_v1                          |    1     | 2     |
| kde_screen_edge_manager_v1                     |    no    | 1     |
| org_kde_kwin_keystate                          |    no    | 4     |
| org_kde_plasma_virtual_desktop_management      |    2     | check |
| org_kde_kwin_outputmanagement                  |    -     | 4 v2  |
| org_kde_kwin_outputdevice                      |    -     | 4 v2  |
| org_kde_plasma_shell                           |    8     | check |
| org_kde_plasma_window_management               |    16    | check |
| org_kde_kwin_remote_access_manager             |    no    | 1     |
| org_kde_kwin_server_decoration_palette_manager |    no    | 1     |
| org_kde_kwin_server_decoration_manager         |    1     |       |
| org_kde_kwin_shadow_manager                    |    no    | 2     |
| org_kde_kwin_slide_manager                     |    no    | 1     |
| zkde_screencast_unstable_v1                    |    no    | 3     |

## weston
> https://gitlab.freedesktop.org/wayland/weston.git

| 协议名称                  | 支持情况 | 说明 |
| :------------------------ | :------: | :--- |
| ivi_surface               |    no    | 1    |
| ivi_hmi_controller        |    no    | 1    |
| text_cursor_position      |    no    | 1    |
| weston_content_protection |    no    | 1    |
| weston_debug_v1           |    no    | 1    |
| weston_desktop_shell      |    no    | 1    |
| weston_direct_display_v1  |    no    | 1    |
| weston_capture_v1         |    no    | 1    |
| weston_test               |    no    | 1    |
| weston_touch_calibration  |    no    | 1    |

## external

| 协议名称                | 支持情况 | 说明 |
| :---------------------- | :------: | :--- |
| wl_drm                  |    2     |      |
| wl_eglstream            |    no    | 1    |
| wl_eglstream_controller |    no    | 2    |
| zwp_input_method_v2     |    1     |      |
| zwp_virtual_keyboard_v1 |    1     |      |
| zwp_text_input_v2       |    no    | 1    |
| qt_surface_extension    |    no    | 1    |
| gtk_shell1              |    no    | 5    |
