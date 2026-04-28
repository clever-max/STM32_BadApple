/**
 * audio_pwm.c - 非阻塞蜂鸣器驱动（占空比音量版）
 *
 * 架构：
 *   TIM3 ISR → PWM 分段翻转 PA0 → 占空比调节音量
 *     TIM3 预分频 71 → 定时器时钟 1MHz
 *     low_ticks  = 1MHz / freq * volume / 100     (PA0 LOW = 蜂鸣器响)
 *     high_ticks = 1MHz / freq * (100-volume) / 100 (PA0 HIGH = 静音)
 *   TIM4 → 1ms 计时器 → AudioPWM_Update() 检查音符到期
 *   主循环 → AudioPWM_Update() 交替执行
 */

#include "audio_pwm.h"
#include "stm32f10x.h"
#include "stm32f10x_tim.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

static const MusicNote *score_ptr    = 0;
static uint16_t         score_count  = 0;
static uint16_t         note_index   = 0;
static uint8_t          playing      = 0;
static volatile uint32_t ms_tick     = 0;
static uint32_t         note_start   = 0;

/* 占空比音量控制 */
static uint8_t  note_volume   = 50;   /* 0~100, 默认 50% */
static uint16_t note_arr_high = 0;    /* PA0 HIGH 阶段 ARR (蜂鸣器关) */
static uint16_t note_arr_low  = 0;    /* PA0 LOW  阶段 ARR (蜂鸣器开) */
static uint8_t  note_phase    = 1;    /* 1=HIGH(关), 0=LOW(开) */


void TIM4_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
        ms_tick++;
    }
}


void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);

        if (note_phase)
        {
            GPIO_ResetBits(GPIOA, GPIO_Pin_0);   /* PA0=LOW → 蜂鸣器响 */
            TIM3->ARR = note_arr_low;
            note_phase = 0;
        }
        else
        {
            GPIO_SetBits(GPIOA, GPIO_Pin_0);     /* PA0=HIGH → 蜂鸣器静 */
            TIM3->ARR = note_arr_high;
            note_phase = 1;
        }
        TIM3->CNT = 0;
    }
}


static void TIM4_Config(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef        NVIC_InitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);

    TIM_TimeBaseStructure.TIM_Period        = 1000 - 1;
    TIM_TimeBaseStructure.TIM_Prescaler     = 72 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);

    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = TIM4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM4, ENABLE);
}


void AudioPWM_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_SetBits(GPIOA, GPIO_Pin_0);   /* 低电平触发：HIGH=静音 */

    /* TIM3: 预分频 71 → 1MHz 定时器时钟 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    TIM_TimeBaseStructure.TIM_Period        = 65535;
    TIM_TimeBaseStructure.TIM_Prescaler     = 72 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseStructure);
    TIM_Cmd(TIM3, DISABLE);                /* TIM_TimeBaseInit 自动开了，立刻关掉 */
    TIM_ClearITPendingBit(TIM3, TIM_IT_Update); /* 清掉可能已有的溢出标志 */
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM4_Config();
}


void AudioPWM_StartNote(uint16_t freq_hz)
{
    if (freq_hz < 20)
    {
        AudioPWM_StopNote();
        return;
    }

    uint32_t full_period = 1000000UL / freq_hz;              /* 整个周期的 tick 数 */
    uint32_t low_t  = full_period * note_volume / 100;       /* LOW 占比 */
    uint32_t high_t = full_period - low_t;                   /* HIGH 占比 */

    if (low_t  < 2) low_t  = 2;
    if (high_t < 2) high_t = 2;

    note_arr_low  = (uint16_t)(low_t  - 1);
    note_arr_high = (uint16_t)(high_t - 1);

    TIM_Cmd(TIM3, DISABLE);
    TIM3->ARR = note_arr_high;            /* 先从 HIGH (静音) 开始 */
    note_phase = 1;
    TIM3->CNT = 0;
    TIM_Cmd(TIM3, ENABLE);
}


void AudioPWM_StopNote(void)
{
    TIM_Cmd(TIM3, DISABLE);
    GPIO_SetBits(GPIOA, GPIO_Pin_0);    /* 低电平触发：HIGH=静音 */
}

void AudioPWM_SetVolume(uint8_t vol)
{
    if (vol > 100) vol = 100;
    note_volume = vol;
}

uint8_t AudioPWM_GetVolume(void)
{
    return note_volume;
}


uint8_t AudioPWM_PlayScore(const MusicNote *score, uint16_t count)
{
    score_ptr   = score;
    score_count = count;
    note_index  = 0;
    playing     = 1;

    AudioPWM_StartNote(score[0].freq);
    note_start = ms_tick;
    return 1;
}


uint8_t AudioPWM_IsPlaying(void)
{
    return playing;
}


void AudioPWM_Update(void)
{
    if (!playing) return;

    if ((ms_tick - note_start) >= score_ptr[note_index].dur)
    {
        note_index++;
        if (note_index >= score_count)
        {
            AudioPWM_StopNote();
            playing = 0;
            return;
        }

        AudioPWM_StartNote(score_ptr[note_index].freq);
        note_start = ms_tick;
    }
}
