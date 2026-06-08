# 在GXDE上引导kylin-wlcom会话

---

> **注意**: 下面将`kylin-wlcom`简称为「`kywc`」; 本文档假设本地的WM已经构建完成



# 编写会话文件

> ℹ️ 写完会话需重启 `LightDM` 才能在登录界面看到新会话：
> ```bash
> # TTY 下执行
> $ sudo systemctl restart lightdm
> ```
> 若没有其它未保存内容、也无其他用户在用,重启整机亦可。



## 「GXDE on kywc」会话

新建/usr/bin/start-gxde-wlcom`: 



**实体机**: 

```bash
#!/bin/bash
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=gxde-wlcom
export DTK2_XWAYLAND=dxcb
exec /usr/bin/kylin-wlcom -s /usr/bin/startdde "$@"
```



**QEMU/KVM 虚拟机**: 

```bash
#!/bin/bash
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=gxde-wlcom
export WLR_RENDERER=pixman  # 虚拟机里GL/dmabuf多半不可用...
export DTK2_XWAYLAND=dxcb
exec /usr/bin/kylin-wlcom -s /usr/bin/startdde "$@"
```



修改权限: 

```bash
$ sudo chmod +x /usr/bin/start-gxde-wlcom
```



新建`/usr/share/wayland-sessions/gxde-wlcom.desktop`：

```ini
[Desktop Entry]
Name=gxde-wlcom
Comment=GXDE desktop on the Kylin Wayland Compositor
Exec=/usr/bin/start-gxde-wlcom
Type=Application
DesktopNames=GXDE
```



下次便可在登录界面选择 `gxde-wlcom` 会话。



## 「纯kywc」会话

新建`/usr/bin/start-kywc-plain`: 



**实体机**: 

```bash
#!/bin/bash
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=kywc
exec /usr/bin/kylin-wlcom "$@"
```


**QEMU/KVM 虚拟机**: 

```bash
#!/bin/bash
export XDG_SESSION_TYPE=wayland
export XDG_SESSION_DESKTOP=kywc
export WLR_RENDERER=pixman
exec /usr/bin/kylin-wlcom "$@"
```



修改权限: 

```bash
$ sudo chmod +x /usr/bin/start-kywc-plain
```



新建`/usr/share/wayland-sessions/kywc-plain.desktop`: 

```ini
[Desktop Entry]
Name=kywc-plain
Comment=Plain Kylin Wayland Compositor session
Exec=/usr/bin/start-kywc-plain
Type=Application
DesktopNames=kywc
```



# 常用命令选项

| 选项 | 说明 |
|---|---|
| `-h, --help` | 打印帮助并退出 |
| `-v, --version` | 打印版本并退出 |
| `-s, --session <process>` | 启动后拉起会话子进程（如 `startdde`）。其退出会带动 kywc 退出 |
| `-d, --debug` | 开启完整日志（含 debug） |
| `-V, --verbose` | 更详细的日志 |
| `-D <flag>` | 调试开关，如 `-D nosessionbinding`（session 退出后 kywc 不随之退出） |



**环境变量**: 

- `WLR_RENDERER=pixman` —— 纯软件渲染（虚拟机/无可用 GL 时用）。
- `WLR_BACKENDS=wayland` —— 让 kywc 以**嵌套**方式跑在另一个 Wayland 会话里（见下，调试用）。



# 嵌套运行冒烟测试

```bash
# 在已有 Wayland 会话内
$ WLR_BACKENDS=wayland WLR_RENDERER=pixman kylin-wlcom -s "sleep 600"
```



它会新建自己的`wayland-N` socket。随后可把客户端指向它: 

```bash
$ WAYLAND_DISPLAY=wayland-1 GDK_BACKEND=wayland gxde-terminal # 原生 wayland 客户端
# 或经其 XWayland(DISPLAY 由 kywc 的会话子进程环境给出,如 :1):
$ DISPLAY=:1 GDK_BACKEND=x11 gxde-terminal # XWayland
```
