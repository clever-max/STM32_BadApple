# Audio 独立音乐播放器 — 实施计划

## 目标

新建 `audio/` 目录，实现一个**独立于视频流**的专用音乐播放器。支持：
- 多首歌曲任意切换（串口命令 + PA2 按键）
- OLED 显示当前歌曲名、播放进度、音量、和声/声道模式
- 非阻塞播放（与现有 `audio_pwm.c` 完全兼容）
- 交互式串口控制

---

## 目录结构

```
4pin/
├── audio/
│   ├── main.c                    ★ 音乐播放器主程序
│   ├── songs.h                   ★ 歌曲注册表（索引所有歌曲）
│   ├── music_score_bad_apple.h   ★ 歌曲1：Bad Apple (三和弦版)
│   ├── music_score_wohuainiande.h ★ 歌曲2：我怀念的 (三和弦版)
│   └── music_score_xxx.h         ★ 歌曲N：用户自行添加
```

### `music_score_xxx.h` 命名规范

每个歌曲头文件定义：

```c
// music_score_bad_apple.h 示例
#ifndef __MUSIC_SCORE_BAD_APPLE_H__
#define __MUSIC_SCORE_BAD_APPLE_H__
#include "audio_pwm.h"

#define SONG_NAME "Bad Apple"
#define SONG_CHORD_COUNT 1608

static const MusicChord song_bad_apple[SONG_CHORD_COUNT] = {
    {312, 624,   0, 222},
    ...
};
#endif
```

> 变量命名规则：`song_<name>`（小写+下划线），与宏 `SONG_<NAME>_CHORD_COUNT` 对应。

### `songs.h` — 歌曲注册表

```c
typedef enum {
    SONG_IDX_BAD_APPLE = 0,
    SONG_IDX_WOHUAINIANDE,
    SONG_COUNT
} SongIndex;

typedef enum {
    SONG_TYPE_NOTE  = 0,  /* MusicNote[] 单音 */
    SONG_TYPE_CHORD = 1,  /* MusicChord[] 和弦 */
} SongType;

typedef struct {
    const char   *name;
    SongType      type;
    const void   *data;       /* MusicNote* 或 MusicChord* */
    uint16_t      count;
} SongEntry;

static const SongEntry song_table[SONG_COUNT] = {
    [SONG_IDX_BAD_APPLE]    = {"Bad Apple",    SONG_TYPE_CHORD, song_bad_apple,    SONG_BAD_APPLE_CHORD_COUNT},
    [SONG_IDX_WOHUAINIANDE] = {"Wo Huai Nian", SONG_TYPE_CHORD, song_wohuainiande, SONG_WOHUAINIANDE_CHORD_COUNT},
};
```

---

## OLED 界面设计（4 行）

```
┌──────────────────────────┐
│ ♫ Wo Huai Nian     [3CH] │  ← 第1行: 歌名 + 声道模式
│ [████████░░]  45%   50%  │  ← 第2行: 进度条 + 百分比 + 音量
│ ▶ Playing    H:UNI       │  ← 第3行: 播放状态 + 和声模式
│ ← → song   s:stop        │  ← 第4行: 操作提示
└──────────────────────────┘
```

- 第1行（8x16字体）：`♫` + 歌名（最多12字符）+ 声道模式（3CH/2CH/1CH）
- 第2行（6x8字体）：进度条 `[████░░]` + 百分比 + 音量%
- 第3行（6x8字体）：状态（▶ Playing / ⏸ Paused）+ 和声模式
- 第4行（6x8字体）：按键提示

---

## 串口命令

| 命令 | 操作 |
|------|------|
| `n` / `→` | 下一首 |
| `p` / `←` | 上一首 |
| `1`~`9` | 直接选第 N 首 |
| `[Space]` | 播放/暂停 |
| `s` | 停止 |
| `h` | 切换和声 OFF→UNI→OCT→5TH |
| `v` | 切换声道 1CH→2CH→3CH |
| `+`/`=` | 音量 +10% |
| `-`/`_` | 音量 -10% |
| `i` | 显示当前歌曲信息 |
| `l` | 列出所有歌曲 |

--- 

## PA2 按键

- 短按：下一首
- 长按（>1s）：播放/暂停

---

## `audio/main.c` 核心逻辑

```c
int main(void)
{
    // 初始化
    SysTick_Init();
    OLED_Init();
    AudioPWM_Init();
    AudioPWM_SetVolume(50);
    AudioPWM_SetVoiceMode(VOX_3);
    USART1_Init(115200);
    BTN_Init();       // PA2

    ShowSongList();   // 显示歌曲列表 2 秒

    current_song = SONG_IDX_BAD_APPLE;  // 默认第一首
    DisplaySongInfo(current_song);

    while (1)
    {
        // 串口命令处理
        if (cmd_ready) { ProcessCmd(usart_cmd); cmd_ready = 0; }

        // 按键处理
        BTN_Check();

        // 音频更新（非阻塞）
        AudioPWM_Update();

        // 播放完毕自动切下一首
        if (!AudioPWM_IsPlaying() && was_playing)
        {
            was_playing = 0;
            current_song = (current_song + 1) % SONG_COUNT;
            PlaySong(current_song);
            was_playing = 1;
        }

        // 播放中更新进度条
        if (AudioPWM_IsPlaying())
            UpdateProgressBar();
    }
}
```

### 关键函数

```c
// 开始播放指定歌曲
void PlaySong(SongIndex idx)
{
    const SongEntry *e = &song_table[idx];
    if (e->type == SONG_TYPE_CHORD)
        AudioPWM_PlayChord((const MusicChord*)e->data, e->count);
    else
        AudioPWM_PlayScore((const MusicNote*)e->data, e->count);
    DisplaySongInfo(idx);
}

// 更新进度条（1秒刷新一次）
void UpdateProgressBar(void)
{
    static uint32_t last = 0;
    if (ms() - last < 1000) return;
    last = ms();
    // 用 note_index / score_count 算进度
    // 画 [████░░░░] + 百分比到 OLED
}
```

---

## 实施步骤

| 步骤 | 内容 | 文件 |
|------|------|------|
| 1 | 创建 `audio/` 目录 | — |
| 2 | 从现有 `music_score.h` 提取 Song 1 数据 → `music_score_xxx.h`（改名+加 SONG_NAME 宏） | `audio/music_score_*.h` |
| 3 | 创建 `songs.h` 歌曲注册表 | `audio/songs.h` |
| 4 | 编写 `main.c`（OLED + 串口 + 按键 + 播放逻辑） | `audio/main.c` |
| 5 | 创建配套 PC 脚本 `audio_control.py` | `video/audio_control.py` |
| 6 | Keil 中新建 Target "AudioPlayer" 或切换 User 目录 | `Project.uvprojx` |

---

## 关于 Keil 工程

方案 A（推荐）：在 Keil 中新建 **Target "AudioPlayer"**，将 `audio/main.c` + `audio/songs.h` + `audio/music_score_*.h` 加入编译，移除 `User/main.c` 和 `video_stream.c`。同一 Project 两个 Target 互不干扰。

方案 B：直接替换 `User/main.c`，但会覆盖视频流版本。不如方案 A 灵活。

---

## Flash 占用估算

| 组件 | 大小 |
|------|------|
| 代码（main + OLED + audio_pwm） | ~5KB |
| song_bad_apple (1608×8B) | ~12.8KB |
| song_wohuainiande (1526×8B) | ~12.2KB |
| **合计** | **~30KB / 64KB** ✅ |

剩余空间足够再加 2~3 首歌。

---

## PC 端控制脚本（可选）

仿照 `buzzer_control.py` 写一个 `audio_control.py`，单键发送命令，实时显示播放状态。
