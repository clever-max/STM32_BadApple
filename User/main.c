/**
 * main.c - STM32 OLED 视频流 + 三蜂鸣器真和弦音频播放
 * =======================================================
 *
 * 硬件:
 *   PA0  → 蜂鸣器1 (高声部, TIM3)
 *   PA1  → 蜂鸣器2 (中声部, TIM2)
 *   PA2  → 模式切换按钮 (低→循环 VOX_1→VOX_2→VOX_3)
 *   PA3  → 蜂鸣器3 (低声部, TIM1)
 *   PA9  → USART1 TX (ACK)
 *   PA10 → USART1 RX (DMA 视频流)
 *
 * 声道模式: VOX_1=仅PA0, VOX_2=PA0+PA1, VOX_3=三声道全开
 */

#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "Delay.h"
#include "OLED.h"
#include "video_stream.h"
#include "audio_pwm.h"
#include "music_score.h"

#define DEFAULT_VOICE_MODE  VOX_3   /* VOX_1 / VOX_2 / VOX_3 */

#define BTN_PORT    GPIOA
#define BTN_PIN     GPIO_Pin_2
#define BTN_DEBOUNCE_MS  50

/* ── SysTick 1ms ── */
static volatile uint32_t g_ms = 0;
void SysTick_Handler(void) { g_ms++; }
static uint32_t ms(void) { return g_ms; }
static void SysTick_Init(void) { SysTick_Config(SystemCoreClock / 1000); }

/* ── 全局 ── */
static VoiceMode  g_vox = DEFAULT_VOICE_MODE;
static volatile uint8_t g_audio_done = 0;


/* ── 按钮: VOX_1 → VOX_2 → VOX_3 → VOX_1 ... ── */
static void BTN_Init(void)
{
    GPIO_InitTypeDef gis;
    gis.GPIO_Pin  = BTN_PIN;
    gis.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(BTN_PORT, &gis);
}

static void BTN_Check(void)
{
    static uint32_t last = 0;
    static uint8_t  prev = 1;
    if ((ms() - last) < BTN_DEBOUNCE_MS) return;
    last = ms();
    uint8_t cur = (GPIO_ReadInputDataBit(BTN_PORT, BTN_PIN) == Bit_RESET) ? 0 : 1;
    if (cur == 0 && prev == 1)
    {
        g_vox = (g_vox >= VOX_3) ? VOX_1 : (VoiceMode)(g_vox + 1);
        AudioPWM_SetVoiceMode(g_vox);
    }
    prev = cur;
}


/* ── OLED ── */
static const char *vox_name(VoiceMode m)
{
    switch (m) {
    case VOX_1: return "1CH";
    case VOX_2: return "2CH";
    case VOX_3: return "3CH";
    default:    return "???";
    }
}

static void ShowBoot(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0,  "Video + 3-Voice", OLED_8X16);
    OLED_ShowString(0, 18, "Vox:", OLED_6X8);
    OLED_ShowString(24, 18, (char *)vox_name(g_vox), OLED_6X8);
    OLED_ShowString(0, 30, "921600 bps", OLED_6X8);
    OLED_ShowString(0, 42, "Waiting PC...", OLED_6X8);
    OLED_Update();
}


/* ── main ── */
int main(void)
{
    SysTick_Init();
    OLED_Init();
    ShowBoot();

    AudioPWM_Init();
    AudioPWM_SetVolume(50);
    AudioPWM_SetVoiceMode(g_vox);

    BTN_Init();
    Delay_ms(1500);
    VideoStream_Init();

    while (1)
    {
        VideoStream_Process();
        AudioPWM_Update();
        BTN_Check();

        if (!AudioPWM_IsPlaying() && !g_audio_done)
            g_audio_done = 1;
    }
}
