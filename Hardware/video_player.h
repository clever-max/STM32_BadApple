/**
 * ============================================================
 * video_player.h - 视频播放器模块（头文件）
 * ============================================================
 * 功能：提供 OLED 屏幕视频播放 API
 * 依赖：OLED.h（显存操作）、video_frames.h（帧数据）
 * 平台：STM32F103C8T6（Cortex-M3）
 * ============================================================
 */

#ifndef __VIDEO_PLAYER_H
#define __VIDEO_PLAYER_H

#include "video_frames.h"

/**
 * @brief  播放视频一次（播放完自动停止）
 * @note   播放期间阻塞 CPU，帧间隔由 VIDEO_PLAY_FPS 宏控制
 *          实际视频时长 = VIDEO_FRAME_COUNT / VIDEO_PLAY_FPS 秒
 *          可通过 VideoPlayer_Stop() 提前终止播放
 */
void VideoPlayer_PlayOnce(void);

/**
 * @brief  循环播放视频（不会自动停止）
 * @note   此函数会无限循环，需通过 VideoPlayer_Stop() 终止
 */
void VideoPlayer_PlayLoop(void);

/**
 * @brief  停止正在进行的视频播放
 * @note   只是设置停止标志位，当前正在显示的帧会播放完毕
 */
void VideoPlayer_Stop(void);

#endif
