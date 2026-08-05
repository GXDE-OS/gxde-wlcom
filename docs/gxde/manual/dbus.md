# GXDE Wlcom D-Bus接口 (Alias)
## 屏幕设置

| 项目 | 值 |
| --- | --- |
| 总线 | Session Bus |
| 服务名 | `top.gxde.Wlcom.Screen` |
| 对象路径 | `/top/gxde/Wlcom/Screen` |
| 接口名 | `top.gxde.Wlcom.Screen` |

可以通过以下命令查看接口：

```bash
busctl --user introspect top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen
```

### SetScaleRatio
设置主屏缩放比例，调用成功后立即生效并写入当前用户配置。

```text
SetScaleRatio(double ratio)
```

- `ratio`：缩放比例，取值范围为 `1.0` 至 `3.0`，支持两位小数；超过两位的小数会四舍五入。
- 无返回值。

```bash
# 示例：将主屏缩放比例设置为1.25
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen SetScaleRatio d 1.25
```

### SetResolutionWRefreshRate

设置主屏分辨率和刷新率，调用成功后立即生效并写入当前用户配置。

```text
SetResolutionWRefreshRate(int32 width, int32 height, int32 refresh_rate)
```

- `width`：水平分辨率，必须为正整数。
- `height`：垂直分辨率，必须为正整数。
- `refresh_rate`：刷新率，单位为 Hz，必须为正整数。
- 无返回值。

指定的分辨率和刷新率必须受主屏支持，否则调用会失败。

```bash
# 示例：设置为1920×1200@60Hz
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen SetResolutionWRefreshRate iii 1920 1200 60
```

### SetScreenBrightness

设置指定屏幕的亮度，调用成功后立即生效。

```text
SetScreenBrightness(string screen, int32 brightness)
```

- `screen`：屏幕输出名称，例如 `eDP-1`、`HDMI-A-1`。
- `brightness`：亮度百分比，取值范围为 `0` 至 `100`。
- 无返回值。

可以通过既有的输出接口查询屏幕输出名称：

```bash
busctl --user call com.kylin.Wlcom /com/kylin/Wlcom/Output com.kylin.Wlcom.Output ListAllOutputs
```

```bash
# 示例：将eDP-1的亮度设置为100%
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen SetScreenBrightness si eDP-1 100
```

### RotateScreen

设置主屏相对于正常方向的顺时针旋转角度，调用成功后立即生效并写入当前用户配置。

```text
RotateScreen(int32 angle)
```

- `angle`：只接受 `0`、`90`、`180` 或 `270`。
- 无返回值。

```bash
# 示例：将主屏向右旋转90度
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen RotateScreen i 90
```

### 逐屏设置

以下接口使用输出名称指定屏幕，适合控制中心的多屏设置页面。设置成功后立即生效并写入
当前用户配置：

```text
SetScreenScale(string screen, double ratio)
SetScreenResolution(string screen, int32 width, int32 height, int32 refresh_rate)
SetScreenRotation(string screen, int32 angle)
SetScreenEnabled(string screen, bool enabled)
SetPrimaryScreen(string screen)
SetScreenPosition(string screen, int32 x, int32 y)
```

参数规则与前述主屏接口一致：

- `ratio` 取值范围为 `1.0` 至 `3.0`；
- `refresh_rate` 单位为 Hz；
- `angle` 只接受 `0`、`90`、`180` 或 `270`；
- 不能关闭最后一块已启用的屏幕；
- 只有已启用的屏幕可以设为主屏或移动位置。

```bash
# 将HDMI-A-1缩放设为1.25
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen SetScreenScale sd HDMI-A-1 1.25

# 将HDMI-A-1设为1920×1080@60Hz
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen SetScreenResolution siii HDMI-A-1 1920 1080 60

# 将HDMI-A-1顺时针旋转90度
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen SetScreenRotation si HDMI-A-1 90

# 启用HDMI-A-1并设为主屏
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen SetScreenEnabled sb HDMI-A-1 true
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen SetPrimaryScreen s HDMI-A-1

# 将HDMI-A-1移动到逻辑坐标(1920, 0)
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen SetScreenPosition sii HDMI-A-1 1920 0
```

### SetScreenMode
设置多屏显示模式，调用成功后立即生效并写入当前用户配置。

```C
SetScreenMode(uint32 mode, string screen)
```

- `mode`：
  - `0`：复制。启用所有屏幕，将它们放在相同坐标，并自动使用所有屏幕共同支持的最大分辨率；
  - `1`：扩展。启用所有屏幕，并按照当前顺序从左到右排列；
  - `2`：仅在指定屏幕上显示。启用 `screen` 指定的屏幕并关闭其他屏幕。
- `screen`：仅在 `mode` 为 `2` 时使用；其他模式传空字符串即可。
- 无返回值。

```bash
# 复制屏幕
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen SetScreenMode us 0 ""

# 扩展屏幕
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen SetScreenMode us 1 ""

# 仅在HDMI-A-1上显示
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen SetScreenMode us 2 HDMI-A-1
```

### SetScreenLayout

按照参数中的逻辑坐标排布当前已启用的屏幕。支持左右、上下、错位以及其他二维排列。
数组必须包含每个已启用的物理屏幕且不能重复。此方法只改变屏幕位置，不启用或
关闭屏幕；如需启用全部屏幕，应先切换至扩展模式。

```text
SetScreenLayout(array<(string screen, int32 x, int32 y)> layout)
```

- `screen`：屏幕输出名称。
- `x`、`y`：屏幕左上角在合成器全局空间中的逻辑坐标，允许为负数。
- 无返回值。

```bash
# eDP-1在左，HDMI-A-1在右
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen SetScreenLayout 'a(sii)' 2 eDP-1 0 0 HDMI-A-1 1920 0

# eDP-1在上，HDMI-A-1在下
busctl --user call top.gxde.Wlcom.Screen /top/gxde/Wlcom/Screen top.gxde.Wlcom.Screen SetScreenLayout 'a(sii)' 2 eDP-1 0 0 HDMI-A-1 0 1080
```

## 错误处理

接口会在下列情况下返回 D-Bus 错误：

- 参数超出允许范围或参数类型错误；
- 找不到指定屏幕；
- 当前没有可配置的主屏；
- 屏幕布局缺少已启用的屏幕、包含重复屏幕或包含已关闭屏幕；
- 复制模式下少于两块屏幕，或屏幕之间没有共同支持的分辨率；
- 屏幕或后端不支持请求的配置。

缩放、分辨率、旋转、显示模式和屏幕布局设置保存在当前用户的
`~/.config/gxde-wlcom/config.json` 中。

## 截图

| 项目 | 值 |
| --- | --- |
| 总线 | Session Bus |
| 服务名 | `top.gxde.Wlcom.Screenshot` |
| 对象路径 | `/top/gxde/Wlcom/Screenshot` |
| 接口名 | `top.gxde.Wlcom.Screenshot` |

可以通过以下命令查看接口：

```bash
busctl --user introspect top.gxde.Wlcom.Screenshot /top/gxde/Wlcom/Screenshot top.gxde.Wlcom.Screenshot
```

### CopyFullscreenToClipboard

截取所有已启用屏幕组成的整个布局到剪切板

```text
CopyFullscreenToClipboard()
```

- 无参数，无返回值
- 剪贴板内容以 `image/png` 与 `PNG` 两种类型广播

```bash
busctl --user call top.gxde.Wlcom.Screenshot /top/gxde/Wlcom/Screenshot top.gxde.Wlcom.Screenshot CopyFullscreenToClipboard
```

如果上一次截图还没完成，调用会返回 `top.gxde.Wlcom.Screenshot.Error.AlreadyTaking` 错误。

默认配置把该方法绑定到了 `PrintScreen` 键（见 `/etc/gxde-wlcom/config.json` 中
`InputAction.keyboard` 的 `Print:no`），按下即把全屏复制到剪贴板。若要改键或禁用，
按照该文件里其他快捷键的写法修改用户配置 `~/.config/gxde-wlcom/config.json` 即可。
