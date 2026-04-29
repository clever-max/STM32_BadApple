"""
convert_to_audio.py — MIDI → AudioPlayer 乐谱头文件 (.h) 转换器
===================================================================
基于 convert_score.py 的时间切片算法，专为 audio/ 播放器定制。
输出格式直接适配 songs.h 注册表：song_<name> / SONG_<NAME>_CHORD_COUNT

用法:
  python convert_to_audio.py song.mid -n "Bad Apple"
  python convert_to_audio.py song.mid -n "Song Name" -o ../audio/music_score_song.h
  python convert_to_audio.py song.mid -n "Song" --voices 3 --count-only
  python convert_to_audio.py song.mid -n "Song" --min-freq 200 --max-freq 2500

依赖: pip install mido
"""

import sys
import os
import re
import argparse
from pathlib import Path


def midi_pitch_to_freq(pitch):
    """MIDI note number → 频率 Hz"""
    return int(440.0 * (2 ** ((pitch - 69) / 12.0)) + 0.5)


def parse_midi(filepath, track_idx=None, voices=3):
    """解析 MIDI → [((f0,f1,f2,...), dur_ms), ...] 支持动态变速"""
    import mido

    mid = mido.MidiFile(filepath)
    ticks_per_beat = mid.ticks_per_beat

    events = []
    tempo_map = [(0, 500000)]

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

    events.sort(key=lambda e: (e[0], 0 if e[2] == "off" else 1))
    tempo_map.sort(key=lambda x: x[0])

    # 预计算 ticks → ms 缓存
    all_ticks = sorted(set([0] + [e[0] for e in events]))
    tick_to_ms = {}
    ti = pk = 0
    pt = 500000
    total_us = 0
    for t in all_ticks:
        while ti < len(tempo_map) and tempo_map[ti][0] <= t:
            tm_t, tm_tempo = tempo_map[ti]
            seg = tm_t - pk
            if seg > 0:
                total_us += seg * pt
            pk = tm_t
            pt = tm_tempo
            ti += 1
        seg = t - pk
        tick_to_ms[t] = int(round((total_us + seg * pt) / (ticks_per_beat * 1000)))

    # 时间切片：从 active 集合中选 voices 个最高音
    active = set()
    last_freqs = None
    cur_start = 0
    result = []
    prev_t = -1
    i = 0
    while i < len(events):
        tick = events[i][0]
        while i < len(events) and events[i][0] == tick:
            _, pitch, etype = events[i]
            if etype == "off":
                active.discard(pitch)
            else:
                active.add(pitch)
            i += 1
        if tick == prev_t:
            continue
        sorted_notes = sorted(active, reverse=True)[:voices]
        cur_freqs = tuple(
            midi_pitch_to_freq(sorted_notes[j]) if j < len(sorted_notes) else 0
            for j in range(voices)
        )
        if cur_freqs != last_freqs and last_freqs is not None:
            dur = tick_to_ms[tick] - tick_to_ms[cur_start]
            if dur > 0:
                result.append((last_freqs, dur))
            cur_start = tick
        if cur_freqs != last_freqs or last_freqs is None:
            last_freqs = cur_freqs
            cur_start = tick
        prev_t = tick

    if last_freqs is not None and events:
        dur = tick_to_ms[events[-1][0]] - tick_to_ms[cur_start]
        if dur > 0:
            result.append((last_freqs, dur))

    return result


def transpose_chords(chords, min_freq, max_freq):
    """自适应频率提升 + 碰撞消解"""
    if min_freq <= 0 and max_freq <= 0:
        return chords

    def _transpose(freqs):
        r = list(freqs)
        mf = min_freq if min_freq > 0 else 1
        xf = max_freq if max_freq > 0 else 99999
        # Step 1: 升/降到 [mf, xf]
        for j, f in enumerate(r):
            if f == 0:
                continue
            while f < mf:
                f *= 2
            while f > xf:
                f //= 2
            r[j] = f
        # Step 2: 碰撞消解
        nz = [(j, r[j]) for j in range(len(r)) if r[j] > 0]
        if len(nz) <= 1:
            return tuple(r)
        changed = True
        while changed:
            changed = False
            nz.sort(key=lambda x: x[1], reverse=True)
            for k in range(len(nz) - 1):
                if nz[k][1] == nz[k + 1][1]:
                    idx_low = nz[k + 1][0]
                    new_f = r[idx_low] * 2
                    if new_f <= xf:
                        r[idx_low] = new_f
                        changed = True
                    nz = [(j, r[j]) for j in range(len(r)) if r[j] > 0]
                    break
        return tuple(r)

    return [(_transpose(f), d) for f, d in chords]


def generate_header(chords, output_path, song_name, voices, min_freq, max_freq,
                    header_comment=None):
    """生成 MusicChord[] 头文件，适配 songs.h 注册表"""
    n = len(chords)
    # 从 song_name 生成合法的 C 标识符
    ident = re.sub(r'[^a-zA-Z0-9_]', '_', song_name).upper()
    guard = f"__MUSIC_SCORE_{ident}_H__"
    macro = f"SONG_{ident}_CHORD_COUNT"
    var = f"song_{re.sub(r'[^a-zA-Z0-9_]', '_', song_name).lower()}"

    lines = []
    lines.append("/**")
    lines.append(f" * {Path(output_path).name} — {song_name}")
    lines.append(f" * 共 {n} 个和弦, {voices} 声道")
    lines.append(f" * 由 convert_to_audio.py 自动生成 (min_freq={min_freq}, max_freq={max_freq})")
    lines.append(" */")
    lines.append(f"#ifndef {guard}")
    lines.append(f"#define {guard}")
    lines.append("")
    lines.append('#include "audio_pwm.h"')
    lines.append("")
    lines.append(f"#define {macro} {n}")
    lines.append("")
    lines.append(f"static const MusicChord {var}[{macro}] = {{")
    lines.append("")

    for freqs, dur in chords:
        parts = ", ".join(str(freqs[j]) for j in range(voices))
        lines.append(f"    {{{parts}, {dur}}},")

    lines.append("};")
    lines.append("")
    lines.append(f"#endif /* {guard} */")
    lines.append("")

    content = "\n".join(lines)
    out_path = Path(output_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(content, encoding="utf-8")
    return n, var, macro


def main():
    parser = argparse.ArgumentParser(
        description="MIDI → AudioPlayer 乐谱头文件 (.h) 转换器"
    )
    parser.add_argument("input_file", help="输入的 MIDI 文件路径")
    parser.add_argument("-n", "--name", default="Unnamed",
                        help="歌曲名称 (用于生成变量名、宏名) 默认: Unnamed")
    parser.add_argument("-o", "--output", default=None,
                        help="输出路径 (默认: audio/music_score_<name>.h)")
    parser.add_argument("--voices", type=int, choices=[1, 2, 3], default=3,
                        help="声道数 (默认: 3)")
    parser.add_argument("-t", "--track", type=int, default=None,
                        help="MIDI 音轨编号 (默认: 合并所有)")
    parser.add_argument("--min-freq", type=int, default=200,
                        help="最低清晰频率 Hz (默认: 200)")
    parser.add_argument("--max-freq", type=int, default=3000,
                        help="最高有效频率 Hz (默认: 3000)")
    parser.add_argument("--no-transpose", action="store_true",
                        help="禁用频率自适应转换")
    parser.add_argument("--count-only", action="store_true",
                        help="只统计，不生成文件")
    args = parser.parse_args()

    if not os.path.isfile(args.input_file):
        print(f"错误: 文件不存在 — {args.input_file}")
        sys.exit(1)

    try:
        import mido
    except ImportError:
        print("错误: 需要安装 mido 库, 请执行: pip install mido")
        sys.exit(1)

    print(f"解析 MIDI: {args.input_file}")
    chords = parse_midi(args.input_file, args.track, args.voices)

    if not args.no_transpose:
        chords = transpose_chords(chords, args.min_freq, args.max_freq)
        print(f"频率自适应: min={args.min_freq}Hz, max={args.max_freq}Hz")

    n = len(chords)
    total_s = sum(d for _, d in chords) / 1000

    if args.count_only:
        print(f"和弦数: {n}")
        print(f"总时长: {total_s:.1f}s = {total_s / 60:.2f}min")
        voice_usage = [0, 0, 0, 0]
        for f, _ in chords:
            v = sum(1 for x in f if x > 0)
            voice_usage[v] += 1
        print(f"声道分布: 0音={voice_usage[0]}, 1音={voice_usage[1]}, "
              f"2音={voice_usage[2]}, 3音={voice_usage[3]}")
        return

    # 默认输出路径
    if args.output is None:
        ident = re.sub(r'[^a-zA-Z0-9_]', '_', args.name).lower()
        args.output = f"music_score_{ident}.h"

    _, var, macro = generate_header(
        chords, args.output, args.name, args.voices,
        args.min_freq, args.max_freq
    )

    print(f"已生成: {args.output}")
    print(f"  变量: {var}[], 宏: {macro}")
    print(f"  和弦数: {n}, 总时长: {total_s:.1f}s")
    print()
    print(f"在 songs.h 中注册:")
    print(f'    {{"{args.name}", SONG_TYPE_CHORD, {var}, {macro}}},')


if __name__ == "__main__":
    main()
