# ReiPan 简体中文版

ReiPan 是《零～刺青之声～ / Fatal Frame III: The Tormented》的 PlayStation®2 至 PC 原生移植项目。本仓库基于 Mikompilation 的 ReiPan，为 Windows 提供日版游戏的简体中文适配。

## 适用版本

- 本分支仅提供 Windows 构建。
- 游戏资源读取已转换为日版 NTSC-J，对应游戏编号 `SLPS-25544`，光盘启动文件为 `SLPS_255.44`。
- 为了正常显示简体中文，**必须使用扑家汉化组制作的日版汉化 ISO**。本分支按该版本的字库、文字编码和资源布局适配。

## 游戏文件

请自行准备上述扑家汉化组的汉化 ISO。可以在游戏数据选择界面指定该 ISO，或从光盘中提取游戏数据后选择对应目录。提取时应保留完整光盘目录结构，主要文件布局如下。

```text
ReiPan/
├─ ReiPan-SimplifiedChinese.exe
└─ binary/
   ├─ IMG_BD.BIN
   ├─ ZERO3.FHD
   └─ SLPS_255.44
```

`IMG_BD.BIN` 与 `ZERO3.FHD` 需要放在同一目录。选定路径后，程序会将其保存在 `reipan.ini` 的 `[paths]` 配置项中。后续可通过设置菜单中的游戏数据文件夹选项修改路径。

## 调试框

游戏运行时，按 `F1` 可以打开调试框，访问设置与调试菜单。

## Windows 构建

准备 Windows C/C++ 编译工具链、CMake 3.20 或更新版本、Ninja 和 Git。依赖由 CMake 配置过程自动获取，Windows 使用 `extern/ffmpeg` 中提供的 FFmpeg 库。

```powershell
git clone https://github.com/kasumi-ko/ReiPan-SimplifiedChinese.git
cd ReiPan-SimplifiedChinese

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DREIPAN_SLPS_25544_CHS=ON
cmake --build build --target ReiPan --parallel
```

生成的可执行文件位于 `build/ReiPan/ReiPan-SimplifiedChinese.exe`。`REIPAN_SLPS_25544_CHS=ON` 用于启用日版简体中文数据适配。运行时请保留随构建生成的资源与依赖文件。发行构建直接打开游戏窗口，诊断信息写入用户数据目录下的 `reipan.log`。

如需复用仓库内的着色器字节码，可在 CMake 配置命令中加入 `-DREIPAN_COMPILE_SHADERS=OFF`。

## 图形与手柄

Windows 默认使用 Direct3D 12。可以在图形设置中选择图形后端，并在修改后重新启动程序。

举起射影机时，支持陀螺仪的手柄可通过转动手柄辅助瞄准。相关灵敏度、转动轴和反转选项可在控制设置中调整。

支持独立扳机马达的 Xbox 手柄可使用扳机震动。可在震动设置中调整强度及测试各个马达。

## PlayStation®2 存档转移

通过设置菜单中的 PlayStation®2 存档转移功能，可以导入或导出游戏存档。上游支持从提取的存档目录、Memory Card (8MB) (for PlayStation®2) 映像以及 PSU、MAX、XPS、SPS、CBS 存档包导入，并可导出为以下格式。

- 带 ECC 的 Memory Card (8MB) (for PlayStation®2) 映像（`.ps2`）。
- 无 ECC 的 Memory Card (8MB) (for PlayStation®2) 映像（`.bin`）。
- `.psu`、`.max`、`.xps`、`.sps`、`.cbs` 存档包。
- 提取的游戏存档目录。

转移前请关闭游戏内的存档或读档界面。导入时选定目标槽位，导出时选定目标地区，随后通过游戏的正常读档入口加载。跨地区转换会受到原版游戏的脚本与场景差异限制，工具会显示具体原因。

## 字体与界面

游戏内的简体中文显示依赖扑家汉化组 ISO 中的字库与汉化资源。Port 界面使用的字体随程序资源提供。

原版磁带使用光盘自带的字幕。简中版为部分预渲染影片、实时剧情和游戏内语音补充了字幕，使用光盘字库和游戏字幕开关。

高清字体替换文件需保留原字形布局、颜色和透明度，支持统一放大至 4 倍。将以字体指纹命名的目录复制到程序旁的 `resources/fonts/`，重新启动后，在设置中启用高清字体选项。

## 致谢

感谢 Mikompilation 及 [ReiPan](https://github.com/Mikompilation/ReiPan) 的所有贡献者，为《零～刺青之声～ / Fatal Frame III: The Tormented》的 PC 原生移植提供基础。感谢扑家汉化组的汉化工作。

## 版权与许可

《零～刺青之声～ / Fatal Frame III: The Tormented》原版作品的版权署名包括 **TECMO, LTD.**（特库摩）。《零》系列及其游戏名称、角色、剧情、美术、音乐、影像、原版程序和相关商标的权利归 **KOEI TECMO GAMES CO., LTD.**（光荣特库摩）及其他相应权利人所有。

“PlayStation”是 [Sony Interactive Entertainment Inc.](https://www.playstation.com/zh-hans-cn/legal/copyright-and-trademark-notice/) 的注册商标或商标。“SONY”是 Sony Group Corporation 的注册商标或商标。其他产品名称、服务名称、标志和公司名称的商标与版权归各自所有者或授权方所有。

本项目为社区移植与简体中文适配项目，开源移植及适配代码采用 [GNU Affero General Public License v3.0](LICENSE) 许可。

扑家汉化组制作的汉化文本与相关汉化资源保留原团队署名，相关权利归各自权利人所有。运行所需的 ISO 与光盘数据请自行准备。
