/**
 * ============================================================
 * video_stream.h - 视频流接收模块（头文件）
 * ============================================================
 * 协议：[0xAA][0x55][1024字节视频帧]
 * 引脚：PA9=TX, PA10=RX, DMA1_Channel5
 * ============================================================
 */

#ifndef __VIDEO_STREAM_H
#define __VIDEO_STREAM_H

#include <stdint.h>

#define STREAM_BAUDRATE       921600
#define STREAM_FRAME_SIZE     1024
#define STREAM_PKT_SIZE       (STREAM_FRAME_SIZE + 2)
#define STREAM_SYNC0          0xAA
#define STREAM_SYNC1          0x55
#define STREAM_ACK            0xAC

void VideoStream_Init(void);
void VideoStream_Process(void);

#endif
