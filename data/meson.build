udev_framebuffer = files('udev-rules/90-framebuffer.rules')
install_data(udev_framebuffer, install_dir: '/usr/lib/udev/rules.d')

# 安装「GXDE on kywc」会话
# 详见 docs/gxde/gxde-wlcom-session.md
gxde_session = files('gxde-wlcom.desktop')
install_data(gxde_session, install_dir: '/usr/share/wayland-sessions')

gxde_session_launcher = files('startgxde_wlcom')
install_data(gxde_session_launcher, install_dir: '/usr/bin', install_mode: 'rwxr-xr-x')

default_config = files('config.json')
install_data(default_config, install_dir:'/etc/gxde-wlcom/')

embed = find_program('./embed.sh', native: true)

# 安装 UKUI 鼠标 gsettings schema(定位指针 / 晃动光标开关)
# 供 src/config/ukui_gsettings.c 读取 locate-pointer、shake-cursor 等键
gschema_conf = configuration_data()
gschema_conf.set('GETTEXT_PACKAGE', 'gxde-wlcom')
configure_file(
  input: 'org.ukui.peripherals-mouse.gschema.xml',
  output: 'org.ukui.peripherals-mouse.gschema.xml',
  configuration: gschema_conf,
  install_dir: get_option('datadir') / 'glib-2.0/schemas',
)
