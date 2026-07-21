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

## 错误处理

接口会在下列情况下返回 D-Bus 错误：

- 参数超出允许范围或参数类型错误；
- 找不到指定屏幕；
- 当前没有可配置的主屏；
- 屏幕或后端不支持请求的配置。

缩放、分辨率和旋转设置保存在当前用户的 `~/.config/gxde-wlcom/config.json` 中。
