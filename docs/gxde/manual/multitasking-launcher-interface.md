# 在 Wayland 会话接管「多任务视图」启动接口

## 目标

GXDE 菜单中的「多任务视图」不是独立应用。它是一个 D-Bus 启动器：

```ini
Exec=dbus-send --session --dest=com.deepin.wm --print-reply \
  /com/deepin/wm com.deepin.wm.PerformAction int32:1
```

桌面文件由 `gxde-daemon` 提供：

```text
/usr/share/applications/gxde-multitaskingview.desktop
```

在 X11/KWin 会话中，`com.deepin.wm` 由兼容服务处理；当前
`gxde-wlcom` Wayland 会话没有接入这条调用链，所以点击菜单项不会打开
wlcom 的原生多任务视图。

要保持现有桌面文件不变，重写实现需要接管下述 D-Bus 契约，并将其连接到
wlcom 内部的多任务视图开关。

## 必须实现的 D-Bus 契约

| 项目 | 值 |
| --- | --- |
| 总线 | 用户会话总线（session bus） |
| well-known name | `com.deepin.wm` |
| object path | `/com/deepin/wm` |
| interface | `com.deepin.wm` |
| method | `PerformAction` |
| 输入签名 | `i`，即 D-Bus `INT32` |
| 输出签名 | 空 |
| 多任务动作值 | `1` |

最小 introspection XML：

```xml
<node>
  <interface name="com.deepin.wm">
    <method name="PerformAction">
      <arg name="type" type="i" direction="in"/>
    </method>
  </interface>
</node>
```

接收 `PerformAction(1)` 时应切换多任务视图：

- 当前未显示：打开；
- 当前已显示：关闭；
- 成功后发送一个无返回参数的 method reply；
- 不要等待进入/退出动画结束后才回复。

`1` 的含义来自 Deepin WM 的 `ShowWorkspace` 动作，也是现有
「多任务视图」桌面文件实际发送的值。

## wlcom 侧的实际触发点

当前原生实现只注册了内部快捷键：

```text
Win+S
```

相关实现位于：

```text
src/vendor/dkwin/multitask/wlcom_multitask.c
```

调用链是：

```text
Win+S
  -> shortcut_action(...)
  -> multitask_view_set_enabled(!overview->enabled)
```

D-Bus 接口应复用这个状态切换入口。建议从多任务模块导出一个不接触
D-Bus 类型的窄接口，例如：

```text
multitask_view_toggle()
```

该函数内部只负责：

```text
overview 存在
  -> multitask_view_set_enabled(!overview->enabled)
```

D-Bus handler 只做参数校验、调用该函数和回复消息。不要在 D-Bus 层复制
多任务视图状态，也不要模拟 `Win+S` 键盘输入。

### 为什么不能直接调用 KGlobalAccel

当前 wlcom 的 `org.kde.kglobalaccel.Component.invokeShortcut` 虽然存在，
但 `Win+S` 是在 KGlobalAccel 扫描内建快捷键之后才注册的，因此它不在
`/component/gxde_wlcom` 的 action 列表中。当前会话中不能通过
`invokeShortcut("win+s:no", "default")` 可靠触发它。

## 接管方式

### 方式 A：在现有 `deepin-wm-dbus` 兼容层中替换动作后端

这是兼容性最好的做法。

保留现有服务名称、对象路径、接口以及其他方法，只将 Wayland 会话中的
`PerformAction(1)` 改为调用 wlcom 暴露的多任务开关。

但当前 wlcom 尚未公开这个开关，因此需要先提供一个 compositor-owned
D-Bus 方法或其他专用 IPC。兼容层再调用它。

优点：

- 不影响 `com.deepin.wm` 上的其他兼容方法；
- X11 和 Wayland 可以在同一个 shim 中按会话类型选择后端；
- 桌面文件不需要修改。

### 方式 B：由 `gxde-wlcom` 直接拥有 `com.deepin.wm`

这是调用链最短的做法。wlcom 在连接用户会话总线后：

1. 请求 well-known name `com.deepin.wm`；
2. 在 `/com/deepin/wm` 注册 `com.deepin.wm`；
3. 将 `PerformAction(1)` 直接连接到原生多任务开关。

必须在普通桌面组件可能调用该接口之前完成名称注册。建议在 compositor
启动早期注册，而不是第一次打开多任务视图时再注册。

注意：D-Bus 的 well-known name 不能由两个进程同时拥有。wlcom 一旦取得
`com.deepin.wm`，所有发往该名称的 WM 调用都会到 wlcom，而不只是
`PerformAction`。如果其他桌面组件仍依赖 `com.deepin.wm` 的
`MinimizeActiveWindow`、`ShowWorkspace` 等方法，就需要一并兼容；否则这些
调用会得到 `UnknownMethod`。

因此：

- 只要求菜单项可用时，最小实现 `PerformAction(i)` 即可；
- 要作为发行版默认实现时，应审计并覆盖仍有调用者的方法；
- 需要完整兼容时优先采用方式 A。

## D-Bus activation 与启动顺序

系统已经安装：

```text
/usr/share/dbus-1/services/com.deepin.wm.service
```

其内容会按需启动：

```ini
[D-BUS Service]
Name=com.deepin.wm
Exec=/usr/bin/deepin-wm-dbus
```

如果 wlcom 采用方式 B，应在该服务被激活前取得 `com.deepin.wm`。名称已有
所有者时，wlcom 必须记录错误，不能假装注册成功。

发行版集成时不要让两个实现竞态抢占名称。可选策略：

- Wayland 会话由 wlcom 在启动阶段取得名称，X11 会话继续使用 activation；
- 或把 activation 的 `Exec` 指向能够自行判断会话后端的新兼容服务；
- 不建议让两个服务使用 replace/queue 语义互相抢占。

## 错误处理

建议行为：

- `type == 1`：切换多任务视图并正常回复；
- 多任务模块尚未初始化：返回明确的 D-Bus failure；
- 不支持的动作值：返回 `org.freedesktop.DBus.Error.NotSupported`；
- D-Bus 名称获取失败：启动日志必须包含名称和具体错误；
- 多次快速调用：每次都按“切换”处理，不做额外去抖；动画和重入由多任务
  模块自身处理。

如果需要兼容只检查调用是否成功的旧客户端，也可以对未知动作正常回复但
不执行操作；不建议在新实现中静默吞掉错误。

## 验证

### 1. 确认接口所有者

```bash
busctl --user status com.deepin.wm
```

Wayland 直接接管方案中，进程应为 `gxde-wlcom`。兼容层方案中，应为新的或
修改后的 WM shim。

### 2. 确认方法签名

```bash
busctl --user introspect \
  com.deepin.wm \
  /com/deepin/wm \
  com.deepin.wm
```

输出中应有：

```text
PerformAction  method  i  -
```

### 3. 使用与桌面文件完全相同的调用测试

```bash
dbus-send --session \
  --dest=com.deepin.wm \
  --print-reply \
  /com/deepin/wm \
  com.deepin.wm.PerformAction \
  int32:1
```

验收标准：

1. 第一次调用打开 wlcom 原生多任务视图；
2. 第二次调用关闭；
3. 命令得到正常 method reply；
4. 不启动 `/usr/bin/deepin-wm-dbus` 的旧错误后端；
5. `Win+S` 仍与 D-Bus 调用表现一致；
6. 连续快速调用不会造成输入 grab 泄漏或多任务视图状态卡死。

### 4. 验证桌面入口

在应用启动器中点击「多任务视图」，行为应与上面的手动命令一致。无需修改
`gxde-multitaskingview.desktop`。

## 不建议的方案

- 用 `wtype`、`ydotool` 等工具模拟 `Win+S`；
- 在桌面文件中写复杂的 shell 分支；
- 修改桌面文件去调用一个只在本机存在、没有稳定契约的临时命令；
- 在 D-Bus handler 中重新实现多任务视图状态机；
- 为了一个方法强制启动 Qt/KWin effect；wlcom 的原生多任务实现不依赖它们。

