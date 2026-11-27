# ReiPan-SimplifiedChinese

零～刺青之声～ PC 移植的简体中文适配，当前版本为 **0.1.0 beta**。

简体中文适配作者为 **kasumi-ko (霞詩@bilibili)**。项目基于 Mikompilation 的 ReiPan，保留 GPL-3.0 许可证与上游署名。

[项目主页](https://github.com/kasumi-ko/reipan-simplifiedchinese) · [上游项目](https://github.com/Mikompilation/ReiPan) · [回归与发布清单](TODO.md)

## Windows 构建

使用 MSYS2 MinGW64 的 GCC、CMake、Ninja 和 Git。CMake 会获取库依赖，Windows 的 FFmpeg 位于 `extern/ffmpeg`。

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DREIPAN_COMPILE_SHADERS=OFF
cmake --build build --target ReiPan
```

构建结果位于 `build/ReiPan/ReiPan-SimplifiedChinese.exe`。运行时需要同目录的 `resources` 和 FFmpeg DLL。

## 准备游戏数据

使用你自己的 `SLPS_255.44` 简体中文 ISO 和 Python 3.9 或更新版本。运行以下命令，在 EXE 旁生成 `data` 目录。

```powershell
python tools/prepare_slps25544.py "D:\Games\Fatal Frame III CHS.iso" build/ReiPan/data
```

工具以只读方式打开 ISO，根据同盘资源表生成适配文件，并找回 ISO 目录记录末尾之外的 95 项资源。输出目录需要约 2.74 GB 可用空间。程序自动识别同目录的 `data`，放在其他位置时可在游戏数据选择器中选择适配后的 `IMG_BD.BIN`。路径保存在 `reipan.ini`。

主菜单和暂停菜单的「设置」可打开移植版选项。默认用 WASD 移动、左 Shift 奔跑、鼠标右键举起相机、左键拍摄。F1 切换当次运行的调试栏。

## Graphics API

ReiPan picks a graphics API by itself: Direct3D 12 on Windows, Vulkan on Linux,
Metal on macOS. Choose another in **Options → Graphics → Graphics API** or
**Settings → Graphics**, then restart. One that cannot be started falls back to
the automatic choice. If one starts but misbehaves, set `backend = auto` under
`[render]` in `reipan.ini`, or start ReiPan once with `SDL_GPU_DRIVER` set to
`direct3d12`, `vulkan` or `metal`.

## Gyro aiming

While the Camera Obscura is raised, a controller with a gyro (DualShock 4,
DualSense, Switch Pro, Steam Controller, ...) aims it by being turned, alongside
the stick and the mouse. It is on by default; **Options → Controls → Gyro** sets
the sensitivity, yaw or roll to turn, and the inversions. The gyro's drift is
learned whenever the controller lies still with the Camera Obscura down.

## Trigger rumble

A controller with motors behind its triggers (Xbox One and later) also plays there
what the rumble is for: heartbeats, blows, a ghost's grip, thunder and the
cutscenes. The grips shake as the game asks, and any other controller behaves as
before. It is on by default; **Options → Controls → Rumble** sets the strengths,
tests each motor, and can add the Camera Obscura (the shutter, the charge and
shutter chances), which the original does not shake for. A controller that Steam
Input stands in for only has its grips.

## PS2 save transfer

Open **Settings → PS2 save transfer…** to transfer European, US or Japanese
gameplay saves. Choose the target region when exporting. For European PS2 saves,
select the original game's PAL/60 Hz timing so mission time records remain correct.

Import from an extracted save folder, an 8 MB memory card, or a PSU/MAX/XPS/SPS/CBS
archive. Select saves and assign their destination slots. Export formats are:

- **ECC (.ps2):** a memory card with spare/ECC bytes, for PCSX2.
- **Raw (.bin):** an 8 MB card without ECC bytes, for OPL.
- **PSU (.psu):** an EMS archive, for uLaunchELF and PS2 save-transfer tools.
- **MAX (.max):** an Action Replay MAX archive.
- **XPS/SPS (.xps/.sps):** an X-Port V2 / SharkPort archive.
- **CBS (.cbs):** a CodeBreaker archive.
- **Extracted folder:** the game's save files, for tools that accept folders.

Archives contain this game's selected saves, not an entire memory card. Card
exports create a blank card populated with the selected saves. Choosing a format
opens its picker; confirming the destination exports immediately. Missing
extensions are added automatically; existing output files/folders are refused.
Imports replace only the selected destination slots. Load imported saves normally
afterward, and close the game's save/load screen before transferring.

Unavailable saves remain visible with an explanation; other valid slots can still
be transferred. Regional script differences can prevent transferring an active
scene or conversation. The extra US/Japanese Trailer unlock has no European
equivalent and is left unobtained when exporting from ReiPan.

## Font texture replacements

Redraw or upscale the PNGs uniformly up to 4×, preserving glyph layout, colours
and transparency. Copy the hash-named directory into `resources/fonts/` beside
the executable and restart. Enable **HD fonts (if available)** in Renderer or
**Use HD Fonts when available** in Game options. Missing or unmatched files
fall back to the original fonts.
