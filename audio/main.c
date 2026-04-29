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

/* ── 按键 ── */
#define BTN_PORT   GPIOA
#define BTN_PIN    GPIO_Pin_2

/* ── SysTick ── */
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
    Delay_ms(800);

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

/* ── PA2 按键 ── */
static void BTN_Init(void) {
    GPIO_InitTypeDef g;
    g.GPIO_Pin=BTN_PIN; g.GPIO_Mode=GPIO_Mode_IPU; GPIO_Init(BTN_PORT,&g);
}
static void BTN_Check(uint8_t *force_redraw) {
    static uint32_t last=0; static uint8_t prev=1;
    if(ms()-last<50)return; last=ms();
    uint8_t cur=(GPIO_ReadInputDataBit(BTN_PORT,BTN_PIN)==Bit_RESET)?0:1;
    if(cur==0&&prev==1){
        cur_song=(cur_song+1)%SONG_COUNT; PlaySong(cur_song); is_playing=1; *force_redraw=1;
    }
    prev=cur;
}

/* ── main ── */
int main(void) {
    SysTick_Init(); OLED_Init(); AudioPWM_Init();
    AudioPWM_SetVolume(50); AudioPWM_SetVoiceMode(VOX_3); AudioPWM_SetHarmony(HARMONY_UNISON);
    USART1_Init(115200); BTN_Init();

    /* 启动菜单：蜂鸣器保持静默，OLED 显示歌曲列表 */
    ShowMenu();

    /* 等待用户选择歌曲 (串口 1~9 或 PA2 按键) */
    cur_song = 0;
    is_playing = 0;
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
        /* PA2 按键：直接播放第一首 */
        {
            static uint32_t btn_last = 0; static uint8_t btn_prev = 1;
            if (ms() - btn_last >= 50) {
                btn_last = ms();
                uint8_t cur = (GPIO_ReadInputDataBit(BTN_PORT, BTN_PIN) == Bit_RESET) ? 0 : 1;
                if (cur == 0 && btn_prev == 1) {
                    cur_song = 0;
                    ShowPlayingInfo(0);
                    PlaySong(0);
                    is_playing = 1;
                }
                btn_prev = cur;
            }
        }
    }

    uint8_t force_redraw = 0;
    while (1) {
        AudioPWM_Update();
        if (cmd_ready) { cmd_ready = 0; ProcessCmd(usart_cmd, &force_redraw); }
        /* 多位数超时触发 (500ms 无新数字) */
        if (num_len > 0 && (ms() - num_last_tick) > 500) {
            exec_number(); force_redraw = 1;
        }
        BTN_Check(&force_redraw);
        if (!AudioPWM_IsPlaying() && is_playing) {
            is_playing = 0; force_redraw = 1;
        }
        static uint32_t last_draw = 0;
        if (force_redraw || (ms() - last_draw) > 500) {
            last_draw = ms(); force_redraw = 0; DrawUI(cur_song, is_playing);
        }
    }
}
