/**
 * main.c - 独立音乐播放器
 * 硬件: PA0蜂鸣器1 + PA1蜂鸣器2 + PA3蜂鸣器3 + PA2按键
 * 串口: PA9=TX PA10=RX @115200
 * OLED: SCL=PB8 SDA=PB9
 */
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"
#include "Delay.h"
#include "OLED.h"
#include "audio_pwm.h"
#include "songs.h"

/* ── 多按钮定义 ── */
#define BTN_LONG_MS     800   /* 长按阈值 */

typedef enum {
    BTN_ACT_NEXT,      /* PB3 short: 下一首 */
    BTN_ACT_PREV,      /* PB6 short: 上一首 */
    BTN_ACT_VOL_UP,    /* PB4 short: 音量+ */
    BTN_ACT_VOL_DOWN,  /* PB5 short: 音量- */
    BTN_ACT_PAUSE,     /* PB3 long:  播放/暂停 */
    BTN_ACT_HARMONY,   /* PB4 long:  切换和声 */
    BTN_ACT_VOICE,     /* PB5 long:  切换声道 */
    BTN_ACT_STOP,      /* PB6 long:  停止 */
} BtnAction;

#define NUM_BTNS  4

typedef struct {
    uint16_t  pin;
    BtnAction short_act;
    BtnAction long_act;
    uint8_t   prev;
    uint32_t  press_tick;
} BtnDef;

static BtnDef g_btns[NUM_BTNS] = {
    {GPIO_Pin_3, BTN_ACT_NEXT,   BTN_ACT_NEXT,   1, 0},  /* PB3 */
    {GPIO_Pin_4, BTN_ACT_VOL_UP, BTN_ACT_HARMONY, 1, 0},  /* PB4 */
    {GPIO_Pin_5, BTN_ACT_VOL_DOWN, BTN_ACT_VOICE,   1, 0},  /* PB5 */
    {GPIO_Pin_6, BTN_ACT_PREV,   BTN_ACT_PREV,   1, 0},  /* PB6 */
};

/* ── 前向声明 ── */
static uint32_t ms(void);
static void PlaySong(uint8_t idx);
static void StopPlay(void);
static uint8_t cur_song;
static uint8_t is_playing;

static void _exec_btn(BtnAction act, uint8_t *fr)
{
    if (fr) *fr = 1;
    switch (act) {
    case BTN_ACT_NEXT:
        cur_song = (cur_song + 1) % SONG_COUNT;
        PlaySong(cur_song); is_playing = 1; break;
    case BTN_ACT_PREV:
        cur_song = (cur_song == 0) ? SONG_COUNT - 1 : cur_song - 1;
        PlaySong(cur_song); is_playing = 1; break;
    case BTN_ACT_VOL_UP: {
        uint8_t v = AudioPWM_GetVolume();
        if (v < 100) AudioPWM_SetVolume(v + 10); break;
    }
    case BTN_ACT_VOL_DOWN: {
        int8_t v = AudioPWM_GetVolume();
        if (v > 0) { v -= 10; if (v < 0) v = 0; AudioPWM_SetVolume(v); } break;
    }
    case BTN_ACT_PAUSE:
        if (is_playing) { is_playing = 0; AudioPWM_Pause(); }
        else { is_playing = 1; AudioPWM_Resume(); }
        break;
    case BTN_ACT_HARMONY: {
        HarmonyMode n[4]={HARMONY_OFF,HARMONY_UNISON,HARMONY_OCTAVE,HARMONY_FIFTH};
        HarmonyMode c=AudioPWM_GetHarmony(); int i;
        for (i=0;i<4;i++){if(n[i]==c)break;}
        AudioPWM_SetHarmony(n[(i+1)%4]); break;
    }
    case BTN_ACT_VOICE: {
        VoiceMode cv = AudioPWM_GetVoiceMode();
        AudioPWM_SetVoiceMode(cv >= VOX_3 ? VOX_1 : (VoiceMode)(cv + 1));
        break;
    }
    case BTN_ACT_STOP:
        is_playing = 0; AudioPWM_StopNote(); break;
    }
}

static void BTN_Init(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);  /* 释放 PB3/PB4 */
    GPIO_InitTypeDef g;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    g.GPIO_Mode  = GPIO_Mode_IPU;
    for (uint8_t i = 0; i < NUM_BTNS; i++) {
        g.GPIO_Pin = g_btns[i].pin;
        GPIO_Init(GPIOB, &g);
    }
}

static void BTN_Check(uint8_t *force_redraw) {
    static uint32_t last_call = 0;
    if (ms() - last_call < 20) return;
    last_call = ms();

    for (uint8_t i = 0; i < NUM_BTNS; i++) {
        BtnDef *b = &g_btns[i];
        uint8_t cur = (GPIO_ReadInputDataBit(GPIOB, b->pin) == Bit_RESET) ? 0 : 1;

        if (cur == 0 && b->prev == 1) {
            /* 下降沿：记录按下时间 */
            b->press_tick = ms();
        }
        else if (cur == 1 && b->prev == 0) {
            /* 上升沿：判断短按还是长按 */
            uint32_t held = ms() - b->press_tick;
            if (held >= BTN_LONG_MS)
                _exec_btn(b->long_act, force_redraw);
            else
                _exec_btn(b->short_act, force_redraw);
        }
        b->prev = cur;
    }
}
static volatile uint32_t g_ms = 0;
void SysTick_Handler(void) { g_ms++; }
static uint32_t ms(void) { return g_ms; }
static void SysTick_Init(void) { SysTick_Config(SystemCoreClock / 1000); }

/* ── 串口 ── */
static volatile uint8_t usart_cmd = 0;
static volatile uint8_t cmd_ready = 0;
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE))
    {
        usart_cmd = USART_ReceiveData(USART1);
        cmd_ready = 1;
    }
}
static void USART1_Init(uint32_t baud)
{
    GPIO_InitTypeDef g; USART_InitTypeDef u; NVIC_InitTypeDef n;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA|RCC_APB2Periph_USART1,ENABLE);
    g.GPIO_Pin=GPIO_Pin_9; g.GPIO_Speed=GPIO_Speed_50MHz; g.GPIO_Mode=GPIO_Mode_AF_PP; GPIO_Init(GPIOA,&g);
    g.GPIO_Pin=GPIO_Pin_10; g.GPIO_Mode=GPIO_Mode_IN_FLOATING; GPIO_Init(GPIOA,&g);
    u.USART_BaudRate=baud; u.USART_WordLength=USART_WordLength_8b;
    u.USART_StopBits=USART_StopBits_1; u.USART_Parity=USART_Parity_No;
    u.USART_HardwareFlowControl=USART_HardwareFlowControl_None;
    u.USART_Mode=USART_Mode_Rx|USART_Mode_Tx;
    USART_Init(USART1,&u); USART_ITConfig(USART1,USART_IT_RXNE,ENABLE);
    n.NVIC_IRQChannel=USART1_IRQn; n.NVIC_IRQChannelPreemptionPriority=1; n.NVIC_IRQChannelSubPriority=0;
    n.NVIC_IRQChannelCmd=ENABLE; NVIC_Init(&n); USART_Cmd(USART1,ENABLE);
}

/* ── OLED 界面 ── */
static const char *vox_str(VoiceMode m) {
    switch(m) { case VOX_1: return "1CH"; case VOX_2: return "2CH"; case VOX_3: return "3CH"; default: return "?"; }
}
static const char *harm_str(HarmonyMode m) {
    switch(m) { case HARMONY_OFF: return "OFF"; case HARMONY_UNISON: return "UNI"; case HARMONY_OCTAVE: return "OCT"; case HARMONY_FIFTH: return "5TH"; default: return "?"; }
}

/* ── 启动菜单 ── */
static void ShowMenu(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "Audio Player", OLED_8X16);
    OLED_ShowString(0, 18, "1-9/Enter pick song", OLED_6X8);
    OLED_ShowString(0, 30, "n:next p:prev s:stop", OLED_6X8);
    OLED_ShowString(0, 42, " +/- vol h:harm v:vox", OLED_6X8);
    OLED_Update();
    Delay_ms(2000);

    OLED_Clear();
    OLED_ShowString(0, 0, "=== Songs ===", OLED_8X16);
    uint8_t i;
    for (i = 0; i < SONG_COUNT; i++)
    {
        OLED_ShowString(0, 18 + i * 12, " ", OLED_6X8);
        OLED_ShowNum(0, 18 + i * 12, i + 1, 1, OLED_6X8);
        OLED_ShowString(6, 18 + i * 12, ". ", OLED_6X8);
        OLED_ShowString(18, 18 + i * 12, (char*)song_table[i].name, OLED_6X8);
    }
    if (SONG_COUNT > 2)
        OLED_ShowString(0, 18 + SONG_COUNT * 12, "...", OLED_6X8);
    OLED_Update();
}

static void ShowPlayingInfo(uint8_t idx)
{
    const SongEntry *e = &song_table[idx];
    OLED_Clear();
    OLED_ShowString(0, 0, "> Now Playing <", OLED_8X16);
    OLED_ShowString(0, 18, (char*)e->name, OLED_6X8);
    OLED_ShowString(0, 30, "Vol:", OLED_6X8);
    OLED_ShowNum(24, 30, AudioPWM_GetVolume(), 3, OLED_6X8);
    OLED_ShowString(48, 30, "%", OLED_6X8);
    OLED_Update();
    Delay_ms(1200);
}

static void DrawUI(uint8_t idx, uint8_t playing)
{
    const SongEntry *e = &song_table[idx];
    uint8_t vol = AudioPWM_GetVolume();
    VoiceMode vm = AudioPWM_GetVoiceMode();
    HarmonyMode hm = AudioPWM_GetHarmony();

    OLED_Clear();
    OLED_ShowString(0, 0, (char*)e->name, OLED_8X16);
    OLED_ShowString(96, 0, (char*)vox_str(vm), OLED_8X16);
    OLED_ShowString(0, 18, "Vol:", OLED_6X8);
    OLED_ShowNum(24, 18, vol, 3, OLED_6X8);
    OLED_ShowString(48, 18, "%", OLED_6X8);
    OLED_ShowString(0, 30, playing ? "> Playing" : "|| Paused", OLED_6X8);
    OLED_ShowString(54, 30, "H:", OLED_6X8);
    OLED_ShowString(66, 30, (char*)harm_str(hm), OLED_6X8);
    OLED_ShowString(0, 42, "n:next s:stop +/- vol", OLED_6X8);
    OLED_Update();
}

/* ── 播放 ── */
static void PlaySong(uint8_t idx)
{
    const SongEntry *e = &song_table[idx];
    if (e->type == SONG_TYPE_CHORD)
        AudioPWM_PlayChord((const MusicChord*)e->data, e->count);
    else
        AudioPWM_PlayScore((const MusicNote*)e->data, e->count);
    DrawUI(idx, 1);
}

static void StopPlay(void)
{
    AudioPWM_StopNote();
}

/* ── 命令处理 ── */
static uint8_t cur_song = 0;
static uint8_t is_playing = 0;

/* 多位数选歌缓冲 */
static uint8_t num_buf[4] = {0};
static uint8_t num_len = 0;
static uint32_t num_last_tick = 0;

static void exec_number(void)
{
    if (num_len == 0) return;
    uint8_t idx = 0;
    for (uint8_t i = 0; i < num_len; i++)
        idx = idx * 10 + (num_buf[i] - '0');
    if (idx > 0 && idx <= SONG_COUNT)
    {
        cur_song = idx - 1;
        PlaySong(cur_song); is_playing = 1;
    }
    num_len = 0;
}

static void ProcessCmd(uint8_t c, uint8_t *force_redraw)
{
    *force_redraw = 0;

    /* 数字: 加入缓冲 */
    if (c >= '0' && c <= '9')
    {
        if (num_len < sizeof(num_buf))
        {
            num_buf[num_len++] = c;
            num_last_tick = ms();
        }
        return;
    }

    /* Enter: 触发多位数选歌 */
    if (c == '\r' || c == '\n')
    {
        exec_number();
        *force_redraw = 1;
        return;
    }

    /* 非数字: 清缓冲再处理命令 */
    num_len = 0;

    switch (c) {
    case 'n': case 'N': case 0x1B:
        cur_song = (cur_song + 1) % SONG_COUNT;
        PlaySong(cur_song); is_playing = 1; *force_redraw = 1;
        break;
    case 'p': case 'P':
        cur_song = (cur_song == 0) ? SONG_COUNT - 1 : cur_song - 1;
        PlaySong(cur_song); is_playing = 1; *force_redraw = 1;
        break;
    case ' ':
        if (is_playing) { is_playing = 0; AudioPWM_StopNote(); }
        else { is_playing = 1; PlaySong(cur_song); }
        *force_redraw = 1;
        break;
    case 's': case 'S':
        is_playing = 0; StopPlay(); *force_redraw = 1;
        break;
    case 'h': case 'H': {
        HarmonyMode n[4]={HARMONY_OFF,HARMONY_UNISON,HARMONY_OCTAVE,HARMONY_FIFTH};
        HarmonyMode c=AudioPWM_GetHarmony(); int i;
        for(i=0;i<4;i++){if(n[i]==c)break;}
        AudioPWM_SetHarmony(n[(i+1)%4]); *force_redraw = 1;
        break;
    }
    case 'v': case 'V': {
        VoiceMode cv=AudioPWM_GetVoiceMode();
        AudioPWM_SetVoiceMode(cv>=VOX_3?VOX_1:(VoiceMode)(cv+1));
        *force_redraw = 1;
        break;
    }
    case '+': case '=': { uint8_t v=AudioPWM_GetVolume(); if(v<100)AudioPWM_SetVolume(v+10); *force_redraw=1; break; }
    case '-': case '_': { int8_t v=AudioPWM_GetVolume(); if(v>0){v-=10;if(v<0)v=0;AudioPWM_SetVolume(v);} *force_redraw=1; break; }
    case 'l': case 'L':
    {
        OLED_Clear();
        OLED_ShowString(0, 0, "Songs:", OLED_8X16);
        uint8_t i;
        for(i=0;i<SONG_COUNT;i++)
            OLED_ShowString(0, 18+i*12, (char*)song_table[i].name, OLED_6X8);
        OLED_Update();
        Delay_ms(2000);
        *force_redraw = 1;
        break;
    }
    default:
        break;
    }
}

/* ── main ── */
int main(void) {
    SysTick_Init(); OLED_Init(); AudioPWM_Init();
    AudioPWM_SetVolume(50); AudioPWM_SetVoiceMode(VOX_3); AudioPWM_SetHarmony(HARMONY_UNISON);
    USART1_Init(115200); BTN_Init();

    static const uint16_t btns_pin[4] = {GPIO_Pin_3, GPIO_Pin_4, GPIO_Pin_5, GPIO_Pin_6};

    while (1)
    {
        /* ── 菜单：显示歌单，等待用户选择 ── */
        cur_song = 0;
        is_playing = 0;
        ShowMenu();

        {
            uint8_t prev[4] = {1,1,1,1};         /* 每次进菜单都重置 */
            while (!is_playing)
            {
                if (cmd_ready)
                {
                    uint8_t c = usart_cmd; cmd_ready = 0;
                    if (c >= '1' && c <= '9')
                    {
                        uint8_t idx = c - '1';
                        if (idx < SONG_COUNT)
                        {
                            cur_song = idx;
                            ShowPlayingInfo(idx);
                            PlaySong(idx);
                            is_playing = 1;
                        }
                    }
                    else if (c == 'n' || c == 'N')
                    {
                        cur_song = 0;
                        ShowPlayingInfo(0);
                        PlaySong(0);
                        is_playing = 1;
                    }
                }
                /* 边沿检测轮询（不用 ms()，因为 Delay 会关 SysTick） */
                {
                    uint8_t any = 0;
                    for (uint8_t i = 0; i < NUM_BTNS; i++)
                    {
                        uint8_t cur = (GPIO_ReadInputDataBit(GPIOB, btns_pin[i]) == Bit_RESET) ? 0 : 1;
                        if (cur == 0 && prev[i] == 1) {
                            prev[i] = 0;
                            any = 1;
                            _exec_btn(g_btns[i].short_act, 0);
                            break;
                        }
                        prev[i] = cur;
                    }
                    if (is_playing)
                        break;
                    if (any)
                        Delay_ms(150);
                    else
                        Delay_ms(2);
                }
            }
        }

        /* ── 同步 BTN_Check 状态，避免进入播放时误触 ── */
        for (uint8_t i = 0; i < NUM_BTNS; i++)
            g_btns[i].prev = (GPIO_ReadInputDataBit(GPIOB, g_btns[i].pin) == Bit_RESET) ? 0 : 1;

        SysTick->CTRL = 0x07;   /* 恢复 SysTick（Delay_ms 会把它关了） */

        /* ── 播放循环 ── */
        uint8_t force_redraw = 0;
        while (is_playing) {
            AudioPWM_Update();
            if (cmd_ready) { cmd_ready = 0; ProcessCmd(usart_cmd, &force_redraw); }
            if (num_len > 0 && (ms() - num_last_tick) > 500) {
                exec_number(); force_redraw = 1;
            }
            BTN_Check(&force_redraw);
            if (!AudioPWM_IsPlaying() && is_playing) {
                is_playing = 0;
            }
            static uint32_t last_draw = 0;
            if (force_redraw || (ms() - last_draw) > 500) {
                last_draw = ms(); force_redraw = 0; DrawUI(cur_song, is_playing);
            }
        }
        /* STOP 或歌曲结束 → 回到外层循环 → 重新显示菜单 */
    }
}
