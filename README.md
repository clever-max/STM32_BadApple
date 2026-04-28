# 🎵 STM32 Bad Apple — OLED 视频 + 三蜂鸣器真和弦

> 用一块 STM32F103C8T6（成本 ¥8）和三只无源蜂鸣器（成本 ¥3），
> 播放 128×64 OLED 视频流 + 三声道真和弦音频，音画天然同步。

### 🎬 核心亮点

| ✨ | |
|---|---|
| **视频** | PC 串口→DMA 实时推流，30fps 流畅播放，任意时长 |
| **音频** | 三只蜂鸣器同时发声，**真正的三部和弦**（不是软件混音） |
| **音画同步** | 首帧自动触发蜂鸣器，物理级同步 |
| **MIDI→乐谱** | 一行命令 `convert_score.py song.mid --voices 3` 把任何 MIDI 变成 STM32 固件 |
| **交互控制** | 键盘实时切换和声模式 / 声道数量，无需重启 |
| **资源极限** | 4 个定时器（TIM1/2/3/4），DMA 接收，汇编级 I2C 优化 |

### 🎧 三蜂鸣器 = 真正的和弦

```
PA0 ─── TIM3 Update ISR ─── 高声部（旋律）   ┐
PA1 ─── TIM2 Update ISR ─── 中声部（和弦）   ├── 三音同时发声
PA3 ─── TIM1 Update ISR ─── 低声部（贝斯）   ┘
```

每个定时器独立产生不同频率的方波。没有数模转换、没有软件混音——三个蜂鸣器在空气中**物理叠加**形成真和弦。

---

## 📋 环境要求

| 软件 | 版本 / 安装方式 |
|------|----------------|
| Keil MDK-ARM | V5+，ARM Compiler V5 |
| Python | 3.7+ |
| FFmpeg | 系统安装，需在 PATH 中 |
| pyserial | `pip install pyserial` |
| Pillow | `pip install Pillow` |
| mido (MIDI转乐谱) | `pip install mido` |

| 硬件 | 说明 |
|------|------|
| STM32F103C8T6 最小系统板 | "Blue Pill" 或同等 |
| 0.96" OLED | 128×64, SSD1306, I2C |
| 无源蜂鸣器 ×1~3 | 3.3V 驱动 |
| USB 转 TTL | CH340/CP2102/FT232 |
| ST-Link V2 | 烧录用 |

---

## 🚀 快速开始

```bash
# 1. 克隆仓库
git clone https://github.com/clever-max/STM32_BadApple.git
cd STM32_BadApple

# 2. 安装 Python 依赖
pip install pyserial Pillow mido

# 3. Keil 打开 Project.uvprojx → Build → Download

# 4. 连接蜂鸣器 (PA0/PA1/PA3) 和串口 (PA9/PA10)

# 5. 推流视频 + 音频同步播放
cd video
python pc_streamer.py COM3
# → 拖入 Bad Apple 视频 → 选择声道模式 → 回车 → 播放！
```

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
    蜂鸣器1(+) ─────│ PA0 (TIM3)   │── 高声部
    蜂鸣器2(+) ─────│ PA1 (TIM2)   │── 中声部
    蜂鸣器3(+) ─────│ PA3 (TIM1)   │── 低声部
    蜂鸣器(-) ──────│ GND          │
                    │              │
    USB-TTL RX ─────│ PA9  (TX)    │── 应答
    USB-TTL TX ─────│ PA10 (RX)    │── 视频流
    USB-TTL GND ────│ GND          │
                    └──────────────┘
```

少接蜂鸣器也能工作——PA2 按钮可实时切 `1CH→2CH→3CH`。

---

## 🎹 MIDI → 乐谱转换

```bash
# 单声道（兼容旧格式）
python convert_score.py song.mid

# 三声道真和弦（自动检测多音同时发声）
python convert_score.py song.mid --voices 3 -o ../Hardware/music_score.h

# 先看看统计
python convert_score.py song.mid --voices 3 --count-only
# 输出: 声道分布: 0音=101, 1音=638, 2音=120, 3音=45
```

生成的 `music_score.h` 格式：

```c
static const MusicChord bad_apple_score[] = {
    {659, 523,   0, 250},  // 双音: PA0=E5  PA1=C5
    {784, 659, 523, 250},  // 三音: PA0=G5  PA1=E5  PA3=C5
    {  0,   0,   0, 250},  // 休止
};
```

支持输入格式：`.mid` / `.json` / `.csv` / `.c`

---

## 🕹️ 播放中实时控制

| 按键 | pc_streamer.py 播放中 |
|------|----------------------|
| `h` | 循环和声模式 (OFF→UNI→OCT→5TH) |
| `+/-` | 音量 ±10% |
| `s` | 显示当前状态 |
| `q` | 退出 |

| 按键 | MCU 端 PA2 按钮 |
|------|----------------|
| 按一下 | 声道数切换: 1CH→2CH→3CH→1CH... |

---

## 📁 项目结构

```
4pin/
├── Hardware/
│   ├── audio_pwm.c/h        ★ 三蜂鸣器真和弦驱动
│   ├── video_stream.c/h     ★ 视频流 DMA 接收 + 命令解析
│   ├── music_score.h        ★ 乐谱数据（MIDI 转换生成）
│   ├── OLED.c/h             SSD1306 驱动（汇编级 I2C 优化）
│   └── music_score.c        已废弃
├── User/
│   ├── main.c               ★ 视频+音频主程序
│   └── stm32f10x_it.c       中断服务（SysTick 已移至 main.c）
├── video/
│   ├── pc_streamer.py       ★ 交互式视频推流 (v2.1)
│   ├── convert_score.py     ★ MIDI/JSON/CSV → MusicNote/MusicChord
│   ├── buzzer_control.py      串口蜂鸣器测试遥控器
│   └── convert_video.py       视频→Flash C数组
├── main.c                    蜂鸣器测试程序（串口菜单版）
├── docs/                     详细技术文档
└── Project.uvprojx           Keil 工程
```

---

## ⏱️ 性能

| 指标 | 数值 |
|------|------|
| 视频帧率 | 30~40fps (I2C 上限 ~40fps) |
| 音频声道 | 1~3 声道真和弦 |
| 音频频率 | 20~4000Hz 任意频率 |
| 串口速率 | 921600 bps |
| 定时器 IRQ | 最高 3×2kHz(三音C6) ≈ 6k 中断/秒 |

---

## 📖 详细文档

见 [docs/](docs/) 目录：
- [README.md](docs/README.md) — 项目完整说明 + 视频流架构
- [优化过程分析与指南.md](docs/优化过程分析与指南.md) — 从 24.7fps 到 66.6fps 的完整优化历程
- [技术手册分析解读.md](docs/技术手册分析解读.md) — 硬件约束、通信协议、性能瓶颈深度分析
- [视频流播放优化方案.md](docs/视频流播放优化方案.md) — 基于数据手册的优化方案
- [PWM音频播放方案规划.md](docs/PWM音频播放方案规划.md) — 音频方案与实施计划

---

## ⚖️ 许可

OLED 驱动基于江协科技 V2.0 免费开源。ST 标准外设库及 CMSIS 版权归 STMicroelectronics / ARM 所有。
