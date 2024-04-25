udev_backlight = files('udev-rules/90-backlight.rules')
udev_framebuffer = files('udev-rules/90-framebuffer.rules')
install_data(udev_backlight, udev_framebuffer, install_dir: '/usr/lib/udev/rules.d') 

session = files('kylin-wlcom.desktop')
install_data(session, install_dir: '/usr/share/wayland-sessions')

default_config = files('config.json')
install_data(default_config, install_dir:'/etc/kylin-wlcom/')

embed = find_program('./embed.sh', native: true)
