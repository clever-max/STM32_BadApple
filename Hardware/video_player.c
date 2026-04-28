/**
 * ============================================================
 * video_player.c - 视频播放器模块（实现）
 * ============================================================
 * 工作原理：
 *   1. 视频帧数据以 const 数组形式预存在 Flash 中
 *      格式：video_frames[帧序号][1024 字节]，与显存布局一一对应
 *   2. 播放时通过 memcpy() 将帧数据直接拷贝到显存数组
 *      OLED_DisplayBuf[8][128]
 *   3. 调用 OLED_Update() 通过软件 I2C 将数据刷新到 OLED 硬件
 *   4. 使用 Delay_ms() 控制帧间隔实现指定帧率播放
 *
 * 显存布局（与帧数据一致）：
 *   纵向 8 点为一组（高位在下），先从左到右（列），再从上到下（页）
 *       Page0:  B0 B0 ... (128 列)
 *       Page1:  B0 B0 ... (128 列)
 *       ...
 *       Page7:  B0 B0 ... (128 列)
 * ============================================================
 */

#include "video_player.h"
#include "OLED.h"
#include "Delay.h"
#include <string.h>

/* 引用 OLED 驱动模块中的显存数组，视频帧数据将直接拷贝到此数组 */
extern uint8_t OLED_DisplayBuf[8][128];

/* 停止标志位：为 1 时播放循环中止，volatile 保证中断安全 */
static volatile uint8_t stopped = 0;

/**
 * @brief  播放视频一次，播放完成后返回
 * @note   逐帧从 Flash 读取 → 拷贝到显存 → I2C 刷新到 OLED
 *          帧间隔 = 1000 / VIDEO_PLAY_FPS 毫秒
 */
void VideoPlayer_PlayOnce(void)
{
	uint16_t i;

	stopped = 0;                                /* 清除停止标志 */

	for (i = 0; i < VIDEO_FRAME_COUNT; i++)     /* 遍历所有帧 */
	{
		if (stopped) break;                     /* 检查是否需要提前停止 */

		/* 将当前帧数据从 Flash 拷贝到显存数组 */
		memcpy(OLED_DisplayBuf, video_frames[i], VIDEO_FRAME_SIZE);

		/* 通过 I2C 将显存内容刷新到 OLED 硬件显示 */
		OLED_Update();

		/* 等待一帧的时间间隔（毫秒） */
		Delay_ms(1000 / VIDEO_PLAY_FPS);
	}
}

/**
 * @brief  循环播放视频，直到被 VideoPlayer_Stop() 终止
 * @note   内外双层循环：外层无限循环，内层遍历所有帧
 */
void VideoPlayer_PlayLoop(void)
{
	stopped = 0;                                /* 清除停止标志 */

	while (!stopped)                            /* 外层：无限循环 */
	{
		uint16_t i;
		for (i = 0; i < VIDEO_FRAME_COUNT; i++) /* 内层：遍历所有帧 */
		{
			if (stopped) break;

			memcpy(OLED_DisplayBuf, video_frames[i], VIDEO_FRAME_SIZE);
			OLED_Update();
			Delay_ms(1000 / VIDEO_PLAY_FPS);
		}
	}
}

/**
 * @brief  停止正在播放的视频
 * @note   设置停止标志位，当前帧播放完毕后循环自然退出
 */
void VideoPlayer_Stop(void)
{
	stopped = 1;
}
