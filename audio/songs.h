/**
 * songs.h - 歌曲注册表
 * 添加新歌：1. 创建 music_score_xxx.h  2. 在此注册
 */
#ifndef __SONGS_H__
#define __SONGS_H__

#include "audio_pwm.h"
#include "music_score_bad_apple.h"
#include "music_score_wohuainiande.h"
#include "music_score____.h"

typedef enum {
    SONG_TYPE_NOTE  = 0,  /* MusicNote[] */
    SONG_TYPE_CHORD = 1,  /* MusicChord[] */
} SongType;

typedef struct {
    const char   *name;
    SongType      type;
    const void   *data;       /* MusicNote* 或 MusicChord* */
    uint16_t      count;
} SongEntry;

#define SONG_COUNT  3

static const SongEntry song_table[SONG_COUNT] = {
    {"Bad Apple",    SONG_TYPE_CHORD, song_bad_apple,    SONG_BAD_APPLE_CHORD_COUNT},
    {"Wo Huai Nian De", SONG_TYPE_CHORD, song_wohuainiande, SONG_WOHUAINIANDE_CHORD_COUNT},
    {"Lan Hua Cao",         SONG_TYPE_CHORD, song____,          SONG_____CHORD_COUNT},
};

#endif /* __SONGS_H__ */
