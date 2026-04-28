#ifndef __VIDEO_FRAMES_H
#define __VIDEO_FRAMES_H

#include <stdint.h>

/* ---- 视频帧参数宏定义 ---- */
#define VIDEO_FRAME_COUNT  60       /* 总帧数 */
#define VIDEO_FRAME_WIDTH  128                 /* 帧宽度（像素） */
#define VIDEO_FRAME_HEIGHT 64                 /* 帧高度（像素） */
#define VIDEO_PLAY_FPS    10                /* 播放帧率 */
#define VIDEO_FRAME_SIZE  1024                /* 单帧字节数 */

/* 视频帧数据数组（存储在 Flash 中） */
extern const uint8_t video_frames[][VIDEO_FRAME_SIZE];

#endif
