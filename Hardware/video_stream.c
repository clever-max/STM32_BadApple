/**
 * video_stream.c - 视频流接收模块（流水线版）
 * 协议：[0xAA][0x55][1024字节] → ACK 0xAC
 */

#include "video_stream.h"
#include "stm32f10x.h"
#include "stm32f10x_usart.h"
#include "stm32f10x_dma.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "OLED.h"
#include "audio_pwm.h"
#include "music_score.h"
#include <string.h>

extern uint8_t OLED_DisplayBuf[8][128];

static uint8_t rx_buf[STREAM_PKT_SIZE + 16] __attribute__((aligned(4)));
static uint8_t temp_frame[STREAM_FRAME_SIZE];
static volatile uint8_t frame_ready = 0;
static uint8_t music_started = 0;  /* 蜂鸣器是否已触发 */

static void _process_cmd(uint8_t cmd)
{
	switch (cmd)
	{
	case 'h':
	case 'H':
	{
		HarmonyMode next[] = {HARMONY_OFF, HARMONY_UNISON, HARMONY_OCTAVE, HARMONY_FIFTH};
		HarmonyMode cur = AudioPWM_GetHarmony();
		int i;
		for (i = 0; i < 4; i++) { if (next[i] == cur) break; }
		i = (i + 1) % 4;
		AudioPWM_SetHarmony(next[i]);
	} break;
	case '+':
	case '=':
	{
		uint8_t v = AudioPWM_GetVolume();
		if (v < 100) AudioPWM_SetVolume(v + 10);
	} break;
	case '-':
	case '_':
	{
		int8_t v = (int8_t)AudioPWM_GetVolume();
		if (v > 0) { v -= 10; if (v < 0) v = 0; AudioPWM_SetVolume((uint8_t)v); }
	} break;
	default: break;
	}
}

void USART1_IRQHandler(void)
{
	if (USART_GetITStatus(USART1, USART_IT_IDLE) != RESET)
	{
		uint16_t received;

		volatile uint32_t sr_tmp = USART1->SR;
		volatile uint32_t dr_tmp = USART1->DR;
		(void)sr_tmp;
		(void)dr_tmp;

		DMA_Cmd(DMA1_Channel5, DISABLE);
		received = STREAM_PKT_SIZE - DMA_GetCurrDataCounter(DMA1_Channel5);

		/* 单字节命令 */
		if (received == 1 && rx_buf[0] != STREAM_SYNC0)
		{
			_process_cmd(rx_buf[0]);
		}
		/* 完整视频帧 */
		else if (received >= STREAM_PKT_SIZE)
		{
			if (rx_buf[0] == STREAM_SYNC0 && rx_buf[1] == STREAM_SYNC1)
			{
				if (!frame_ready)
				{
					memcpy(temp_frame, &rx_buf[2], STREAM_FRAME_SIZE);
					frame_ready = 1;

					/* 首帧到达时触发蜂鸣器开始播放 */
					if (!music_started)
					{
						music_started = 1;
#if defined(BAD_APPLE_SCORE_CHORD_COUNT)
						AudioPWM_PlayChord(bad_apple_score, BAD_APPLE_SCORE_CHORD_COUNT);
#elif defined(BAD_APPLE_NOTE_COUNT)
						AudioPWM_PlayScore(bad_apple_score, BAD_APPLE_NOTE_COUNT);
#else
						AudioPWM_PlayScore(bad_apple_score, BAD_APPLE_SCORE_NOTE_COUNT);
#endif
					}
				}
				if (USART_GetFlagStatus(USART1, USART_FLAG_TXE) != RESET)
				{
					USART_SendData(USART1, STREAM_ACK);
				}
			}
		}

		DMA_SetCurrDataCounter(DMA1_Channel5, STREAM_PKT_SIZE);
		DMA_Cmd(DMA1_Channel5, ENABLE);
	}
}

static void GPIO_Config(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN_FLOATING;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}

static void USART_Config(void)
{
	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate            = STREAM_BAUDRATE;
	USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits            = USART_StopBits_1;
	USART_InitStructure.USART_Parity              = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART1, &USART_InitStructure);
	USART_DMACmd(USART1, USART_DMAReq_Rx, ENABLE);
	USART_ITConfig(USART1, USART_IT_IDLE, ENABLE);
	USART_Cmd(USART1, ENABLE);
}

static void DMA_Config(void)
{
	DMA_InitTypeDef DMA_InitStructure;
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
	DMA_DeInit(DMA1_Channel5);

	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;
	DMA_InitStructure.DMA_MemoryBaseAddr     = (uint32_t)rx_buf;
	DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralSRC;
	DMA_InitStructure.DMA_BufferSize         = STREAM_PKT_SIZE;
	DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
	DMA_InitStructure.DMA_Priority           = DMA_Priority_High;
	DMA_InitStructure.DMA_M2M                = DMA_M2M_Disable;
	DMA_Init(DMA1_Channel5, &DMA_InitStructure);
	DMA_Cmd(DMA1_Channel5, ENABLE);
}

static void NVIC_Config(void)
{
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

void VideoStream_Init(void)
{
	GPIO_Config();
	USART_Config();
	DMA_Config();
	NVIC_Config();
}

void VideoStream_Process(void)
{
	if (frame_ready)
	{
		memcpy(OLED_DisplayBuf, temp_frame, STREAM_FRAME_SIZE);
		frame_ready = 0;
		OLED_Update();
	}
}
