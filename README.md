# STM32F103C8 — OLED 视频流 + 独立音乐播放器

> 一块 STM32F103C8T6（¥8），两个独立功能，Keil 中切换 Target 即可。

| 功能 | Target | 说明 |
|------|--------|------|
| **视频流播放** | Target 1: VideoPlayer | PC 串口→DMA 实时推流 128×64 OLED 视频 + 三蜂鸣器真和弦音频 |
| **音乐播放器** | Target 2: AudioPlayer | 独立运行，OLED 选歌界面，三蜂鸣器和弦，按键/串口双控 |

---

## 📋 环境要求

| 软件 | 说明 |
|------|------|
| Keil MDK-ARM | V5+，ARM Compiler V5 |
| Python 3.7+ | 视频推流用 |
| FFmpeg | 系统安装，需在 PATH 中 |
| `pyserial` `Pillow` `mido` | `pip install pyserial Pillow mido` |

| 硬件 | 说明 |
|------|------|
| STM32F103C8T6 最小系统板 | "Blue Pill" |
| 0.96" OLED | 128×64, SSD1306, I2C (SCL=PB8, SDA=PB9) |
| 无源蜂鸣器 ×3 | PA0 / PA1 / PA3，至少接 1 个即可工作 |
| USB 转 TTL (CH340 等) | 仅 Target 1 需要 |
| ST-Link V2 | 烧录用 |

---

## 🔌 硬件连接

```
                   STM32F103C8T6
                  ┌──────────────┐
  OLED SCL ───────│ PB8          │
  OLED SDA ───────│ PB9          │
  OLED VCC ───────│ 3.3V         │
  OLED GND ───────│ GND          │
                  │              │
  蜂鸣器1(+) ─────│ PA0 (高声部)   │
  蜂鸣器2(+) ─────│ PA1 (中声部)   │
  蜂鸣器3(+) ─────│ PA3 (低声部)   │
  蜂鸣器(-) ──────│ GND          │
                  │              │
  USB-TTL RX ─────│ PA9  (TX)    │── 仅 Target 1 需要
  USB-TTL TX ─────│ PA10 (RX)    │── 仅 Target 1 需要
  USB-TTL GND ────│ GND          │
                  │              │
  按键 PB3 ───────│ PB3 (下一首)   │── 仅 Target 2
  按键 PB4 ───────│ PB4 (音量+)    │── 仅 Target 2
  按键 PB5 ───────│ PB5 (音量-)    │── 仅 Target 2
  按键 PB6 ───────│ PB6 (上一首)   │── 仅 Target 2
                  └──────────────┘
```

---

## 🎬 Target 1 — 视频流播放 (VideoPlayer)

PC 端 Python 脚本实时读取视频 → 逐帧串口推流 → STM32 DMA 接收 → OLED 显示 + 蜂鸣器同步播放乐谱。

### 快速开始

```bash
# 1. 安装依赖
pip install pyserial Pillow mido

# 2. Keil 打开 Project.uvprojx
#    → 上方 Target 下拉选 "Target 1: VideoPlayer"
#    → Build (F7) → Download (F8)

# 3. 连接硬件 (OLED + 蜂鸣器 + USB-TTL)

# 4. 推流播放
cd video
python pc_streamer.py COM3
# → 拖入视频 → 选择声道模式 → 回车播放！
```

### 播放中控制

| 按键 | 作用 |
|------|------|
| `h` | 循环和声模式 (OFF→UNI→OCT→5TH) |
| `+`/`-` | 音量 ±10% |
| `q` | 退出 |
| MCU PA2 按钮 | 声道切换: 1CH→2CH→3CH |

---

## 🎵 Target 2 — 音乐播放器 (AudioPlayer)

独立运行，无需 PC。OLED 显示歌曲列表，按键或串口命令选歌播放。

### 歌曲列表

曲库位于 `audio/` 目录，当前包含 5 首：

| # | 歌曲 | 文件 |
|---|------|------|
| 1 | Bad Apple!! | `music_score_bad_apple.h` |
| 2 | 鳥の詩 | `music_score_tori_no_uta.h` |
| 3 | Air | `music_score_air.h` |
| 4 | 我怀念的 | `music_score_wohuainiande.h` |
| 5 | ____ | `music_score____.h` |

### 快速开始

```bash
# 1. Keil 打开 Project.uvprojx
#    → 上方 Target 下拉选 "Target 2: AudioPlayer"
#    → Build (F7) → Download (F8)

# 2. 连接硬件 (OLED + 蜂鸣器 + 按键)
#    上电 → OLED 显示歌曲菜单 → 按 PB3 播放第一首
```

### 控制方式

| 按键 | 短按 | 长按 |
|------|------|------|
| PB3 | 下一首 | 播放/暂停 |
| PB4 | 音量 +10% | 切换和声 |
| PB5 | 音量 -10% | 切换声道 |
| PB6 | 上一首 | 停止 |

串口命令 (115200 bps)：`n`下一首 `p`上一首 `空格`暂停 `s`停止 `h`和声 `v`声道 `+/-`音量

---

## 📁 项目结构

```
4pin/
├── User/main.c                ★ Target 1 主程序 (视频流)
├── audio/
│   ├── main.c                 ★ Target 2 主程序 (音乐播放器)
│   ├── songs.h                歌曲注册表
│   ├── music_score_*.h        乐谱数据
│   ├── control.py             串口遥控器
│   └── convert_to_audio.py    MIDI→乐谱 .h
├── Hardware/
│   ├── audio_pwm.c/h          三蜂鸣器真和弦驱动 (两 Target 共用)
│   ├── video_stream.c/h       视频流 DMA 接收 (Target 1)
│   ├── video_frames.c/h       Flash 模式帧数据
│   ├── video_player.c/h       Flash 模式播放器
│   ├── OLED.c/h               SSD1306 驱动 (汇编级 I2C)
│   └── music_score.c/h        乐谱播放
├── video/
│   ├── pc_streamer.py         ★ PC 视频推流 (v2.1)
│   ├── convert_score.py       MIDI→MusicChord 三和弦转换
│   ├── convert_video.py       视频→Flash C 数组
│   └── convert_badapple_score.py
├── System/Delay.c/h           SysTick 延时
├── Library/                   STM32F10x 标准外设库
├── Start/                     启动文件 + CMSIS
└── Project.uvprojx            Keil 工程 (含两个 Target)
```

### Keil Source Group 架构

```
Project
├── User/          → User/main.c (Target 1 编译)
├── Audio/         → audio/*.c (仅 Target 2 编译)
├── Video/         → Hardware/video_*.c (仅 Target 1 编译)
├── Hardware/      → 共用驱动 (两 Target 均编译)
├── System/        → Delay.c (共用)
├── Library/       → STM32 标准外设库 (共用)
└── Start/         → 启动文件 (共用)
```

> **切换 Target 时**：Keil 顶部下拉选 `Target 1: VideoPlayer` 或 `Target 2: AudioPlayer`，Audio/Video 文件夹的编译勾选会自动切换，无需手动调整文件。

---

## 📖 详细文档

见 [.trae/documents/docs/](.trae/documents/docs/) 目录：
- [README.md](.trae/documents/docs/README.md) — 项目完整架构 + 三模式详解
- [优化过程分析与指南.md](.trae/documents/docs/优化过程分析与指南.md) — 从 24.7fps 到 66.6fps 的完整优化历程
- [视频流播放优化方案.md](.trae/documents/docs/视频流播放优化方案.md) — 基于数据手册的优化分析
- [PWM音频播放方案规划.md](.trae/documents/docs/PWM音频播放方案规划.md) — 音频方案（PWM DAC 备选 vs 位翻转实际方案）

---

## ⚖️ 许可

OLED 驱动基于江协科技 V2.0 免费开源。ST 标准外设库及 CMSIS 版权归 STMicroelectronics / ARM 所有。
