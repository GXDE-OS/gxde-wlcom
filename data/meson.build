udev_framebuffer = files('udev-rules/90-framebuffer.rules')
install_data(udev_framebuffer, install_dir: '/usr/lib/udev/rules.d')

session = files('kylin-wlcom.desktop')
install_data(session, install_dir: '/usr/share/wayland-sessions')

# 安装「GXDE on kywc」会话
# 详见 docs/gxde/gxde-wlcom-session.md
gxde_session = files('gxde-wlcom.desktop')
install_data(gxde_session, install_dir: '/usr/share/wayland-sessions')

gxde_session_launcher = files('start-gxde-wlcom')
install_data(gxde_session_launcher, install_dir: '/usr/bin', install_mode: 'rwxr-xr-x')

default_config = files('config.json')
install_data(default_config, install_dir:'/etc/kylin-wlcom/')

embed = find_program('./embed.sh', native: true)
