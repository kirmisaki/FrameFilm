# 帧影（FrameFilm）电子胶片冰箱贴

<div align="center">

![FrameFilm渲染图](assets/pic/model/rendering2.png)

*复古胶片质感 × 电子纸显示 × 磁吸安装*

[![License](https://img.shields.io/badge/license-GPL--3.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32-green.svg)](https://www.espressif.com/) 

</div>

---

## 项目概述

帧影（FrameFilm）✨ 一款超有氛围感的彩色电子纸冰箱贴！📺

主打胶片复古质感，把家的仪式感拉满～轻轻一贴，定格生活每一帧的美好瞬间。💫

"帧影"寓意"一帧一影，定格时光"，英文名FrameFilm结合"帧"与"胶片"，满满的复古情怀 🎞️

## 核心功能

- 🎞️ **胶片质感呈现**：多种抖动算法加持，颗粒感满满，秒回复古胶片时代！
- 🖼️ **彩色电子纸载体**：低功耗无蓝光，阳光下也清晰，充一次电能用好几个月～
- 📱 **便捷照片传输**：蓝牙秒连手机，一键上传超方便，还能定时轮播生活瞬间！
- 🧲 **磁吸便捷安装**：背部磁铁设计，往冰箱上一贴就搞定，安装 so easy！
- ✨ **简约小巧设计**：轻薄高颜值，适配各种家居风格，摆在哪儿都是风景线！

### 固件组件

| 组件 | 说明 |
|------|------|
| `film_sys` | 系统初始化、日志、配置管理 |
| `film_service` | 核心业务服务（BLE通信、文件管理、照片播放、参数存储） |
| `film_hal` | 硬件抽象层（电子纸、电池、LED、编码器、存储） |

## 硬件规格

### FrameFilm（基础版）

| 参数 | 规格 |
|------|------|
| 主控芯片 | ESP32-S3 WROOM N16R8 |
| 显示屏 | 彩色电子纸 3.6" 600×400 (WFT驱动) |
| 存储 | TF卡 最大32GB 或 内置SDnand |
| 通信 | 蓝牙 BLE 4.2 |
| 交互 | 旋转编码器（带按键） |
| 电池 | 锂电池 304040规格 1.5mm插头 |
| 尺寸 | 约 92 × 60 × 7 mm |
| 安装方式 | 背部磁吸 磁铁 2x12mm-1mm 1x15mm-2mm |

### FrameFilm Pro

| 参数 | 规格 |
|------|------|
| 主控芯片 | ESP32-S3 mini N4R2 |
| 显示屏 | 彩色电子纸 3.68" 792×528 (SE0368-C驱动) |
| 存储 | 内置SDnand |
| 通信 | 蓝牙 BLE 4.2 / WiFi |
| 交互 | 三按键 |
| 电池 | 锂电池 244147规格 |
| 充电 | 支持无线充电（兼容 MagSafe 充电器） |
| 尺寸 | 约 90 × 59 × 5 mm |
| 安装方式 | 背部磁吸 magasafe磁环 |

## 硬件开源

硬件设计已在立创开源硬件平台开源：

👉 [立创开源硬件平台 - FrameFilm](https://oshwhub.com/kiritro/project_ttfkoxxv)  
👉 [立创开源硬件平台 - FrameFilmPro](https://oshwhub.com/kiritro/project_wgzqduhs)
👉 [立创开源硬件平台 - FrameFilm星火版](https://oshwhub.com/kiritro/project_uaqmgawa)

💬 复刻交流 QQ 群：**1103626779**（欢迎入群交流制作经验）

## 项目结构

```
FrameFilm/
├── assets/                     # 资源文件
│   └── pic/                   # 产品图片和渲染图
│
├── docs/                      # 文档资料
│   ├── blecmd/                # BLE 通信协议文档
│   ├── datasheet/             # 芯片和模块数据手册
│   ├── film/                  # film 文件格式规范
│   ├── filmhub/               # 云端相框服务设计文档
│   ├── hardware/              # 硬件规格说明文档
│   ├── knowledge/             # AI 知识库（架构/规范/命令速查）
│   └── wifi/                  # WiFi 功能说明（Pro 版）
│
├── firmware/                   # 设备固件 (ESP-IDF)
│   └── frame_film/
│       ├── components/
│       │   ├── film_service/  # 服务层
│       │   ├── film_sys/      # 系统层
│       │   └── film_hal/      # 硬件抽象层
│       ├── main/              # 主程序
│       └── sdkconfig_*        # 各机型配置 (std/pro/max)
│
├── hardware/                   # 硬件设计
│   ├── pcb/                   # PCB 电路原理图
│   └── model/                 # 外壳 3D 模型
│
├── server/                     # 云端服务
│   ├── backend/               # 后端 API 服务 (FastAPI)
│   └── web/                   # Web 管理前端
│
├── tools/                      # 开发工具
│   ├── convert/               # 照片转换工具
│   ├── ForFilm/               # Web 端相框管理工具
│   └── wechart/               # 微信小程序
│
├── AGENTS.md                   # AI 开发指南
├── README.md                   # 项目说明文档
└── LICENSE                     # 许可证
```

## 快速开始

### 固件开发

固件为多机型统一源码，编译前需先完成机型配置（替换 sdkconfig + 修改设备类型宏），再编译烧录。

1. **环境要求**
   - ESP-IDF v5.3+

2. **配置目标机型与屏幕**
   - 替换 `sdkconfig`：将 `firmware/frame_film/sdkconfig_<机型>` 复制为 `sdkconfig`
   - 修改设备类型宏：编辑 `firmware/frame_film/components/film_sys/inc/sys_cfg.h`，将对应机型宏置 1（三选一）
   - 选择屏幕：编辑 `firmware/frame_film/components/film_hal/inc/hal_epd.h`，在对应机型分支内将目标屏幕的 `EPD_SELECT_E6_*` 置 1、其余置 0

   | 机型 | 替换用的 sdkconfig | sys_cfg.h 宏 | 默认屏幕 |
   |------|--------------------|--------------|---------|
   | 基础版 | `sdkconfig_std` | `FRAMEFILM_STD` | 3.6" 600×400 |
   | Pro 版 | `sdkconfig_pro` | `FRAMEFILM_PRO` | 3.68" 792×528 |
   | Max 版 | `sdkconfig_max` | `FRAMEFILM_MAX` | 7.09" 1200×1600 双面板 |

   可选屏幕（`hal_epd.h` 的 `EPD_SELECT_E6_*` 宏）：

   | 屏幕 | 分辨率 | 面板 ID | 宏 |
   |------|--------|---------|-----|
   | 3.68" | 792×528 | 0x01 | `EPD_SELECT_E6_3_68_792_528` |
   | 3.70" | 720×480 | 0x02 | `EPD_SELECT_E6_3_70_720_480` |
   | 3.6" | 600×400 | 0x03 | `EPD_SELECT_E6_3_60_600_400` |
   | 1.54" | 240×240 | 0x04 | `EPD_SELECT_E6_1_54_240_240` |
   | 7.09" 双面板 | 1200×1600 | 0x05 | `EPD_SELECT_E6_7_09_1600_1200` |

   ```bash
   cd firmware/frame_film
   cp sdkconfig_std sdkconfig   # 示例：基础版；Pro/Max 版同理
   ```

3. **编译固件**
   ```bash
   idf.py build
   ```

4. **烧录固件**
   ```bash
   idf.py flash monitor
   ```

#### 使用 VS Code ESP-IDF 插件

项目已包含 `.vscode/settings.json`（IDF 路径已配置），按以下步骤即可编译烧录：

1. 安装扩展：VS Code 扩展商店搜索 **Espressif IDF** 并安装
2. 打开项目：文件 → 打开文件夹 → 选择 `firmware/frame_film`
3. 配置机型：与命令行方式相同（替换 `sdkconfig` + 修改 `sys_cfg.h` 机型宏）
4. 选择目标芯片：底部状态栏点击芯片图标，选择 **esp32s3**
5. 选择串口：底部状态栏点击 **COM 端口**，选择设备对应的串口
6. 编译：点击底部状态栏的 **构建图标（火焰）**，或按 `Ctrl+E` 然后 `B`
7. 烧录：点击 **烧录图标**，或按 `Ctrl+E` 然后 `F`
8. 串口监视：点击 **监视图标**，或按 `Ctrl+E` 然后 `M`（`Ctrl+]` 退出）

> 提示：首次构建时间较长属正常；若更换串口/芯片，在底部状态栏重新选择即可。

### 照片转换

#### ForFilm
ForFilm Web 工具，支持以下功能：

- **照片格式转换** - 将普通照片转换为 .Film 格式文件
- **蓝牙连接** - 与 FrameFilm 设备通过蓝牙连接实现控制和文件传输管理
- **OTA升级** - 无线更新设备固件
- **抖动效果调整** - 实时调整照片转换参数和抖动效果

**在线工具（GitHub Pages）：**
👉 [FrameFilm Web 工具](https://kirmisaki.github.io/FrameFilm/tools/ForFilm/)

#### 微信小程序

FrameFilm 微信小程序，方便在手机端管理和传输照片：

- **蓝牙连接** - 连接 FrameFilm 设备进行通信
- **相框管理** - 管理多个相框设备
- **照片传输** - 从手机相册上传或直接拍照发送到设备
- **手绘创作** - 在手机上手绘图案并发送显示

**小程序码，扫码即可体验。**  
![小程序码](assets/pic/wechartQR.png)

#### Film-hub（云端相框服务）

局域网部署的云端相框服务，为设备提供 WiFi 内容推送、模板渲染与远程管理能力：

- **设备管理** - 设备心跳注册与认领，实时查看电量、在线状态与设备配置
- **相册管理** - 批量上传照片，服务端转换生成 film 文件
- **模板引擎** - 内置日历/天气/备忘录/倒计时等模板，支持画布拖拽自定义
- **轮播流** - 编排多模板轮播（相对/绝对时间调度），设备定时拉取更新
- **AI 接入** - 对话生成模板、AI 生图

**技术栈：** FastAPI + SQLite + Vue 3，单机局域网部署。

- 设计文档：[docs/filmhub/](docs/filmhub/)（需求与技术方案）
- 后端源码：`server/backend/` ｜ Web 前端：`server/web/`

## 开发指南

详细的开发文档请参考各模块 README：

- [固件开发](firmware/README.md) - ESP32 固件代码开发
- [硬件设计](hardware/README.md) - PCB 和外壳设计文件
- [工具使用](tools/README.md) - ForFilm Web 工具和辅助脚本
- [微信小程序](tools/wechart/README.md) - 小程序源码
- [Film-hub 云端服务](docs/filmhub/) - 相框云服务需求与技术方案

技术文档目录：

- [docs/blecmd/](docs/blecmd/) - BLE 通信协议文档
- [docs/datasheet/](docs/datasheet/) - 芯片和模块数据手册
- [docs/film/](docs/film/) - Film 文件格式规范
- [docs/filmhub/](docs/filmhub/) - 云端相框服务需求与技术方案
- [docs/hardware/](docs/hardware/) - 硬件规格说明
- [docs/knowledge/](docs/knowledge/) - AI 知识库（架构/规范/命令速查）
- [docs/wifi/](docs/wifi/) - WiFi 功能说明（Pro 版）

## 免责声明

本项目（FrameFilm/帧影）为开源学习与兴趣制作项目，所提供的一切源码、硬件设计、文档及资料均按「现状」提供，仅供学习、研究与非商业用途参考。

- 本项目不提供任何明示或暗示的保证，包括但不限于对适销性、特定用途适用性及不侵权的保证。
- 使用者应自行评估并承担因使用本项目资料（包括固件烧录、电路制作、电池连接、磁吸安装、无线充电等）所产生的一切风险与后果；因操作不当、硬件差异或元件批次差异导致的设备损坏、数据丢失、人身伤害或财产损失，项目作者与贡献者不承担任何责任。
- 本项目涉及蓝牙、WiFi、云端服务等功能，使用者须遵守所在国家/地区的法律法规及无线电管理规定，不得用于任何违法用途。
- 使用者基于本项目代码或资料所实施的任何行为（包括但不限于复制、修改、分发、商业使用或二次开发）若引发侵犯第三方权利（如知识产权、商标、专利、肖像、隐私等）或其他纠纷，均由实施该行为的使用者自行承担全部责任，本项目作者与贡献者概不负责。
- 项目中的产品图片、渲染图及品牌名称仅供参考，不构成任何商业承诺或质量保证。

使用本项目即视为已阅读并同意上述条款。

## 开源许可

本项目基于 [GNU General Public License v3.0 (GPLv3)](LICENSE) 开源。

## 致谢

- [ESP-IDF](https://github.com/espressif/esp-idf) - ESP32 开发框架
