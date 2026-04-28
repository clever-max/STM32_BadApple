/**
 * audio_pwm.h - 非阻塞蜂鸣器驱动（模仿 Bad Apple Buzzer 架构）
 *
 * 引脚：PA0
 * 原理：TIM3 中断翻转 PA0 → 产生任意频率方波 → 非阻塞！
 *       主循环调用 AudioPWM_Update() 切换音符
 */
#ifndef __AUDIO_PWM_H
#define __AUDIO_PWM_H

#include <stdint.h>

/* 音符定义——直接存频率 */
#define NOTE_PAUSE  0
#define NOTE_C4   262
#define NOTE_CS4  277
#define NOTE_D4   294
#define NOTE_DS4  311
#define NOTE_E4   330
#define NOTE_F4   349
#define NOTE_FS4  370
#define NOTE_G4   392
#define NOTE_GS4  415
#define NOTE_A4   440
#define NOTE_AS4  466
#define NOTE_B4   494
#define NOTE_C5   523
#define NOTE_CS5  554
#define NOTE_D5   587
#define NOTE_DS5  622
#define NOTE_E5   659
#define NOTE_F5   698
#define NOTE_FS5  740
#define NOTE_G5   784
#define NOTE_GS5  831
#define NOTE_A5   880
#define NOTE_AS5  932
#define NOTE_B5   988
#define NOTE_C6  1047
#define NOTE_CS6 1109
#define NOTE_D6  1175
#define NOTE_DS6 1245
#define NOTE_E6  1319
#define NOTE_F6  1397
#define NOTE_FS6 1480
#define NOTE_G6  1568
#define NOTE_GS6 1661
#define NOTE_A6  1760
#define NOTE_AS6 1865
#define NOTE_B6  1976

typedef struct {
    uint16_t freq;  /* Hz, 0 = 休止 */
    uint16_t dur;   /* ms */
} MusicNote;

void AudioPWM_Init(void);
void AudioPWM_StartNote(uint16_t freq_hz);
void AudioPWM_StopNote(void);

/**
 * @brief  设置全局音量（百分比）
 * @param  vol  0~100, 0=静音, 100=最大
 * @note   内部通过 PWM 占空比实现音量调节
 */
void    AudioPWM_SetVolume(uint8_t vol);
uint8_t AudioPWM_GetVolume(void);

/**
 * @brief  非阻塞音符播放器
 * @param  score  音符数组
 * @param  count  音符数量
 * @note   在 main 循环中反复调用, 自动推进
 *         返回非零表示正在播放, 返回 0 表示播完
 */
uint8_t AudioPWM_PlayScore(const MusicNote *score, uint16_t count);
uint8_t AudioPWM_IsPlaying(void);
void    AudioPWM_Update(void);

#endif
