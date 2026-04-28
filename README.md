# STM32F103C8 + 0.96" OLED 视频播放器

基于 STM32F103C8T6 的 OLED 视频流播放项目，通过 PC → USB 串口 → STM32 → I2C → OLED 链路实现实时视频播放。

## 快速开始

1. Keil MDK 打开 `Project.uvprojx` → Build → Download
2. 连接 USB 转 TTL 到 STM32（TX→PA10, RX→PA9, GND→GND）
3. `cd video && python pc_streamer.py` → 输入视频路径和帧率

## 硬件连接

| OLED | STM32 |
|------|-------|
| SCL | PB8 |
| SDA | PB9 |
| VCC | 3.3V |
| GND | GND |

| USB 转 TTL | STM32 |
|-----------|-------|
| TX | PA10 |
| RX | PA9 |
| GND | GND |

| 蜂鸣器 | STM32 |
|------|-------|
| I/O (PWM) | PA0 |
| VCC | 3.3V |
| GND | GND |

## 文档

完整项目文档、优化历程和技术手册见 [docs/](docs/) 目录：

| 文档 | 内容 |
|------|------|
| [README.md](docs/README.md) | 项目完整说明 |
| [优化过程分析与指南.md](docs/优化过程分析与指南.md) | 从 24.7fps 到 66.6fps 的完整优化历程 |
| [技术手册分析解读.md](docs/技术手册分析解读.md) | 硬件约束、通信协议、性能瓶颈深度分析 |
| [视频流播放优化方案.md](docs/视频流播放优化方案.md) | 基于数据手册的优化方案制定 |
