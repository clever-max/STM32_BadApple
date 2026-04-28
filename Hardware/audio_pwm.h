/**
 * audio_pwm.h - 非阻塞蜂鸣器驱动（三蜂鸣器版，真和弦）
 *
 * 引脚：PA0 (TIM3), PA1 (TIM2), PA3 (TIM1)
 * 原理：TIM3/TIM2/TIM1 Update 中断位翻转 GPIO → 任意频率方波
 *       主循环调用 AudioPWM_Update() 切换和弦
 *
 * 声道模式: VOX_1=仅PA0, VOX_2=PA0+PA1, VOX_3=三声道
 */

#ifndef __AUDIO_PWM_H
#define __AUDIO_PWM_H

#include <stdint.h>

/* 音符频率常量 */
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

/* 单音结构体 (兼容旧格式) */
typedef struct {
    uint16_t freq;
    uint16_t dur;
} MusicNote;

/* 三和弦结构体 (新格式, convert_score.py --voices 3) */
typedef struct {
    uint16_t f0;  /* PA0 主旋律, 0=静音 */
    uint16_t f1;  /* PA1 第二声部, 0=静音 */
    uint16_t f2;  /* PA3 第三声部, 0=静音 */
    uint16_t dur; /* ms */
} MusicChord;

/* 兼容旧版和声模式 (当不使用 MusicChord 时) */
typedef enum {
    HARMONY_OFF    = 0,
    HARMONY_UNISON = 1,
    HARMONY_OCTAVE = 2,
    HARMONY_FIFTH  = 3,
} HarmonyMode;

/* 声道数量模式 */
typedef enum {
    VOX_1 = 1,  /* 仅 PA0 */
    VOX_2 = 2,  /* PA0 + PA1 */
    VOX_3 = 3,  /* PA0 + PA1 + PA3 */
} VoiceMode;

void AudioPWM_Init(void);

/* 单音 API (兼容) */
void AudioPWM_StartNote(uint16_t freq_hz);
void AudioPWM_StopNote(void);
uint8_t AudioPWM_PlayScore(const MusicNote *score, uint16_t count);

/* 和弦 API (新) */
void AudioPWM_StartChord(const MusicChord *chord);
uint8_t AudioPWM_PlayChord(const MusicChord *chords, uint16_t count);

/* 通用 */
uint8_t AudioPWM_IsPlaying(void);
void    AudioPWM_Update(void);

void    AudioPWM_SetVolume(uint8_t vol);
uint8_t AudioPWM_GetVolume(void);

void    AudioPWM_SetHarmony(HarmonyMode mode);
HarmonyMode AudioPWM_GetHarmony(void);

void    AudioPWM_SetVoiceMode(VoiceMode mode);
VoiceMode AudioPWM_GetVoiceMode(void);

#endif
