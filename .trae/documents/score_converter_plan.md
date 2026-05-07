# 乐谱转换器 — 实施计划

## 目标

开发一个 Python 程序，接受用户的乐谱文件，生成 STM32 可用的 `music_score.h` 头文件。

## 支持的输入格式

| 格式          | 示例                               | 说明                                  |
| ----------- | -------------------------------- | ----------------------------------- |
| **CSV/纯文本** | `C4,378`  每行一个音符                 | 用音名(C4/D4/E4...)或直接用频率(262/294/...) |
| **双数组格式**   | `music_notes[]` + `music_durs[]` | 当前 `music_score.c` 的旧格式             |
| **JSON**    | `[["C4", 378], ["G4", 756]]`     | 结构清晰，适合脚本生成                         |
| **MIDI** (`.mid`) | 标准 MIDI 文件                    | 可用 MuseScore/FL Studio 等导出，自动提取音符和时长 |

### MIDI 转换规则
```
pitch 69 = A4 = 440Hz  →  freq = 440 × 2^((pitch - 69) / 12)
velocity = 0  →  休止
velocity > 0  →  取 pitch 对应频率

多音轨处理：默认合并所有音轨（按时间排序）
可选 --track N 只取第 N 轨
tempo 变化自动跟随
```

### 依赖
| 格式 | 额外依赖 |
|------|---------|
| CSV/双数组/JSON | 无 |
| MIDI | `pip install mido` |

### 音名解析规则

```
C4=262   CS4=277  D4=294   DS4=311  E4=330   F4=349
FS4=370  G4=392   GS4=415  A4=440   AS4=466  B4=494
C5=523   CS5=554  D5=587   DS5=622  E5=659   F5=698
FS5=740  G5=784   GS5=831  A5=880   AS5=932  B5=988
C6=1047  CS6=1109 D6=1175  DS6=1245 E6=1319  F6=1397
FS6=1480 G6=1568  GS6=1661 A6=1760  AS6=1865 B6=1976
```

还支持直接写频率数值（如 `262,378`），以及 `0` 或 `REST` 表示休止。

## 输出格式

生成 `Hardware/music_score.h`，与当前项目格式完全一致：

```c
/**
 * music_score.h — 自动生成
 * 共 N 个音符
 */
#ifndef __MUSIC_SCORE_H__
#define __MUSIC_SCORE_H__

#include "audio_pwm.h"

#define BAD_APPLE_NOTE_COUNT N

static const MusicNote bad_apple_score[BAD_APPLE_NOTE_COUNT] = {

    {262, 378},
    {294, 378},
    ...
    {0, 1512},
};

#endif
```

## 实施步骤

### 步骤 1: 创建 `video/convert_score.py`

* [x] 定义完整音名→频率映射表

* [x] 实现 `parse_note(str)` → 解析 "C4" / "262" / "REST" / "0"

* [x] 实现 `detect_format(filepath)` → 自动识别输入格式 (.mid / .json / .csv / .c)
* [x] 实现四种解析器: CSV行解析、双数组解析、JSON解析、MIDI解析
* [x] MIDI 解析: 用 `mido` 读取 → 合并音轨 → pitch→freq → ticks→ms → 生成 notes 列表
* [x] 实现 `generate_header(notes, output_path)` → 生成 `.h` 文件

### 步骤 2: CLI 接口

```
usage: convert_score.py [-h] [-o OUTPUT] [-n NAME] [-t TRACK] [--count-only] input_file

positional arguments:
  input_file           输入的乐谱文件路径 (.mid/.json/.csv/.c)

options:
  -o, --output         输出的 .h 文件路径 (默认: Hardware/music_score.h)
  -n, --name           数组名称 (默认: bad_apple_score)
  -t, --track          MIDI 音轨编号 (默认: 合并所有音轨)
  --count-only         只统计音符数量，不生成文件
```

### 步骤 3: 验证

* [ ] 用当前 `music_score.h` 的 CSV 逆导出验证往返一致性
* [ ] 用已知 MIDI 文件验证 pitch→freq 和 tempo→ms 转换精度

## 使用示例

```bash
# MIDI 文件转换（最常见）
python video/convert_score.py song.mid

# 只取第 1 轨
python video/convert_score.py song.mid -t 1

# CSV 文件转换
python video/convert_score.py notes.csv

# JSON 文件转换
python video/convert_score.py notes.json

# 指定输出路径和数组名
python video/convert_score.py song.mid -o Hardware/my_score.h -n my_score

# 只查看音符数量
python video/convert_score.py song.mid --count-only
```

## 文件变更清单

| 操作 | 文件                                            |
| -- | --------------------------------------------- |
| 新建 | `d:\Keil_Project\4pin\video\convert_score.py` |
| 不改 | 所有现有文件均不改动                                    |

