# Project Occultation
## Introduction
GXDE OS is switching to `GXWM` from `gxde-kwin` for a long time for its Wayland session. However, we are forced to do that (due to some dependency issue) so we still want to keep the original experience.

However, it is hard to re-create similar UIs, especially those visual effects. So here is another idea: how about directly re-use those KWin's UI?

In fact, here is the structure of this sub-project: Project Occultation itself contains the original source code (partial of the UI) from `gxde-kwin`, and it also have a sub-project `Exoskeleton` that provides a minimal shim to emulate the KWin environment for the frontend to use. The emulation is brought possible by `GXWM`'s feature and `GXWM`'s `libkywc` provided a bridge for WM and Project Occultation to exchange information.

## License
This project may use mixed license, please refer to README/LICENSE under every folder, if any.
