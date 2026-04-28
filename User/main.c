#include "stm32f10x.h"
#include "Delay.h"
#include "OLED.h"
#include "video_stream.h"
#include "audio_pwm.h"

int main(void)
{
	OLED_Init();

	OLED_ShowString(0, 0, "Video Stream", OLED_8X16);
	OLED_ShowString(0, 18, "Waiting PC...", OLED_6X8);
	OLED_ShowString(0, 36, "Baud: 921600", OLED_6X8);
	OLED_Update();
	Delay_ms(2000);

	VideoStream_Init();

	/* 蜂鸣器初始化：TIM3 音频发生器 + TIM4 1ms 时基 */
	AudioPWM_Init();

	while (1)
	{
		VideoStream_Process();
		AudioPWM_Update();
	}
}
