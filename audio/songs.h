/**
 * songs.h - 歌曲注册表
 * 添加新歌：
 *   1. python convert_to_audio.py song.mid -n "SongName"
 *   2. 在此添加 #include 和 song_table 条目
 *   (无须手动更新计数——sizeof 自动计算)
 */
#ifndef __SONGS_H__
#define __SONGS_H__

#include "audio_pwm.h"
#include "music_score_bad_apple.h"
#include "music_score_wohuainiande.h"
#include "music_score_tori_no_uta.h"
#include "music_score_air.h"

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

#define SONG_COUNT  (sizeof(song_table) / sizeof(song_table[0]))

static const SongEntry song_table[] = {
    {"Bad Apple",     SONG_TYPE_CHORD, song_bad_apple,     SONG_BAD_APPLE_CHORD_COUNT},
    {"Wo Huai Nian",  SONG_TYPE_CHORD, song_wohuainiande,  SONG_WOHUAINIANDE_CHORD_COUNT},
    {"Tori no Uta",   SONG_TYPE_CHORD, song_tori_no_uta,   SONG_TORI_NO_UTA_CHORD_COUNT},
    {"Air", SONG_TYPE_CHORD, song_air, SONG_AIR_CHORD_COUNT},
};

#endif /* __SONGS_H__ */
