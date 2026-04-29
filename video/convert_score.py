"""
convert_score.py — 通用乐谱转 STM32 MusicNote 头文件
=====================================================
支持输入格式:
  .mid   — 标准 MIDI 文件 (需 pip install mido)
  .json  — JSON: [["C4",378], ["G4",756], ...]
  .csv   — 文本: 每行 "音名,时长(ms)" 或 "频率,时长"
  .c     — 双数组格式: music_notes[] + music_durs[]

用法:
  python convert_score.py song.mid
  python convert_score.py song.mid -t 1 -o Hardware/my_score.h
  python convert_score.py notes.csv --count-only
"""

import sys
import os
import json
import re
import argparse
from pathlib import Path

# ============================================================
# 音名 → 频率 映射表
# ============================================================
NOTE_NAME_TO_FREQ = {
    "REST": 0, "PAUSE": 0,
    # Octave 4
    "C4": 262,  "CS4": 277, "DB4": 277, "D4": 294,  "DS4": 311,
    "EB4": 311, "E4": 330,  "F4": 349,  "FS4": 370, "GB4": 370,
    "G4": 392,  "GS4": 415, "AB4": 415, "A4": 440,  "AS4": 466,
    "BB4": 466, "B4": 494,
    # Octave 5
    "C5": 523,  "CS5": 554, "DB5": 554, "D5": 587,  "DS5": 622,
    "EB5": 622, "E5": 659,  "F5": 698,  "FS5": 740, "GB5": 740,
    "G5": 784,  "GS5": 831, "AB5": 831, "A5": 880,  "AS5": 932,
    "BB5": 932, "B5": 988,
    # Octave 6
    "C6": 1047, "CS6": 1109, "DB6": 1109, "D6": 1175, "DS6": 1245,
    "EB6": 1245, "E6": 1319, "F6": 1397, "FS6": 1480, "GB6": 1480,
    "G6": 1568, "GS6": 1661, "AB6": 1661, "A6": 1760, "AS6": 1865,
    "BB6": 1865, "B6": 1976,
}

# 反向: freq -> 最接近的音名 (用于 debug 输出)
_FREQ_TO_NAME = {v: k for k, v in NOTE_NAME_TO_FREQ.items() if v > 0}


# ============================================================
# parse_note(str) → 频率(Hz)
# ============================================================
def parse_note(token):
    """token: "C4" / "262" / "0" / "REST" → 返回频率 Hz (int)"""
    if not isinstance(token, str):
        token = str(token)
    token = token.strip().upper()

    if token in ("0", "REST", "PAUSE", ""):
        return 0

    # 先查音名表
    if token in NOTE_NAME_TO_FREQ:
        return NOTE_NAME_TO_FREQ[token]

    # 纯数字频率
    if token.isdigit():
        return int(token)

    # 可能是 "CS4" 这类没覆盖到的
    raise ValueError(f"无法解析音符: '{token}'")


# ============================================================
# 格式检测
# ============================================================
def detect_format(filepath):
    """返回: 'midi' / 'json' / 'csv' / 'c_dual'"""
    ext = Path(filepath).suffix.lower()

    if ext in (".mid", ".midi"):
        return "midi"
    if ext == ".json":
        return "json"
    if ext in (".c", ".h"):
        return "c_dual"
    return "csv"


# ============================================================
# 各格式解析器 — 返回 [(freq, dur_ms), ...]
# ============================================================

def parse_csv(filepath):
    notes = []
    with open(filepath, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = re.split(r"[\s,;]+", line)
            if len(parts) >= 2:
                freq = parse_note(parts[0])
                dur = int(parts[1])
                notes.append((freq, dur))
    return notes


def parse_json(filepath):
    with open(filepath, "r", encoding="utf-8") as f:
        data = json.load(f)
    notes = []
    for item in data:
        if isinstance(item, list):
            freq = parse_note(str(item[0]))
            dur = int(item[1])
            notes.append((freq, dur))
    return notes


def parse_c_dual(filepath):
    """解析 music_score.c 双数组格式: music_notes[] + music_durs[]"""
    text = Path(filepath).read_text(encoding="utf-8")

    # 提取 music_notes[] 数组
    notes_match = re.search(
        r"music_notes\[\]\s*=\s*\{([^}]+)\}", text, re.DOTALL
    )
    durs_match = re.search(
        r"music_durs\[\]\s*=\s*\{([^}]+)\}", text, re.DOTALL
    )
    if not notes_match or not durs_match:
        raise ValueError("未找到 music_notes[] 或 music_durs[] 数组")

    freqs = [int(x.strip()) for x in notes_match.group(1).split(",") if x.strip()]
    durs  = [int(x.strip()) for x in durs_match.group(1).split(",") if x.strip()]

    if len(freqs) != len(durs):
        raise ValueError(f"频率/时长数组长度不一致: {len(freqs)} vs {len(durs)}")

    return list(zip(freqs, durs))


def parse_midi(filepath, track_idx=None, poly_mode="highest", voices=1):
    """解析 MIDI 文件 → [(freq, dur_ms), ...] 或 [(freqs_tuple, dur_ms), ...]

    使用时间切片算法：在任意 tick 时刻选择当前应播放的频率，
    正确插入休止符。支持动态变速（ritardando/accelerando）。

    voices: 输出声道数 (1/2/3)
        voices=1 → [(freq, dur), ...]  (兼容旧格式 MusicNote[])
        voices>1 → [((f0,f1,...), dur), ...]  (MusicChord[])
    poly_mode: voices=1 时的多音选择
        "highest" - 取最高音 (旋律线，默认)
        "lowest"  - 取最低音 (贝斯线)
    """
    try:
        import mido
    except ImportError:
        print("错误: 需要安装 mido 库才能解析 MIDI")
        print("  pip install mido")
        sys.exit(1)

    mid = mido.MidiFile(filepath)
    ticks_per_beat = mid.ticks_per_beat

    events = []
    tempo_map = [(0, 500000)]  # (tick, tempo_us_per_beat)

    for i, track in enumerate(mid.tracks):
        if track_idx is not None and i != track_idx:
            continue
        abs_tick = 0
        for msg in track:
            abs_tick += msg.time
            if msg.type == "set_tempo":
                tempo_map.append((abs_tick, msg.tempo))
            elif msg.type == "note_on" and msg.velocity > 0:
                events.append((abs_tick, msg.note, "on"))
            elif msg.type == "note_off" or (msg.type == "note_on" and msg.velocity == 0):
                events.append((abs_tick, msg.note, "off"))

    # note_off 排前 (先释放再按下，避免同一 tick on/off 误判重叠)
    events.sort(key=lambda e: (e[0], 0 if e[2] == "off" else 1))
    tempo_map.sort(key=lambda x: x[0])

    # 预计算所有切片 tick 的绝对 ms（只除一次，避免累积截断误差）
    all_ticks = sorted(set([0] + [e[0] for e in events]))
    tick_to_ms_cache = {}
    ti = 0
    prev_tick = 0
    prev_tempo = 500000
    total_us = 0
    for target_tick in all_ticks:
        while ti < len(tempo_map) and tempo_map[ti][0] <= target_tick:
            tm_tick, tm_tempo = tempo_map[ti]
            seg = tm_tick - prev_tick
            if seg > 0:
                total_us += seg * prev_tempo
            prev_tick = tm_tick
            prev_tempo = tm_tempo
            ti += 1
        seg = target_tick - prev_tick
        tick_to_ms_cache[target_tick] = int(round((total_us + seg * prev_tempo) / (ticks_per_beat * 1000)))


    if voices <= 1:
        return _parse_midi_time_slice(events, poly_mode, tick_to_ms_cache)
    else:
        return _parse_midi_time_slice_multi(events, voices, tick_to_ms_cache)


def _parse_midi_time_slice(events, poly_mode, tick_to_ms):
    """时间切片解析：逐 tick 输出 1 个频率（含休止符）

    对单声道 MIDI，一次输出 1 个 (freq, dur)。
    对多音 MIDI，只选最高/最低音。
    tick_to_ms: dict{abs_tick: abs_ms}
    """
    active = set()
    last_freq = -1
    cur_start_tick = 0
    notes_result = []

    i = 0
    prev_tick = -1
    while i < len(events):
        tick = events[i][0]

        while i < len(events) and events[i][0] == tick:
            _, pitch, etype = events[i]
            if etype == "off":
                active.discard(pitch)
            else:
                active.add(pitch)
            i += 1

        if tick == prev_tick:
            continue

        if active:
            chosen = max(active) if poly_mode != "lowest" else min(active)
        else:
            chosen = None

        cur_freq = midi_pitch_to_freq(chosen) if chosen is not None else 0

        if cur_freq != last_freq and last_freq >= 0:
            dur_ms = tick_to_ms[tick] - tick_to_ms[cur_start_tick]
            if dur_ms > 0:
                notes_result.append((last_freq, dur_ms))
            cur_start_tick = tick

        if cur_freq != last_freq or last_freq < 0:
            last_freq = cur_freq
            cur_start_tick = tick

        prev_tick = tick

    if last_freq >= 0 and events:
        dur_ms = tick_to_ms[events[-1][0]] - tick_to_ms[cur_start_tick]
        if dur_ms > 0:
            notes_result.append((last_freq, dur_ms))

    return notes_result


def _parse_midi_time_slice_multi(events, num_voices, tick_to_ms):
    """多声道时间切片：逐 tick 输出 num_voices 个频率 → [(freqs_tuple, dur), ...]

    每个切片时刻，从 active 集合中选 num_voices 个音，
    按 pitch 从高到低排列，不足则补 0（休止）。
    tick_to_ms: dict{abs_tick: abs_ms}
    """
    active = set()
    last_freqs = None
    cur_start_tick = 0
    notes_result = []

    i = 0
    prev_tick = -1
    while i < len(events):
        tick = events[i][0]

        while i < len(events) and events[i][0] == tick:
            _, pitch, etype = events[i]
            if etype == "off":
                active.discard(pitch)
            else:
                active.add(pitch)
            i += 1

        if tick == prev_tick:
            continue

        # 选 N 个音 (从高到低)
        sorted_notes = sorted(active, reverse=True)[:num_voices]
        cur_freqs = tuple(
            midi_pitch_to_freq(sorted_notes[j]) if j < len(sorted_notes) else 0
            for j in range(num_voices)
        )

        if cur_freqs != last_freqs and last_freqs is not None:
            dur_ms = tick_to_ms[tick] - tick_to_ms[cur_start_tick]
            if dur_ms > 0:
                notes_result.append((last_freqs, dur_ms))
            cur_start_tick = tick

        if cur_freqs != last_freqs or last_freqs is None:
            last_freqs = cur_freqs
            cur_start_tick = tick

        prev_tick = tick

    if last_freqs is not None and events:
        dur_ms = tick_to_ms[events[-1][0]] - tick_to_ms[cur_start_tick]
        if dur_ms > 0:
            notes_result.append((last_freqs, dur_ms))

    return notes_result


def midi_pitch_to_freq(pitch):
    """MIDI note number → 频率 Hz"""
    return int(440.0 * (2 ** ((pitch - 69) / 12.0)) + 0.5)


# ============================================================
# 频率自适应转换（解决蜂鸣器低频表现差的问题）
# ============================================================
def transpose_chords(chords, min_freq, max_freq):
    """对每个和弦做自适应频率提升 + 碰撞消解

    chords: [((f0,f1,f2,...), dur), ...]
    返回:   [((f0',f1',f2',...), dur), ...]
    """
    if min_freq <= 0 and max_freq <= 0:
        return chords

    min_f = min_freq if min_freq > 0 else 1
    max_f = max_freq if max_freq > 0 else 99999

    result = []
    for freqs, dur in chords:
        new_freqs = _transpose_chord(freqs, min_f, max_f)
        result.append((new_freqs, dur))
    return result


def _transpose_chord(freqs, min_freq, max_freq):
    """自适应多声部频率提升 + 碰撞消解"""
    num_voices = len(freqs)
    result = list(freqs)

    # Step 1: 各声部独立升至 [min_freq, max_freq]
    for i, f in enumerate(result):
        if f == 0:
            continue
        while f < min_freq:
            f *= 2
        while f > max_freq:
            f //= 2
        result[i] = f

    # Step 2: 碰撞消解 — 将 (原始声部索引, 频率) 按频率降序
    non_zero = [(i, result[i]) for i in range(num_voices) if result[i] > 0]
    if len(non_zero) <= 1:
        return tuple(result)

    changed = True
    while changed:
        changed = False
        non_zero.sort(key=lambda x: x[1], reverse=True)
        for j in range(len(non_zero) - 1):
            if non_zero[j][1] == non_zero[j + 1][1]:
                # 碰撞！取较低索引（较低声部位）升八度
                idx_low = non_zero[j + 1][0]
                new_f = result[idx_low] * 2
                if new_f <= max_freq:
                    result[idx_low] = new_f
                    changed = True
                non_zero = [(i, result[i]) for i in range(num_voices) if result[i] > 0]
                break

    return tuple(result)


# ============================================================
# 头文件生成
# ============================================================
def generate_header(notes, output_path, array_name="bad_apple_score",
                    header_comment=None):
    """单音格式: MusicNote[]"""
    n = len(notes)
    if header_comment is None:
        header_comment = f"music_score.h — 自动生成\n * 共 {n} 个音符"

    macro_name = f"__{array_name.upper()}_H__"

    lines = []
    lines.append("/**")
    for line in header_comment.split("\n"):
        lines.append(f" * {line.strip()}")
    lines.append(" */")
    lines.append(f"#ifndef {macro_name}")
    lines.append(f"#define {macro_name}")
    lines.append("")
    lines.append('#include "audio_pwm.h"')
    lines.append("")
    lines.append(f"#define {array_name.upper()}_NOTE_COUNT {n}")
    lines.append("")
    lines.append(f"static const MusicNote {array_name}[{array_name.upper()}_NOTE_COUNT] = {{")
    lines.append("")

    for freq, dur in notes:
        lines.append(f"    {{{freq}, {dur}}},")

    lines.append("};")
    lines.append("")
    lines.append(f"#endif /* {macro_name} */")
    lines.append("")

    content = "\n".join(lines)

    out_path = Path(output_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(content, encoding="utf-8")

    return n


def generate_chord_header(chords, output_path, array_name="bad_apple_score",
                          header_comment=None, voices=3):
    """多声道和弦格式: MusicChord[]"""
    n = len(chords)
    if header_comment is None:
        header_comment = f"music_score.h — 自动生成\n * 共 {n} 个和弦, {voices} 声道"

    macro_name = f"__{array_name.upper()}_H__"

    lines = []
    lines.append("/**")
    for line in header_comment.split("\n"):
        lines.append(f" * {line.strip()}")
    lines.append(" */")
    lines.append(f"#ifndef {macro_name}")
    lines.append(f"#define {macro_name}")
    lines.append("")
    lines.append('#include "audio_pwm.h"')
    lines.append("")
    lines.append(f"#define {array_name.upper()}_CHORD_COUNT {n}")
    lines.append("")
    lines.append(f"static const MusicChord {array_name}[{array_name.upper()}_CHORD_COUNT] = {{")
    lines.append("")

    for freqs, dur in chords:
        parts = ", ".join(str(freqs[j]) if j < len(freqs) else "0" for j in range(voices))
        lines.append(f"    {{{parts}, {dur}}},")

    lines.append("};")
    lines.append("")
    lines.append(f"#endif /* {macro_name} */")
    lines.append("")

    content = "\n".join(lines)

    out_path = Path(output_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(content, encoding="utf-8")

    return n


# ============================================================
# CLI
# ============================================================
def main():
    parser = argparse.ArgumentParser(
        description="乐谱 → STM32 music_score.h 转换器"
    )
    parser.add_argument("input_file", help="输入的乐谱文件 (.mid/.json/.csv/.c)")
    parser.add_argument("-o", "--output",
                        default="Hardware/music_score.h",
                        help="输出 .h 文件路径 (默认: Hardware/music_score.h)")
    parser.add_argument("-n", "--name", default="bad_apple_score",
                        help="C 数组名称 (默认: bad_apple_score)")
    parser.add_argument("-t", "--track", type=int, default=None,
                        help="MIDI 音轨编号 (默认: 合并所有音轨)")
    parser.add_argument("--poly", choices=["highest", "lowest"],
                        default="highest",
                        help="多音选择: highest(取最高音,默认)/lowest(取最低音)")
    parser.add_argument("--voices", type=int, choices=[1, 2, 3], default=1,
                        help="输出声道数: 1=单音MusicNote, 2/3=多音MusicChord (默认: 1)")
    parser.add_argument("--min-freq", type=int, default=200,
                        help="蜂鸣器最低清晰频率(Hz), 低于此值的音自动升八度 (默认: 200)")
    parser.add_argument("--max-freq", type=int, default=3000,
                        help="蜂鸣器最高有效频率(Hz), 高于此值的音自动降八度 (默认: 3000)")
    parser.add_argument("--no-transpose", action="store_true",
                        help="禁用频率自适应转换 (保留原始频率)")
    parser.add_argument("--count-only", action="store_true",
                        help="只统计音符数，不生成文件")
    args = parser.parse_args()

    input_file = args.input_file
    if not os.path.isfile(input_file):
        print(f"错误: 文件不存在 — {input_file}")
        sys.exit(1)

    fmt = detect_format(input_file)
    print(f"检测格式: {fmt}")

    # 解析
    parsers = {
        "midi":    lambda: parse_midi(input_file, args.track, args.poly, args.voices),
        "json":    lambda: parse_json(input_file),
        "csv":     lambda: parse_csv(input_file),
        "c_dual":  lambda: parse_c_dual(input_file),
    }

    try:
        result = parsers[fmt]()
    except Exception as e:
        print(f"解析失败: {e}")
        import traceback; traceback.print_exc()
        sys.exit(1)

    # 多声道输出的是和弦列表
    if args.voices > 1:
        chords = result  # [((f0,f1,f2), dur), ...]

        # 频率自适应转换（默认开启，--no-transpose 关闭）
        if not args.no_transpose and (args.min_freq > 0 or args.max_freq > 0):
            chords = transpose_chords(chords, args.min_freq, args.max_freq)
            print(f"频率自适应: min={args.min_freq}Hz, max={args.max_freq}Hz")

        n = len(chords)
    else:
        notes = result  # [(freq, dur), ...]
        n = len(notes)

    if n == 0:
        print("警告: 未提取到任何音符")
        return

    if args.count_only:
        print(f"音符/和弦数量: {n}")
        if args.voices > 1:
            total_ms = sum(d for _, d in chords)
        else:
            total_ms = sum(d for _, d in notes)
        print(f"总时长: {total_ms} ms = {total_ms / 1000:.1f} s = {total_ms / 60000:.2f} min")
        # 统计多音分布
        if args.voices > 1:
            voice_usage = [0, 0, 0, 0]  # 0/1/2/3 voice chords
            for freqs, _ in chords:
                nv = sum(1 for f in freqs if f > 0)
                voice_usage[nv] += 1
            print(f"声道分布: 0音={voice_usage[0]}, 1音={voice_usage[1]}, "
                  f"2音={voice_usage[2]}, 3音={voice_usage[3]}")
        return

    # 生成
    input_name = Path(input_file).name
    if args.voices > 1:
        transpose_note = ""
        if not args.no_transpose:
            transpose_note = f"\n * 频率自适应: min={args.min_freq}Hz, max={args.max_freq}Hz"
        header_comment = (
            f"{input_name} → STM32 MusicChord\n"
            f" * 共 {n} 个和弦, {args.voices} 声道\n"
            f" * 由 convert_score.py --voices {args.voices} 自动生成"
            f"{transpose_note}"
        )
        generate_chord_header(chords, args.output, args.name,
                              header_comment, args.voices)
        print(f"已生成: {args.output}  ({n} 个和弦, {args.voices} 声道)")
        total_ms = sum(d for _, d in chords)
    else:
        header_comment = (
            f"{input_name} → STM32 MusicNote\n"
            f" * 共 {n} 个音符\n"
            f" * 由 convert_score.py 自动生成"
        )
        generate_header(notes, args.output, args.name, header_comment)
        print(f"已生成: {args.output}  ({n} 个音符)")
        total_ms = sum(d for _, d in notes)

    print(f"总时长: {total_ms / 1000:.1f} s")


if __name__ == "__main__":
    main()
