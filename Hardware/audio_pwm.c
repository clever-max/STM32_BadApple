/**
 * audio_pwm.c - 三蜂鸣器真和弦驱动（占空比音量版）
 *
 * 定时器 → 引脚:
 *   TIM3 → PA0  主旋律/高声部
 *   TIM2 → PA1  第二声部
 *   TIM1 → PA3  第三声部
 *   TIM4 → 1ms 全局计时器
 *
 * 所有定时器时钟: 72MHz / 72(PSC) = 1MHz
 */

#include "audio_pwm.h"
#include "stm32f10x.h"
#include "stm32f10x_tim.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"

/* ── 乐谱指针 ── */
static const void       *score_ptr    = 0;
static uint16_t          score_count  = 0;
static uint16_t          note_index   = 0;
static uint8_t           playing      = 0;
static uint8_t           is_chord     = 0;  /* 0=MusicNote, 1=MusicChord */
static volatile uint32_t ms_tick      = 0;
static uint32_t          note_start   = 0;

/* ── 音量 ── */
static uint8_t  note_volume = 50;

/* ── 声道0: PA0 / TIM3 ── */
static uint16_t v0_arr_high = 0;
static uint16_t v0_arr_low  = 0;
static uint8_t  v0_phase    = 1;
static uint8_t  v0_active   = 0;

/* ── 声道1: PA1 / TIM2 ── */
static uint16_t v1_arr_high = 0;
static uint16_t v1_arr_low  = 0;
static uint8_t  v1_phase    = 1;
static uint8_t  v1_active   = 0;

/* ── 声道2: PA3 / TIM1 ── */
static uint16_t v2_arr_high = 0;
static uint16_t v2_arr_low  = 0;
static uint8_t  v2_phase    = 1;
static uint8_t  v2_active   = 0;

/* ── 兼容旧和声模式 ── */
static HarmonyMode harmony_mode = HARMONY_OFF;
static VoiceMode   voice_mode   = VOX_3;

/* ================================================================
 * TIM4: 1ms 计时器
 * ================================================================ */
void TIM4_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
        ms_tick++;
    }
}

/* ================================================================
 * TIM3: PA0 声道0
 * ================================================================ */
void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
        if (v0_phase) {
            GPIO_ResetBits(GPIOA, GPIO_Pin_0);
            TIM3->ARR = v0_arr_low;
            v0_phase = 0;
        } else {
            GPIO_SetBits(GPIOA, GPIO_Pin_0);
            TIM3->ARR = v0_arr_high;
            v0_phase = 1;
        }
        TIM3->CNT = 0;
    }
}

/* ================================================================
 * TIM2: PA1 声道1
 * ================================================================ */
void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
        if (v1_phase) {
            GPIO_ResetBits(GPIOA, GPIO_Pin_1);
            TIM2->ARR = v1_arr_low;
            v1_phase = 0;
        } else {
            GPIO_SetBits(GPIOA, GPIO_Pin_1);
            TIM2->ARR = v1_arr_high;
            v1_phase = 1;
        }
        TIM2->CNT = 0;
    }
}

/* ================================================================
 * TIM1: PA3 声道2
 * ================================================================ */
void TIM1_UP_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM1, TIM_IT_Update) != RESET)
    {
        TIM_ClearITPendingBit(TIM1, TIM_IT_Update);
        if (v2_phase) {
            GPIO_ResetBits(GPIOA, GPIO_Pin_3);
            TIM1->ARR = v2_arr_low;
            v2_phase = 0;
        } else {
            GPIO_SetBits(GPIOA, GPIO_Pin_3);
            TIM1->ARR = v2_arr_high;
            v2_phase = 1;
        }
        TIM1->CNT = 0;
    }
}

/* ================================================================
 * 定时器初始化
 * ================================================================ */

static void timer_config(TIM_TypeDef *tim, uint32_t rcc_apb, uint32_t rcc_bit,
                         uint8_t irq_ch, uint8_t preempt, uint8_t sub)
{
    TIM_TimeBaseInitTypeDef tbs;
    NVIC_InitTypeDef        nvic;

    if (rcc_apb == 1)
        RCC_APB1PeriphClockCmd(rcc_bit, ENABLE);
    else
        RCC_APB2PeriphClockCmd(rcc_bit, ENABLE);

    tbs.TIM_Period        = 65535;
    tbs.TIM_Prescaler     = 72 - 1;
    tbs.TIM_ClockDivision = TIM_CKD_DIV1;
    tbs.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(tim, &tbs);
    TIM_Cmd(tim, DISABLE);
    TIM_ClearITPendingBit(tim, TIM_IT_Update);
    TIM_ITConfig(tim, TIM_IT_Update, ENABLE);

    nvic.NVIC_IRQChannel                   = irq_ch;
    nvic.NVIC_IRQChannelPreemptionPriority = preempt;
    nvic.NVIC_IRQChannelSubPriority        = sub;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);
}


static void TIM4_Config(void)
{
    TIM_TimeBaseInitTypeDef tbs;
    NVIC_InitTypeDef        nvic;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
    tbs.TIM_Period        = 1000 - 1;
    tbs.TIM_Prescaler     = 72 - 1;
    tbs.TIM_ClockDivision = TIM_CKD_DIV1;
    tbs.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM4, &tbs);
    TIM_ITConfig(TIM4, TIM_IT_Update, ENABLE);

    nvic.NVIC_IRQChannel                   = TIM4_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 3;
    nvic.NVIC_IRQChannelSubPriority        = 0;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);
    TIM_Cmd(TIM4, ENABLE);
}


void AudioPWM_Init(void)
{
    GPIO_InitTypeDef gis;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* PA0 + PA1 + PA3 推挽输出, 初始化 HIGH=静音 */
    gis.GPIO_Pin   = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_3;
    gis.GPIO_Mode  = GPIO_Mode_Out_PP;
    gis.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gis);
    GPIO_SetBits(GPIOA, GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_3);

    /* TIM3: PA0 */
    timer_config(TIM3, 1, RCC_APB1Periph_TIM3, TIM3_IRQn,       2, 0);

    /* TIM2: PA1 */
    timer_config(TIM2, 1, RCC_APB1Periph_TIM2, TIM2_IRQn,       2, 1);

    /* TIM1: PA3 (APB2 总线) */
    timer_config(TIM1, 2, RCC_APB2Periph_TIM1, TIM1_UP_IRQn,    2, 2);

    TIM4_Config();
}


/* ================================================================
 * 内部工具
 * ================================================================ */

static void compute_arr(uint16_t freq_hz, uint16_t *high, uint16_t *low)
{
    uint32_t full = 1000000UL / freq_hz;
    uint32_t lt = full * note_volume / 100;
    uint32_t ht = full - lt;
    if (lt < 2) lt = 2;
    if (ht < 2) ht = 2;
    *low  = (uint16_t)(lt - 1);
    *high = (uint16_t)(ht - 1);
}


static void start_one_voice(TIM_TypeDef *tim, uint16_t freq,
                            uint16_t *arr_high, uint16_t *arr_low,
                            uint8_t *phase, uint8_t *active_flag,
                            uint16_t gpio_pin)
{
    if (freq < 20)
    {
        TIM_Cmd(tim, DISABLE);
        GPIO_SetBits(GPIOA, gpio_pin);
        *active_flag = 0;
        return;
    }
    compute_arr(freq, arr_high, arr_low);
    TIM_Cmd(tim, DISABLE);
    tim->ARR = *arr_high;
    *phase = 1;
    tim->CNT = 0;
    TIM_Cmd(tim, ENABLE);
    *active_flag = 1;
}


static void stop_one_voice(TIM_TypeDef *tim, uint16_t gpio_pin, uint8_t *active_flag)
{
    TIM_Cmd(tim, DISABLE);
    GPIO_SetBits(GPIOA, gpio_pin);
    *active_flag = 0;
}


static uint16_t compute_harmony_freq(uint16_t freq, HarmonyMode mode)
{
    switch (mode) {
    case HARMONY_UNISON: return freq;
    case HARMONY_OCTAVE: return freq * 2;
    case HARMONY_FIFTH:  return (uint16_t)((uint32_t)freq * 3 / 2);
    default:             return 0;
    }
}


/* ================================================================
 * 单音 API (兼容旧格式 MusicNote[])
 * ================================================================ */

void AudioPWM_StartNote(uint16_t freq)
{
    if (freq < 20) { AudioPWM_StopNote(); return; }

    start_one_voice(TIM3, freq,
                    &v0_arr_high, &v0_arr_low, &v0_phase, &v0_active,
                    GPIO_Pin_0);

    /* 旧和声模式兼容 */
    if (harmony_mode != HARMONY_OFF)
    {
        uint16_t hf = compute_harmony_freq(freq, harmony_mode);
        if (hf >= 20)
        {
            compute_arr(hf, &v1_arr_high, &v1_arr_low);
            TIM_Cmd(TIM2, DISABLE);
            TIM2->ARR = v1_arr_high;
            v1_phase = 1;
            TIM2->CNT = 0;
            TIM_Cmd(TIM2, ENABLE);
            v1_active = 1;
        }
    }
}


void AudioPWM_StopNote(void)
{
    stop_one_voice(TIM3, GPIO_Pin_0, &v0_active);
    stop_one_voice(TIM2, GPIO_Pin_1, &v1_active);
    stop_one_voice(TIM1, GPIO_Pin_3, &v2_active);
}


uint8_t AudioPWM_PlayScore(const MusicNote *score, uint16_t count)
{
    score_ptr  = score;
    score_count = count;
    note_index  = 0;
    is_chord    = 0;
    playing     = 1;
    AudioPWM_StartNote(score[0].freq);
    note_start = ms_tick;
    return 1;
}


/* ================================================================
 * 和弦 API (MusicChord[])
 * ================================================================ */

void AudioPWM_StartChord(const MusicChord *chord)
{
    /* 先停掉所有 */
    stop_one_voice(TIM1, GPIO_Pin_3, &v2_active);
    stop_one_voice(TIM2, GPIO_Pin_1, &v1_active);
    stop_one_voice(TIM3, GPIO_Pin_0, &v0_active);

    /* 按声道数量启动 */
    if (voice_mode >= VOX_1 && chord->f0 >= 20)
        start_one_voice(TIM3, chord->f0,
                        &v0_arr_high, &v0_arr_low, &v0_phase, &v0_active,
                        GPIO_Pin_0);

    if (voice_mode >= VOX_2 && chord->f1 >= 20)
        start_one_voice(TIM2, chord->f1,
                        &v1_arr_high, &v1_arr_low, &v1_phase, &v1_active,
                        GPIO_Pin_1);

    if (voice_mode >= VOX_3 && chord->f2 >= 20)
        start_one_voice(TIM1, chord->f2,
                        &v2_arr_high, &v2_arr_low, &v2_phase, &v2_active,
                        GPIO_Pin_3);
}


uint8_t AudioPWM_PlayChord(const MusicChord *chords, uint16_t count)
{
    score_ptr   = (const void *)chords;
    score_count = count;
    note_index  = 0;
    is_chord    = 1;
    playing     = 1;
    AudioPWM_StartChord(&chords[0]);
    note_start = ms_tick;
    return 1;
}


/* ================================================================
 * 通用 API
 * ================================================================ */

uint8_t AudioPWM_IsPlaying(void) { return playing; }


void AudioPWM_Update(void)
{
    if (!playing) return;

    uint16_t dur;
    if (is_chord)
        dur = ((const MusicChord *)score_ptr)[note_index].dur;
    else
        dur = ((const MusicNote *)score_ptr)[note_index].dur;

    if ((ms_tick - note_start) >= dur)
    {
        note_index++;
        if (note_index >= score_count)
        {
            AudioPWM_StopNote();
            playing = 0;
            return;
        }

        if (is_chord)
            AudioPWM_StartChord(&((const MusicChord *)score_ptr)[note_index]);
        else
            AudioPWM_StartNote(((const MusicNote *)score_ptr)[note_index].freq);

        note_start = ms_tick;
    }
}


void AudioPWM_SetVolume(uint8_t vol)    { if (vol <= 100) note_volume = vol; }
uint8_t AudioPWM_GetVolume(void)         { return note_volume; }

void AudioPWM_SetHarmony(HarmonyMode m)  { harmony_mode = m; }
HarmonyMode AudioPWM_GetHarmony(void)   { return harmony_mode; }

void AudioPWM_SetVoiceMode(VoiceMode m)  { voice_mode = m; }
VoiceMode AudioPWM_GetVoiceMode(void)    { return voice_mode; }
