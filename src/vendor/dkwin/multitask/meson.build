# The upstream Deepin/KWin implementation is kept verbatim in upstream/ for
# layout, animation, shader and asset parity.  It is deliberately not compiled:
# wlcom_multitask.c is the compositor-native adapter and has no KWin dependency.
multitask_assets = configuration_data()
multitask_assets.set_quoted('MULTITASK_SOURCE_ASSET_DIR',
                            meson.current_source_dir() / 'upstream/buttons')
multitask_assets.set_quoted('MULTITASK_INSTALL_ASSET_DIR',
                            get_option('prefix') / get_option('datadir') /
                            'gxde-wlcom/multitask/buttons')
configure_file(output: 'multitask_assets.h', configuration: multitask_assets)

install_subdir('upstream/buttons', install_dir: get_option('datadir') / 'gxde-wlcom/multitask')
install_subdir('upstream/shaders', install_dir: get_option('datadir') / 'gxde-wlcom/multitask')

wlcom_sources += files(
    'wlcom_multitask.c',
    'wlcom_multitask_dbus.c',
)
