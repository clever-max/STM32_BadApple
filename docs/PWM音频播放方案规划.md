# PWM 音频播放方案 — 详细实施规划

> **⚠️ 注：本方案为最初的技术规划文档。实际实现采用了不同的路线——三蜂鸣器位翻转真和弦（见 `README.md` 和 `docs/README.md`），比本方案的 PWM DAC 更简单且支持多声道和弦。本方案若需要 8-bit 音质的单声道输出仍可实施。**
>
> **目标**：在现有 OLED 视频流播放项目中增加同步音频输出
> **方案A（本方案）**：PWM + 无源蜂鸣器/小喇叭，PA8(TIM1\_CH1) 输出，RC 低通 + 三极管放大
> **方案B（实际实施）**：三蜂鸣器位翻转 (TIM3→PA0, TIM2→PA1, TIM1→PA3)，无滤波电路

***

## 一、硬件改动

### 1.1 新增元器件

| 元件            | 规格           | 数量 | 用途           |
| ------------- | ------------ | -- | ------------ |
| 无源蜂鸣器或 8Ω 小喇叭 | 0.5W\~1W     | 1  | 发声           |
| S8050 NPN 三极管 | 或 2N2222     | 1  | 驱动放大         |
| 电阻            | 1kΩ \~ 4.7kΩ | 2  | RC 低通 + 基极限流 |
| 电容            | 100nF (104)  | 1  | RC 低通        |
| 杜邦线           | —            | 若干 | 连接           |

### 1.2 电路连接

```
STM32 PA8 ───┬── 1kΩ ──┬── 100nF ── GND    ← RC 低通滤波器
              │         │
              │         └── 1kΩ ── S8050(B)  ← 基极限流
              │
              └─────────────────────────────────   (TIM1_CH1 PWM 输出)

S8050(C) ──── 小喇叭(+) ──── 3.3V
S8050(E) ──── GND
小喇叭(-) ─── GND
```

### 1.3 电路原理

* **TIM1\_CH1 (PA8)**：输出 28.125kHz PWM 载波，占空比编码音频幅度

* **RC 低通 (1kΩ + 100nF)**：截止频率 ≈ 1.6kHz，滤除 PWM 高频载波，保留音频基带

* **S8050 三极管**：电流放大，驱动 8Ω 喇叭负载

> ⚠️ **不接 RC 滤波直接用蜂鸣器**：声音会是 28kHz 尖锐载波 + 音频包络，效果类似老式 AM 收音机。接 RC 滤波后音质接近 8-bit WAV。

***

## 二、协议设计

### 2.1 修改后的数据包格式

```
旧格式（仅视频）:
  [0xAA] [0x55] [1024 字节视频帧]

新格式（视频 + 音频）:
  [0xAA] [0x55] [N 字节音频] [1024 字节视频帧] [0xFF]

  N = 1000 / VIDEO_PLAY_FPS × AUDIO_SAMPLE_RATE / 1000
     = 1000 / 30 × 8000 / 1000 ≈ 266 字节（@30fps, 8kHz）
```

| 字段            | 字节数      | 说明                   |
| ------------- | -------- | -------------------- |
| SYNC0         | 1        | 0xAA                 |
| SYNC1         | 1        | 0x55                 |
| Audio Payload | N (≈266) | 8kHz, 8-bit PCM, 无符号 |
| Video Payload | 1024     | OLED 帧数据             |
| Audio Trailer | 1        | 0xFF，音频结束标志（可选校验用）   |

总包大小：2 + 266 + 1024 + 1 = **1293 字节**

> 串口传输时间 @ 921600bps：1293 × 10 / 921600 ≈ **14.0ms**（含音频比原 11.1ms 多 \~3ms）

### 2.2 音频采样参数

| 参数    | 值           | 说明                         |
| ----- | ----------- | -------------------------- |
| 采样率   | **8000 Hz** | 电话音质，每 100ms 间隔约 800 样本    |
| 位深    | **8 bit**   | 与 PWM 分辨率一致                |
| 编码    | 无符号 PCM     | 0x80 = 静音，0x00/0xFF = 最大振幅 |
| 每帧采样数 | **266**     | @30fps；@60fps → 133        |

### 2.3 定时器配置

```
TIM1 时钟: 72MHz (APB2)
TIM1_PSC: 0       (不分频)
TIM1_ARR: 255     (8-bit PWM 分辨率)
PWM 频率: 72M / 256 = 281.25 kHz → 远超音频听觉上限，RC 滤波后干净
有效音频更新率: 每帧更新一次 → 30次/秒 → 可表现 8kHz 采样（266 样本/帧）
```

***

## 三、STM32 固件改动

### 3.1 新增文件

| 文件            | 路径                     | 说明                        |
| ------------- | ---------------------- | ------------------------- |
| `audio_pwm.h` | `Hardware/audio_pwm.h` | PWM 音频输出模块头文件             |
| `audio_pwm.c` | `Hardware/audio_pwm.c` | TIM1 PWM 初始化 + 缓冲区 + 定时中断 |

### 3.2 audio\_pwm.h

```c
#ifndef __AUDIO_PWM_H
#define __AUDIO_PWM_H

#include <stdint.h>

/* 音频参数 */
#define AUDIO_SAMPLE_RATE   8000    /* 采样率 (Hz) */
#define AUDIO_BUF_SIZE      512     /* 环形缓冲区，2 倍于每帧数据量 */
#define AUDIO_PWM_TIM       TIM1
#define AUDIO_PWM_CHANNEL   TIM1_CCR1  /* PA8 */

#define AUDIO_SYNC_END      0xFF    /* 音频结束标志 */

/**
 * @brief 初始化 PWM 音频输出
 * @note  配置 TIM1_CH1 (PA8) 为 281.25kHz PWM
 *        配置 TIM2 定时中断以 8kHz 速率从环形缓冲区取数据更新 PWM
 */
void AudioPWM_Init(void);

/**
 * @brief 向环形缓冲区写入音频数据
 * @param  data  音频数据起始地址
 * @param  len   数据长度（字节）
 * @note  ISR 中调用，数据来自串口 DMA 接收
 */
void AudioPWM_Write(const uint8_t *data, uint16_t len);

#endif
```

### 3.3 audio\_pwm.c 核心逻辑

```c
#include "audio_pwm.h"
#include "stm32f10x.h"
#include "stm32f10x_tim.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

/* 环形缓冲区 */
static uint8_t audio_buf[AUDIO_BUF_SIZE];
static volatile uint16_t write_idx = 0;
static volatile uint16_t read_idx  = 0;
static volatile uint16_t buf_count = 0;

/**
 * TIM2 中断 (8kHz): 从环形缓冲区取 1 字节 → 写入 TIM1 CCR1 → PWM 输出
 */
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);

        if (buf_count > 0)
        {
            TIM1->CCR1 = audio_buf[read_idx];   /* 更新 PWM 占空比 */
            read_idx = (read_idx + 1) % AUDIO_BUF_SIZE;
            buf_count--;
        }
    }
}

/**
 * ISR 中调用：写入音频数据到环形缓冲区
 */
void AudioPWM_Write(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        if (buf_count < AUDIO_BUF_SIZE)
        {
            audio_buf[write_idx] = data[i];
            write_idx = (write_idx + 1) % AUDIO_BUF_SIZE;
            buf_count++;
        }
        else
        {
            break;  /* 缓冲区满，丢弃剩余（防止溢出）*/
        }
    }
}

void AudioPWM_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_OCInitTypeDef  TIM_OCInitStructure;

    /* ---- PA8: TIM1_CH1 复用推挽输出 ---- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_TIM1, ENABLE);
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_8;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* ---- TIM1: 281.25kHz PWM, 8-bit 分辨率 ---- */
    TIM_TimeBaseStructure.TIM_Period        = 255;       /* ARR */
    TIM_TimeBaseStructure.TIM_Prescaler     = 0;         /* PSC */
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM1, &TIM_TimeBaseStructure);

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = 128;           /* 初始静音 */
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC1Init(TIM1, &TIM_OCInitStructure);

    TIM_CtrlPWMOutputs(TIM1, ENABLE);   /* TIM1 主输出使能 */
    TIM_Cmd(TIM1, ENABLE);

    /* ---- TIM2: 8kHz 中断，用于更新 PWM 占空比 ---- */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    TIM_TimeBaseStructure.TIM_Period    = 9000 - 1;      /* 72MHz / 9000 = 8000Hz */
    TIM_TimeBaseStructure.TIM_Prescaler = 0;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

    /* NVIC */
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel                   = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM2, ENABLE);
}
```

### 3.4 video\_stream.h 改动

```c
#define STREAM_AUDIO_PER_FRAME   266   /* 每帧音频字节数 (@30fps, 8kHz) */
#define STREAM_PKT_SIZE          (2 + STREAM_AUDIO_PER_FRAME + STREAM_FRAME_SIZE + 1)
                                      /* SYNC0+SYNC1 + Audio + Video + Trailer */
```

### 3.5 video\_stream.c ISR 改动

```c
void USART1_IRQHandler(void)
{
    // ... DMA 暂停、获取 received ...
    if (received >= STREAM_PKT_SIZE)
    {
        if (rx_buf[0] == STREAM_SYNC0 && rx_buf[1] == STREAM_SYNC1)
        {
            /* 提取音频数据（紧接帧头之后） */
            uint8_t *audio_data = &rx_buf[2];
            AudioPWM_Write(audio_data, STREAM_AUDIO_PER_FRAME);

            /* 提取视频数据（音频之后） */
            uint8_t *video_data = &rx_buf[2 + STREAM_AUDIO_PER_FRAME];

            if (!frame_ready)
            {
                memcpy(temp_frame, video_data, STREAM_FRAME_SIZE);
                frame_ready = 1;
            }

            if (USART_GetFlagStatus(USART1, USART_FLAG_TXE) != RESET)
                USART_SendData(USART1, STREAM_ACK);
        }
    }
    // ... 重启 DMA ...
}
```

### 3.6 main.c 改动

```c
#include "audio_pwm.h"

int main(void)
{
    OLED_Init();
    AudioPWM_Init();      /* ← 新增 */
    VideoStream_Init();

    while (1)
    {
        VideoStream_Process();
    }
}
```

### 3.7 Keil 项目改动

* 添加 `Hardware/audio_pwm.c` 到 Source Group

* Include Paths 确认包含 `Hardware\`

### 3.8 中断优先级规划

| 中断                | 抢占优先级 | 子优先级 | 原因                     |
| ----------------- | :---: | :--: | ---------------------- |
| TIM2 (音频更新, 8kHz) | **2** |   0  | 实时音频，比视频更重要但不能阻塞 USART |
| USART1 (帧接收)      | **1** |   0  | 必须高于 TIM2，否则 DMA 溢出    |
| SysTick (Delay)   |   —   |   —  | 默认最低                   |

***

## 四、PC 端 Python 脚本改动

### 4.1 新增音频提取步骤

在 `pc_streamer.py` 的 `extract_and_stream()` 中，视频帧提取之后、串口发送之前增加：

```python
import subprocess

def extract_audio(video_path, start_sec, duration_sec, fps, output_wav):
    """使用 FFmpeg 提取音频为原始 PCM"""
    sample_rate = 8000
    total_samples = int(duration_sec * sample_rate)

    subprocess.run([
        "ffmpeg", "-y", "-loglevel", "error",
        "-ss", str(start_sec),
        "-i", video_path,
        "-t", str(duration_sec),
        "-f", "u8",           # 8-bit 无符号 PCM
        "-ac", "1",           # 单声道
        "-ar", str(sample_rate),
        output_wav
    ], check=True)

    with open(output_wav, "rb") as f:
        raw_pcm = f.read()[:total_samples]  # 8kHz, 8-bit = N 字节

    return raw_pcm, sample_rate
```

### 4.2 构建含音频的数据包

```python
samples_per_frame = sample_rate // fps   # 8000 / 30 = 266

for i, packet in enumerate(frame_cache):
    # 提取该帧对应的音频片段
    audio_start = i * samples_per_frame
    audio_end   = audio_start + samples_per_frame
    audio_chunk = raw_pcm[audio_start:audio_end]

    # 补齐不足一帧的尾部
    if len(audio_chunk) < samples_per_frame:
        audio_chunk += bytes([0x80]) * (samples_per_frame - len(audio_chunk))

    # 构建完整数据包
    full_packet = bytes([0xAA, 0x55]) + audio_chunk + packet[2:] + bytes([0xFF])
    ser.write(full_packet)
    ser.flush()
    # ... 等待 ACK ...
```

### 4.3 视频帧缓存改为原始帧格式

当前 `frame_cache` 中存储的是 `[SYNC0, SYNC1, 1024字节视频]`，音频方案需改为只存储视频数据部分：

```python
# 预处理阶段：frame_cache 直接存 1024 字节纯视频帧
video_data = process_frame(img)
frame_cache.append(video_data)  # 1024 字节，不带帧头
```

### 4.4 `pc_streamer.py` 新增配置项

```python
# 音频参数（与 STM32 端 AUDIO_SAMPLE_RATE 保持一致）
AUDIO_SAMPLE_RATE = 8000        # 音频采样率
AUDIO_ENABLE      = True        # 是否启用音频（False = 仅视频模式）
```

***

## 五、音画同步

### 5.1 同步原理

```
每帧数据包 = 音频(266字节) + 视频(1024字节)

发送流程:
  PC → [音频帧N, 视频帧N] → STM32
       ↓
  STM32 ISR: 音频写入环形缓冲 → TIM2 8kHz 中断逐字节播放
             视频写入 temp_frame → 主循环 I2C 显示
```

音频和视频绑定在同一数据包中，物理上不可分离——音画同步是**天然的**。

### 5.2 缓冲区水位

* 环形缓冲区 512 字节，每帧写入 266 字节

* TIM2 以 8000Hz 速率消耗（每 100ms = 800 字节

* 每 33ms（30fps）写入 266 字节 → 写入速率 = 266/(33ms) ≈ 8061 字节/秒 > 消耗速率 8000 字节/秒

* ✅ 缓冲区不会欠载

### 5.3 潜在问题与缓解

| 问题    | 现象      | 缓解措施                                 |
| ----- | ------- | ------------------------------------ |
| 缓冲区溢出 | 音频卡顿    | ISR 满时丢弃新数据                          |
| 缓冲区欠载 | 音频间隙/爆音 | 欠载时播放 0x80（静音）                       |
| 第一帧延迟 | 开头无声音   | `pc_streamer.py` 启动时发送 512 字节静音填充缓冲区 |

***

## 六、实施步骤

### Phase 1：硬件搭建（约 30 分钟）

1. 准备元器件（蜂鸣器/喇叭、S8050、1kΩ×2、100nF 电容）
2. 在面包板上按 1.2 节电路图搭接
3. 杜邦线连接 STM32 PA8 → RC 滤波输入端

### Phase 2：STM32 固件（约 1 小时）

1. 创建 `audio_pwm.h/c`，复制 3.3 节完整代码
2. 修改 `video_stream.h`：更新 `STREAM_PKT_SIZE`
3. 修改 `video_stream.c` ISR：增加音频提取
4. 修改 `main.c`：增加 `AudioPWM_Init()`
5. Keil 项目中添加 `audio_pwm.c`
6. 验证编译 0 Error

### Phase 3：PC 脚本 （约 1 小时）

1. 修改 `pc_streamer.py`：

   * 增加 `extract_audio()` 函数

   * 修改包构建逻辑（含音频数据）

   * 增加 `AUDIO_SAMPLE_RATE` 和 `AUDIO_ENABLE` 配置项
2. 本地测试（仅音频提取，验证 PCM 数据正确）

### Phase 4：联调测试

1. 烧录 STM32 固件
2. 用简单音调（如 440Hz 正弦波）测试 PWM 输出
3. 用真实视频联调，验证音画同步
4. 若缓冲区欠载或溢出，调整 `AUDIO_BUF_SIZE`（增大 → 当前 512 已足够）

***

## 七、风险与回退

| 风险              | 影响       | 回退方式                          |
| --------------- | -------- | ----------------------------- |
| 噪声/杂音           | 音质差      | 调整 RC 参数（增大电容 → 降低截止频率）       |
| TIM2 中断抢占 USART | DMA 溢出   | 调低 TIM2 抢占优先级                 |
| 串口带宽不足          | ACK 超时   | 降低音频采样率 (8kHz → 6kHz) 或降低 FPS |
| PWM 输出接错        | STM32 损坏 | 务必加三极管隔离，不要直接用 IO 驱喇叭         |

> 关闭音频功能：设置 `AUDIO_ENABLE = False`，脚本自动切换回纯视频模式。

***

## 八、预期效果

| 指标    | 预期值                    |
| ----- | ---------------------- |
| 音频采样率 | 8000 Hz                |
| 音频位深  | 8 bit mono             |
| 音质    | 电话级别（清晰可懂）             |
| 音画同步  | ✅ 天然同步（同一数据包）          |
| 帧率影响  | \~-2fps（串口多传 267 字节/帧） |
| 硬件成本  | < 5 元人民币               |

