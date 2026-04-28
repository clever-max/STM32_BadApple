/**
 * test_buzzer.c - 蜂鸣器音乐测试程序（串口控制版）
 * 
 * PC 通过串口发送单字符即可切换测试：
 *   发送 '1'     → 音阶测试
 *   发送 '2'     → 小星星测试
 *   发送 '3'     → Bad Apple 完整音乐
 *   发送 '+'/'-' → 音量加减（步进 10%）
 *   发送 's'     → 停止当前播放
 * 
 * 串口设置：PA9=TX, PA10=RX, 115200-8-N-1
 */

#include "stm32f10x.h"
#include "stm32f10x_usart.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "Delay.h"
#include "OLED.h"
#include "audio_pwm.h"
#include "music_score.h"
#include <string.h>

/* ---- 串口命令 ---- */
static volatile uint8_t  usart_cmd  = 0;   /* 收到的命令字符 */
static volatile uint8_t  cmd_ready  = 0;   /* 新命令就绪标志 */

/* ---- 测试乐谱 ---- */
static const MusicNote test_scale[] = {
    {NOTE_C4, 200}, {NOTE_D4, 200}, {NOTE_E4, 200},
    {NOTE_F4, 200}, {NOTE_G4, 200}, {NOTE_A4, 200},
    {NOTE_B4, 200}, {NOTE_C5, 400},
    {NOTE_B4, 200}, {NOTE_A4, 200}, {NOTE_G4, 200},
    {NOTE_F4, 200}, {NOTE_E4, 200}, {NOTE_D4, 200},
    {NOTE_C4, 400},
};

static const MusicNote twinkle_twinkle[] = {
    {NOTE_C4, 378}, {NOTE_C4, 378}, {NOTE_G4, 378}, {NOTE_G4, 378},
    {NOTE_A4, 378}, {NOTE_A4, 378}, {NOTE_G4, 756},
    {NOTE_F4, 378}, {NOTE_F4, 378}, {NOTE_E4, 378}, {NOTE_E4, 378},
    {NOTE_D4, 378}, {NOTE_D4, 378}, {NOTE_C4, 756},
};

/* ================================================================
 * USART1 — 简单中断接收
 * 引脚：PA9=TX, PA10=RX, 波特率 115200
 * ================================================================ */
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        usart_cmd = USART_ReceiveData(USART1);
        cmd_ready = 1;
    }
}

static void USART1_SendChar(char c)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
    USART_SendData(USART1, c);
}

static void USART1_SendString(const char *s)
{
    while (*s) USART1_SendChar(*s++);
}

static void USART1_Init(uint32_t baud)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    /* PA9 = TX (复用推挽) */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* PA10 = RX (浮空输入) */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitStructure.USART_BaudRate            = baud;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART1, ENABLE);
}

/* ================================================================
 * 菜单与命令等待
 * ================================================================ */
static void OLED_ShowMenu(void)
{
    uint8_t vol = AudioPWM_GetVolume();

    OLED_Clear();
    OLED_ShowString(0, 0, "==BuzzerTest==", OLED_8X16);
    OLED_ShowString(0, 18, "Vol:   % +  -", OLED_6X8);
    OLED_ShowNum(24, 18, vol, 3, OLED_6X8);
    OLED_ShowString(0, 30, "1:Scale 2:Twinkle", OLED_6X8);
    OLED_ShowString(0, 42, "3:BadApple s:Stop", OLED_6X8);
    OLED_Update();
}

static void USART_ShowMenu(void)
{
    uint8_t vol = AudioPWM_GetVolume();

    USART1_SendString("\r\n========================\r\n");
    USART1_SendString("  Buzzer Test Menu\r\n");
    USART1_SendString("------------------------\r\n");
    USART1_SendString("  1 - Scale (C4..C5)\r\n");
    USART1_SendString("  2 - Twinkle Twinkle\r\n");
    USART1_SendString("  3 - Bad Apple (full)\r\n");
    USART1_SendString("  + - Volume (+-10%)\r\n");
    USART1_SendString("  s - Stop current\r\n");
    USART1_SendString("========================\r\n");

    USART1_SendString("Vol: ");
    USART1_SendChar('0' + vol / 100);
    USART1_SendChar('0' + (vol / 10) % 10);
    USART1_SendChar('0' + vol % 10);
    USART1_SendString("%\r\nSend a command: ");
}

static char WaitCmd(void)
{
    while (!cmd_ready) { __NOP(); }
    cmd_ready = 0;
    return (char)usart_cmd;
}

/* 在非阻塞播放期间检查是否有停止/音量命令 */
static uint8_t PollStopCmd(void)
{
    if (cmd_ready)
    {
        char c = (char)usart_cmd;
        cmd_ready = 0;
        if (c == 's')
        {
            AudioPWM_StopNote();
            return 1;
        }
        if (c == '+' || c == '=')
        {
            uint8_t v = AudioPWM_GetVolume();
            if (v < 100) AudioPWM_SetVolume(v + 10);
        }
        if (c == '-' || c == '_')
        {
            int8_t v = (int8_t)AudioPWM_GetVolume();
            if (v > 0) { v -= 10; if (v < 0) v = 0; AudioPWM_SetVolume((uint8_t)v); }
        }
    }
    return 0;
}

/* ================================================================
 * 三个测试模块
 * ================================================================ */

/* 测试 1: 音阶 — 阻塞式逐个音符 */
static void Test1_Scale(void)
{
    int i, n = sizeof(test_scale) / sizeof(test_scale[0]);

    OLED_Clear();
    OLED_ShowString(0, 0, "Test1: Scale", OLED_8X16);
    OLED_ShowString(0, 20, "Blocking mode", OLED_6X8);
    OLED_ShowString(0, 36, "Send s to stop", OLED_6X8);
    OLED_Update();

    USART1_SendString("\r\n[Test 1] Scale (blocking)...\r\n");

    for (i = 0; i < n; i++)
    {
        if (PollStopCmd()) break;

        AudioPWM_StartNote(test_scale[i].freq);
        Delay_ms(test_scale[i].dur);
        AudioPWM_StopNote();
        Delay_ms(50);   /* 音符间隙 */
    }

    USART1_SendString("Done.\r\n");
    Delay_ms(300);
}

/* 测试 2: 小星星 — 非阻塞 */
static void Test2_Twinkle(void)
{
    int n = sizeof(twinkle_twinkle) / sizeof(twinkle_twinkle[0]);

    OLED_Clear();
    OLED_ShowString(0, 0, "Test2: Twinkle", OLED_8X16);
    OLED_ShowString(0, 20, "Non-blocking", OLED_6X8);
    OLED_ShowString(0, 36, "Send s to stop", OLED_6X8);
    OLED_Update();

    USART1_SendString("\r\n[Test 2] Twinkle (non-blocking)...\r\n");

    AudioPWM_PlayScore(twinkle_twinkle, n);
    while (AudioPWM_IsPlaying())
    {
        AudioPWM_Update();
        if (PollStopCmd()) break;
    }

    USART1_SendString("Done.\r\n");
    Delay_ms(300);
}

/* 测试 3: Bad Apple — 非阻塞 */
static void Test3_BadApple(void)
{
    OLED_Clear();
    OLED_ShowString(0, 0, "Test3:BadApple", OLED_8X16);
    OLED_ShowString(0, 20, "827 notes", OLED_6X8);
    OLED_ShowString(0, 36, "Send s to stop", OLED_6X8);
    OLED_Update();

    USART1_SendString("\r\n[Test 3] Bad Apple (827 notes)...\r\n");

    AudioPWM_PlayScore(bad_apple_score, BAD_APPLE_NOTE_COUNT);
    while (AudioPWM_IsPlaying())
    {
        AudioPWM_Update();
        if (PollStopCmd()) break;
    }

    USART1_SendString("Done.\r\n");
    Delay_ms(300);
}

/* ================================================================
 * main — 交互式菜单循环
 * ================================================================ */
int main(void)
{
    OLED_Init();
    AudioPWM_Init();
    USART1_Init(115200);

    while (1)
    {
        OLED_ShowMenu();
        USART_ShowMenu();

        char cmd = WaitCmd();

        /* 防抖：清掉连击残留，等待 300ms 让用户松手 */
        cmd_ready = 0;
        Delay_ms(300);

        /* 回显命令 */
        USART1_SendChar(cmd);
        USART1_SendString("\r\n");

        switch (cmd)
        {
        case '1': Test1_Scale();     break;
        case '2': Test2_Twinkle();   break;
        case '3': Test3_BadApple();  break;
        case '+':
        case '=':
        {
            uint8_t v = AudioPWM_GetVolume();
            if (v < 100) { v += 10; AudioPWM_SetVolume(v); }
            USART1_SendString("Volume: ");
            USART1_SendChar('0' + v / 100);
            USART1_SendChar('0' + (v / 10) % 10);
            USART1_SendChar('0' + v % 10);
            USART1_SendString("%\r\n");
        } break;
        case '-':
        case '_':
        {
            int8_t v = (int8_t)AudioPWM_GetVolume();
            if (v > 0) { v -= 10; if (v < 0) v = 0; AudioPWM_SetVolume((uint8_t)v); }
            USART1_SendString("Volume: ");
            USART1_SendChar('0' + v / 100);
            USART1_SendChar('0' + (v / 10) % 10);
            USART1_SendChar('0' + v % 10);
            USART1_SendString("%\r\n");
        } break;
        case 's': USART1_SendString("Idle (nothing to stop)\r\n"); break;
        default:
            USART1_SendString("Unknown command. Use 1/2/3/s\r\n");
            break;
        }
    }
}
