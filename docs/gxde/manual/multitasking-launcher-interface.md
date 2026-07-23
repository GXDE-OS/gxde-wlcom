# 在 Wayland 会话接管「多任务视图」与「显示桌面」入口

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

「显示桌面」是另一个需要接管的入口。它目前不是 D-Bus 启动器，而是执行：

```ini
Exec=/usr/lib/deepin-daemon/desktop-toggle
```

该程序只读取并发送 X11 的 `_NET_SHOWING_DESKTOP` ClientMessage。在 Wayland
会话中即使存在 Xwayland，它也不能控制 Wayland 原生窗口，所以需要保留这个
命令行入口并替换其 Wayland 后端。

两个入口的现状：

| 功能 | 桌面文件 | 当前入口 | wlcom 原生快捷键 |
| --- | --- | --- | --- |
| 多任务视图 | `gxde-multitaskingview.desktop` | `com.deepin.wm.PerformAction(1)` | `Win+S` |
| 显示桌面 | `deepin-toggle-desktop.desktop` | `/usr/lib/deepin-daemon/desktop-toggle` | `Win+D` |

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

## 「显示桌面」入口与后端契约

### 需要保留的命令行契约

应用菜单直接执行：

```text
/usr/lib/deepin-daemon/desktop-toggle
```

重写后仍应满足：

- 不需要命令行参数；
- 每次运行切换一次“显示桌面”状态；
- 成功时退出码为 `0`；
- 失败时使用非零退出码并把原因写到标准错误；
- X11 会话可以继续使用 `_NET_SHOWING_DESKTOP`；
- Wayland 会话必须调用 compositor 后端，不能向 Xwayland 根窗口发事件。

因此可以保留桌面文件不变，只把 `desktop-toggle` 重写成会话感知的轻量
客户端：

```text
X11
  -> 现有 _NET_SHOWING_DESKTOP 实现

Wayland
  -> wlcom 的显示桌面 IPC
  -> view_manager_show_desktop(!view_manager_get_show_desktop(), true)
```

### wlcom 内部对接点

当前实现已经提供：

```text
view_manager_get_show_desktop()
view_manager_show_desktop(bool enabled, bool apply)
```

定义位于：

```text
include/view/view.h
src/view/view.c
```

Wayland 的“切换显示桌面”入口应调用：

```text
view_manager_show_desktop(!view_manager_get_show_desktop(), true)
```

`apply` 必须为 `true`，否则只会改变内部状态而不会真正最小化或恢复窗口。

### 建议公开的 IPC

如果复用本文件中的 `com.deepin.wm` 兼容服务，可以实现现有接口中的：

| 方法 | 输入 | 输出 | 含义 |
| --- | --- | --- | --- |
| `GetIsShowDesktop` | 空 | `b` | 返回当前显示桌面状态 |
| `SetShowDesktop` | `b` | 空 | 明确进入或退出显示桌面 |

仅用 `GetIsShowDesktop` 加 `SetShowDesktop` 也能实现切换，但两次 D-Bus
调用之间存在状态竞争。更适合菜单入口的是在 wlcom 自有接口上额外提供一个
原子方法：

```text
com.kylin.Wlcom.View.ToggleShowDesktop()
```

推荐分工：

```text
desktop-toggle
  -> ToggleShowDesktop()

需要明确设置状态的桌面组件
  -> GetIsShowDesktop()
  -> SetShowDesktop(bool)
```

若不希望增加新方法，也可以让 `desktop-toggle` 调用
`GetIsShowDesktop()` 后再调用 `SetShowDesktop(!state)`；必须接受并发调用时
可能发生两次切换合并的问题。

### 快捷键

wlcom 当前注册的相关快捷键：

| 快捷键 | 行为 | 内部动作 |
| --- | --- | --- |
| `Win+S` | 切换多任务视图 | 原生 multitask toggle |
| `Win+D` | 切换显示桌面/恢复窗口 | `TOGGLE_SHOW_DESKTOP` |
| `Win+H` | 进入显示桌面 | `SHOW_DESKTOP` |
| `Win+G` | 恢复桌面窗口 | `RESTORE_DESKTOP` |
| `Win+M` | 最小化所有窗口 | `MINIMIZE_ALL_VIEWS` |
| `Win+Shift+M` | 恢复所有窗口 | `RESTORE_ALL_VIEWS` |

这些绑定位于：

```text
src/view/action.c
```

桌面入口和快捷键必须落到相同的 `view_manager_show_desktop()` 状态机，否则
可能出现快捷键认为桌面已显示、菜单入口却认为未显示的状态分裂。

### 为什么不能直接调用 KGlobalAccel

当前 wlcom 的 `org.kde.kglobalaccel.Component.invokeShortcut` 虽然存在，
但 `Win+S` 是在 KGlobalAccel 扫描内建快捷键之后才注册的，因此它不在
`/component/gxde_wlcom` 的 action 列表中。当前会话中不能通过
`invokeShortcut("win+s:no", "default")` 可靠触发它。

`Win+D` 虽然出现在当前 KGlobalAccel action 列表中，也不应把
`invokeShortcut` 当作 `desktop-toggle` 的稳定后端。当前实现的
`invokeShortcut` 主要发送兼容信号，并没有为 compositor 内建 action
提供一个明确、稳定的命令执行契约。显示桌面应使用专用 compositor IPC。

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

### 5. 验证「显示桌面」

首先验证命令行入口：

```bash
/usr/lib/deepin-daemon/desktop-toggle
```

验收标准：

1. 第一次运行最小化当前工作区中尚未最小化的普通窗口并显示桌面；
2. 第二次运行只恢复由“显示桌面”动作最小化的窗口；
3. `Win+D` 与命令行入口交替使用时，状态始终一致；
4. 已经手动最小化的窗口不会被错误恢复；
5. 切换工作区后不会保留错误的显示桌面状态；
6. Wayland 路径不访问 `_NET_SHOWING_DESKTOP`；
7. X11 路径继续保持原行为。

如果实现了兼容 D-Bus 状态接口，还应验证：

```bash
busctl --user call \
  com.deepin.wm \
  /com/deepin/wm \
  com.deepin.wm \
  GetIsShowDesktop
```

以及：

```bash
busctl --user call \
  com.deepin.wm \
  /com/deepin/wm \
  com.deepin.wm \
  SetShowDesktop \
  b true
```

## 不建议的方案

- 用 `wtype`、`ydotool` 等工具模拟 `Win+S`；
- 通过模拟 `Win+D` 实现 `desktop-toggle`；
- 在桌面文件中写复杂的 shell 分支；
- 修改桌面文件去调用一个只在本机存在、没有稳定契约的临时命令；
- 在 D-Bus handler 中重新实现多任务视图状态机；
- 为了一个方法强制启动 Qt/KWin effect；wlcom 的原生多任务实现不依赖它们。
