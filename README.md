# 帧影（FrameFilm）电子胶片冰箱贴

<div align="center">

![FrameFilm渲染图](assets/pic/model/rendering.png)

*复古胶片质感 × 电子纸显示 × 磁吸安装*

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-ESP32-green.svg)](https://www.espressif.com/)

</div>

---

## 项目概述

帧影（FrameFilm）✨ 一款超有氛围感的彩色电子纸冰箱贴！📺

主打胶片复古质感，把家的仪式感拉满～轻轻一贴，定格生活每一帧的美好瞬间。💫

"帧影"寓意"一帧一影，定格时光"，英文名FrameFilm结合"帧"与"胶片"，满满的复古情怀 🎞️

## 产品展示

| 正面视图 | 侧面视图 |
|:--------:|:--------:|
| ![正面](assets/pic/model/model1.png) | ![侧面](assets/pic/model/model2.png) |

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

| 参数 | 规格 |
|------|------|
| 主控芯片 | ESP32-S3 |
| 显示屏 | 彩色电子纸（E-Ink 6）3.6" |
| 存储 | TF卡 |
| 通信 | 蓝牙 BLE 4.2 |
| 电池 | 锂电池（可充电） |
| 尺寸 | 约 70 × 58 × 9 mm |
| 安装方式 | 背部磁吸 |

## 项目结构

```
FrameFilm/
├── assets/                    # 资源文件
│   └── pic/model/            # 产品图片和渲染图
│
├── docs/                      # 文档资料
│   ├── api/                   # API 接口文档
│   ├── design/                # 设计文档和原型图
│   ├── user-guide/            # 用户使用手册
│   └── development/            # 开发文档
│
├── firmware/                   # 设备固件 (ESP-IDF)
│   └── frame_film/
│       ├── components/
│       │   ├── film_service/  # 服务层
│       │   ├── film_sys/      # 系统层
│       │   ├── film_hal/      # 硬件抽象层
│       └── main/              # 主程序
│
├── hardware/                   # 硬件设计
│   ├── pcb/                   # PCB 设计文件
│   ├── schematics/            # 电路原理图
│   ├── 3dmodels/              # 外壳 3D 模型
│   ├── bom/                   # 物料清单
│   ├── datasheets/            # 芯片数据手册
│   └── gerber/                # PCB 加工文件
│
├── tools/                      # 开发工具
│   ├── convert/               # 照片转换工具
│   │   ├── convert_tool.html  # 照片转 Film Web 工具
│   │   ├── analyze_image.py   # 图片分析脚本
│   │   └── image_h/           # 转换后的图片数据
│   ├── flash/                 # 固件烧录工具
│   ├── debug/                 # 调试工具
│   └── production/            # 生产测试工具
│
├── README.md                   # 项目说明文档
└── LICENSE                     # 许可证
```

## 快速开始

### 固件开发

1. **环境要求**
   - ESP-IDF v5.3+

2. **编译固件**
   ```bash
   cd firmware/frame_film
   idf.py build
   ```

3. **烧录固件**
   ```bash
   idf.py flash monitor
   ```

### 照片转换

ForFilm Web 工具，支持以下功能：

- **照片格式转换** - 将普通照片转换为 .Film 格式文件
- **蓝牙连接** - 与 FrameFilm 设备通过蓝牙连接实现控制和文件传输管理
- **OTA升级** - 无线更新设备固件
- **抖动效果调整** - 实时调整照片转换参数和抖动效果

**在线工具（GitHub Pages）：**
👉 [FrameFilm Web 工具](https://kirmisaki.github.io/FrameFilm/tools/ForFilm/)

## 开发指南

详细的开发文档请参考 [docs/](docs/) 目录：

- [固件开发](firmware/README.md)
- [硬件设计](hardware/README.md)
- [工具使用](tools/README.md)

## 开源许可

本项目基于 [GNU General Public License v3.0 (GPLv3)](LICENSE) 开源。

## 致谢

- [ESP-IDF](https://github.com/espressif/esp-idf) - ESP32 开发框架
