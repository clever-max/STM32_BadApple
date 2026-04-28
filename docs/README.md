# STM32F103C8 0.96寸 OLED 视频播放项目

基于 STM32F103C8T6 的 0.96 寸 OLED 显示屏（4 针 I2C 接口）项目，支持两种视频播放模式：

- **Flash 模式**：视频帧预存 MCU Flash，独立播放（≤60帧，\~6秒）
- **串口流模式**：PC 通过 USB 转串口实时发送帧数据 → 任意时长视频 + 高帧率

***

## 硬件平台

| 项目          | 参数                                     |
| ----------- | -------------------------------------- |
| **主控芯片**    | STM32F103C8T6（中等容量，Cortex-M3）          |
| **系统时钟**    | 72MHz（HSE 8MHz × 9，PLL 倍频）             |
| **Flash**   | 64KB                                   |
| **显示屏**     | 0.96 寸 OLED，分辨率 128×64（SSD1306 驱动）     |
| **通信接口**    | 软件 I2C（SCL=PB8, SDA=PB9, 水平寻址模式单次写入优化） |
| **串口（流模式）** | USART1（TX=PA9, RX=PA10）+ DMA           |
| **开发环境**    | Keil MDK（ARM Compiler V5）              |

***

## 项目结构

```
4pin/
├── User/
│   ├── main.c                  # 主程序（视频流循环）
│   ├── stm32f10x_conf.h        # 标准外设库配置
│   ├── stm32f10x_it.c/h        # 中断服务（空闲，USART ISR 在 video_stream.c）
├── Hardware/
│   ├── OLED.c/h                # OLED SSD1306 驱动（软件 I2C + 水平寻址优化）
│   ├── OLED_Data.c/h           # ASCII/汉字字模、图像数据
│   ├── video_frames.c/h        # Flash 模式帧数据（可选，由脚本生成）
│   ├── video_player.c/h        # Flash 模式播放器（可选）
│   ├── video_stream.c/h        # ★ 串口流模式：UART DMA 接收 + 显示
├── System/
│   └── Delay.c/h               # SysTick 微秒/毫秒/秒延时
├── Library/                    # STM32F10x 标准外设库 V3.5.0
├── Start/                      # 启动文件 + CMSIS 核心 + 系统时钟
├── video/
│   ├── 3724723-1-208.mp4       # 示例视频
│   ├── convert_video.py        # 视频 → Flash C 数组 转换工具
│   └── pc_streamer.py          # ★ PC 串口流式发送器
├── Project.uvprojx             # Keil 项目文件
└── keilkill.bat                # 清理编译中间文件
```

***

## 模式一：串口流模式（推荐，无时长限制）

### 原理

```
┌──────────┐    USB转串口      ┌──────────────────────┐    I2C     ┌──────┐
│   PC     │─── TX ───────────→│ PA10 (USART1_RX)     │           │      │
│ Python   │                   │   ↓ DMA1_Channel5    │── SCL ───→│ OLED │
│ 脚本     │←── RX ────────────│ PA9  (USART1_TX)     │── SDA ───→│128x64│
│          │    0xAC (应答)    │   发送ACK(0xAC)      │           │      │
└──────────┘                   └──────────────────────┘           └──────┘

协议：PC 发送 [0xAA] [0x55] [1024字节帧数据] → STM32 应答 [0xAC] → 下一帧
```

### 带宽与帧率分析

| 环节                 | 每帧耗时          | 说明                                      |
| ------------------ | ------------- | --------------------------------------- |
| 串口传输帧数据 (1026字节)   | \~11ms        | 921600bps                               |
| 软件 I2C 刷新 (1024字节) | \~25ms        | 水平寻址模式单次 WriteData，省去 7 次 Start/Stop 开销 |
| **流水线并行后有效帧时间**    | **\~25ms**    | I2C 时长 ≥ 串口时长，串口传输被 I2C 覆盖              |
| **理论极限**           | **\~40fps**   | I2C 刷新速度上限                              |
| **实测预估**           | **33\~38fps** | 中断响应 + ACK 往返微调                         |

> 两层优化：(1) 水平寻址模式→每帧仅 1 次 I2C 传输（原 8 次），节省 \~4ms；(2) ISR 中条件发送 ACK→PC 立即传下一帧，串口 DMA 接收与 I2C 刷新并行。

### 硬件连接

| USB 转串口模块 | STM32F103C8T6 引脚  | 说明             |
| --------- | ----------------- | -------------- |
| TX        | PA10 (USART1\_RX) | PC 发 → STM32 收 |
| RX        | PA9 (USART1\_TX)  | STM32 发 → PC 收 |
| GND       | GND               | 共地             |

> ⚡ **共用 ST-Link 的虚拟串口**：很多 ST-Link V2 调试器自带虚拟串口，可直接用杜邦线将 ST-Link 的 TX/RX/GND 连接到 STM32 的 PA10/PA9/GND，无需额外买 USB 转 TTL 模块。

### 使用方法

**第一步：编译烧录 STM32 固件**

1. Keil MDK 打开 `Project.uvprojx`
2. 将 `Hardware/video_stream.c` 添加到项目 Source Group
3. 确保 Include Paths 包含 `Hardware\`
4. Build (F7) → Download (F8) 烧录

**第二步：连接串口线**

如上表连接 USB 转串口模块到 STM32。

**第三步：在 PC 上配置并运行流式发送器**

编辑 `video/pc_streamer.py` 顶部的可配置参数：

```python
VIDEO_PATH = "你的视频.mp4"   # 视频文件
SERIAL_PORT = "COM3"          # ★ 改成你电脑上的实际端口号
SERIAL_BAUDRATE = 2000000     # 需与 STM32 端一致
FPS = 30                      # 目标帧率
START_SEC = 0                 # 起始位置（秒）
DURATION_SEC = 0              # 时长限制（0=完整视频）
```

运行：

```bash
cd video
pip install pyserial Pillow     # 首次需安装依赖
python pc_streamer.py
```

### 如何查看串口号

- **Windows**：设备管理器 → 端口(COM和LPT) → 查找 `USB-SERIAL CH340` 或 `STLink Virtual COM Port`
- **Linux**：`ls /dev/ttyUSB*` 或 `ls /dev/ttyACM*`

***

## 模式二：Flash 存储模式（独立运行，无需 PC）

适用于无需 PC 的独立短视频播放（logo 动画、开机画面等）。视频帧数据直接烧录进 STM32 的 Flash，上电后自动播放，完全脱离 PC 独立运行。

### 视频参数

| 项目   | 值                      |
| ---- | ---------------------- |
| 帧数   | ≤60 帧（受 64KB Flash 限制） |
| 帧率   | 10fps                  |
| 时长   | ≤6 秒                   |
| 数据大小 | ≤60KB                  |

> Flash 空间说明：STM32F103C8T6 共有 64KB Flash。代码部分（启动 + OLED 初始化 + I2C + 视频播放）约占用 3KB，其余 ~61KB 可用于存储帧数据。每帧 1KB，因此最多约 60 帧。超出 60 帧会导致编译时链接器报 overflow 错误。

### 完整操作步骤

#### 第 1 步：生成视频帧 C 数组文件

在 PC 上打开命令行，进入项目的 `video/` 目录：

```bash
cd d:\Keil_Project\4pin\video
```

编辑 `convert_video.py`，修改顶部的配置参数（可选，有默认值）：

```python
VIDEO_PATH = "3724723-1-208.mp4"   # 你的视频文件名
MAX_FRAMES = 45                     # 最大帧数（建议 ≤60，视视频时长和Flash空间而定）
FPS = 10                            # 播放帧率（10 帧/秒）
START_SEC = 0                       # 从视频的第几秒开始截取
```

保存后运行：

```bash
python convert_video.py
```

运行成功后，会在 `video/` 目录下生成两个文件：

- `video_frames.h` — 帧参数宏定义与数组声明
- `video_frames.c`  — 帧数据数组（所有帧的二进制像素数据）

终端会输出类似以下信息：

```
提取视频片段: 起始 0s, 时长 4.5s, 10fps, 最多 45 帧
生成 45 个帧文件
预处理帧数据...
  预处理: [========================================] 100.0%  (45/45)

完成! 生成 45 帧, 共 46080 字节 (45KB)
Flash 占用预估: 代码约 3KB + 帧数据 45KB = 约 48KB / 64KB
输出文件: video_frames.h, video_frames.c
```

#### 第 2 步：将帧数据文件复制到项目

将刚刚生成的两个文件复制到 `Hardware/` 目录，覆盖旧文件（如果存在）：

```bash
copy video\video_frames.h Hardware\
copy video\video_frames.c Hardware\
```

或者直接手动拖拽覆盖。

#### 第 3 步：修改 main.c 切换到 Flash 模式

将 `User/main.c` 的内容替换为以下代码（使用 `VideoPlayer_PlayOnce()` 而非 `VideoStream_Process()`）：

```c
#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "video_player.h"

int main(void)
{
    OLED_Init();

    while (1)
    {
        /* 播放一次预存在 Flash 中的视频（约 4.5 秒 @45帧10fps） */
        VideoPlayer_PlayOnce();

        /* 播放完毕后清屏等待 2 秒，然后循环播放 */
        OLED_Clear();
        OLED_Update();
        Delay_ms(2000);
    }
}
```

也可以使用循环播放：

```c
#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "video_player.h"

int main(void)
{
    OLED_Init();

    /* 无限循环播放视频（永不停机） */
    VideoPlayer_PlayLoop();
}
```

> ⚠️ **注意**：`main.c` 中只能包含一种模式——要么用 `video_stream.h`（串口流模式），要么用 `video_player.h`（Flash 模式）。切换模式时必须修改 `main.c` 的 `#include` 和主循环代码，不能同时使用。

#### 第 4 步：Keil MDK 中调整项目文件

1. 用 Keil MDK 打开 `Project.uvprojx`
2. 在左侧 Project 面板中，将 `Hardware/video_stream.c` **移除**（右键 → Remove）
3. 将 `Hardware/video_player.c` 和 `Hardware/video_frames.c` **添加**进来（右键 Source Group → Add Existing Files to Group）
4. 确认 Include Paths 中包含 `Hardware\` 目录（Project → Options for Target → C/C++ → Include Paths）

> 最终项目中的文件列表应为：
> - `Hardware/OLED.c` ✓
> - `Hardware/OLED_Data.c` ✓
> - `Hardware/video_player.c` ✓（新增）
> - `Hardware/video_frames.c` ✓（新增）
> - `System/Delay.c` ✓
> - `User/main.c` ✓
> - ...其他 ST 标准库文件不变

#### 第 5 步：编译与烧录

1. 点击 **Build (F7)** 编译项目
2. 确认输出窗口显示 `0 Error(s), 0 Warning(s)`
3. 使用 ST-Link 调试器连接 STM32
4. 点击 **Download (F8)** 烧录到目标芯片

#### 第 6 步：上电运行

- 断开 ST-Link 和 USB 线，重新上电
- STM32 会自动播放预存在 Flash 中的视频
- 无需连接 PC、USB 转串口或任何其他设备

### 与串口流模式互相切换

Flash 模式和串口流模式不能同时存在，需要手动切换。步骤对照：

| 步骤 | 切换到 Flash 模式 | 切换回串口流模式 |
|------|-------------------|------------------|
| 1 | `main.c` 改为 `#include "video_player.h"` | `main.c` 改为 `#include "video_stream.h"` |
| 2 | Keil 项目中添加 `video_player.c` + `video_frames.c` | Keil 项目中添加 `video_stream.c` |
| 3 | Keil 项目中移除 `video_stream.c` | Keil 项目中移除 `video_player.c` + `video_frames.c` |
| 4 | Build + Download | Build + Download |

### 换视频的方法

1. 修改 `convert_video.py` 中的 `VIDEO_PATH` 和 `MAX_FRAMES`（如需）
2. 重新运行 `python convert_video.py`
3. 将新生成的 `video_frames.h/c` 复制到 `Hardware/` 覆盖
4. 在 Keil 中重新 Build + Download

### Flash 模式测试视频

如果只是想快速测试 Flash 模式能否正常工作，可以使用项目中自带的示例视频快速生成帧数据：

```bash
cd d:\Keil_Project\4pin\video
python convert_video.py    # 直接使用默认参数，无需修改
```

该命令会从 `3724723-1-208.mp4` 的开头提取 45 帧（4.5 秒），生成帧数据文件。之后按上述步骤操作即可。

***

## 视频转换工具

两个 Python 脚本都依赖：

- Python 3.x
- Pillow：`pip install Pillow`
- FFmpeg（系统安装，需在 PATH 中）

| 脚本                 | 用途              | 输出                  |
| ------------------ | --------------- | ------------------- |
| `convert_video.py` | 视频 → Flash C 数组 | `video_frames.h/.c` |
| `pc_streamer.py`   | 视频 → 串口实时流式发送   | 直接串口输出              |

两个脚本共同的视频处理流程：

1. FFmpeg 按 FPS 提取帧 → 缩放为 128×64 并居中填充黑边
2. Pillow 转为 1-bit 黑白二值图（阈值 128）
3. 按 **OLED 显存格式**重组为 1024 字节/帧

***

## 核心技术要点

### 串口流 DMA 双缓冲机制

```c
// 中断（USART1_IRQHandler）：DMA 接收完成 → 拷贝到显存 → 置标志位
if (received >= 1026 && rx_buf[0]==0xAA && rx_buf[1]==0x55) {
    memcpy(OLED_DisplayBuf, &rx_buf[2], 1024);
    frame_ready = 1;  // 通知主循环
}
// 主循环（VideoStream_Process）：显示 + ACK
if (frame_ready) {
    frame_ready = 0;
    OLED_Update();         // I2C 刷新
    USART_SendData(0xAC);  // 应答 PC → 触发下一帧
}
```

这种"中断接收 + 主循环显示"的分离设计，使得串口接收和 I2C 显示可以流水线并行，互不阻塞。

### OLED 显存布局（帧数据格式）

```
纵向 8 像素 = 1 字节（高位在下 Bit7）
遍历顺序：先列(0→127) 后页(0→7)

     0 ────── X轴(128列) ────── 127
    ┌─────────────────────────────┐
  0 │ Page0: B0 B0 B0 ...        │
    │ Page1: B1 B1 B1 ...        │
    │ ...                         │
 63 │ Page7: B7 B7 B7 ...        │
    └─────────────────────────────┘

每个 B 是一个字节，Bit0=最上方像素，Bit7=最下方像素
```

***

## 编译与烧录

### 环境要求

- Keil MDK-ARM V5+，ARM Compiler V5
- STM32F10x 标准外设库 (V3.5.0，项目已包含)

### 编译步骤

1. Keil MDK 打开 `Project.uvprojx`
2. 将 `video_stream.c`（或 `video_player.c` + `video_frames.c`）添加到项目
3. Build (F7) → 确保 0 Error
4. Download (F8) 烧录

### 清理

运行 `keilkill.bat` 清理编译中间文件。

***

## 硬件连接总图

```
┌──────────────────────────────────────────────────────┐
│                    STM32F103C8T6                     │
│                                                      │
│  PB8 ── SCL ──→ OLED                                 │
│  PB9 ── SDA ──→ OLED                                 │
│                                                      │
│  PA9  (TX) ──→ USB转TTL RX ──→ PC (串口流模式)         │
│  PA10 (RX) ←── USB转TTL TX ←── PC                    │
│                                                      │
│  GND ←── OLED GND, USB转TTL GND, ST-Link GND         │
│  3.3V → OLED VCC                                     │
└──────────────────────────────────────────────────────┘
```

***

## 致谢与版权

- **OLED 驱动程序**：[江协科技](https://jiangxiekeji.com) V2.0（2024.10.20），4 针 I2C 接口，免费开源
- **标准外设库**：STMicroelectronics STM32F10x Standard Peripheral Library V3.5.0
- **CMSIS 核心**：ARM CMSIS Cortex-M3

***

## 许可

本项目中的 OLED 驱动部分由江协科技创建并免费开源共享，可任意查看、使用和修改并应用到自己的项目中；ST 标准外设库及相关 CMSIS 文件版权归 STMicroelectronics 及 ARM 所有。
