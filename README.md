## Description

A simple GUI utility to issue the switch input command via DDC to a monitor connected to an AMD GPU.

Looking for the macOS version? Check out [ddc-input-switcher-macos](https://github.com/jccr/ddc-input-switcher-macos).

[Download](https://github.com/jccr/amdddc-windows-gui/releases) the latest release.

## Features

Enables input switching on LG monitors that require using alternative i2c addresses that are missing from other Windows DDC utilities.

#### Input switching in the Windows tray

<img width="308" height="183" alt="image" src="https://github.com/user-attachments/assets/5e52e3e8-fc3f-4007-bf67-9a93241f3d65" />

#### Settings to configure display and hotkeys
<img width="483" height="355" alt="image" src="https://github.com/user-attachments/assets/5afa8bf8-cedd-4b8b-90b8-c852639e98cd" />

## Disclaimer

> [!IMPORTANT]
> This GUI utility will **_only_** work with an AMD GPU!
> 
> This is forked from the [original CLI utility](https://github.com/amildahl/amdddc-windows).
> 
> If you're using an Nvidia GPU, see [this project](https://github.com/kaleb422/NVapi-write-value-to-monitor) for using NVapi.
> 
> If you're using Intel, you could watch [this issue](https://github.com/rockowitz/ddcutil/issues/100) to see if someone writes a utility.

> [!Warning]
> This utility sends data over the i2c bus.  Use at your own risk.


## Development

Built using VS2022

1. Install Visual Studio BuildTools 2022, you can use `winget install Microsoft.VisualStudio.2022.BuildTools`
2. Run `.\build.bat` to build.
3. Binary outputs under `.\x64\Release`


## Credits

This is based from the original CLI utility, [amildahl/amdddc-windows](https://github.com/amildahl/amdddc-windows).

The ADL bits are based off the ADL sample [here](https://github.com/GPUOpen-LibrariesAndSDKs/display-library/blob/master/Sample/DDCBlockAccess/DDCBlockAccessDlg.cpp)

Commands, channel ids, etc were all helpfully sourced from the [ddcutil wiki](https://github.com/rockowitz/ddcutil/wiki/Switching-input-source-on-LG-monitors)

The DDC spec listed [here](https://boichat.ch/nicolas/ddcci/specs.html) was helpful in determing the i2cset command to issue.
