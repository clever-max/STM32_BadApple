import mido
import sys
import math

# --------------------- 工具函数 ---------------------
def note_to_name(note):
    """将 MIDI 音符号转为音名，如 C4, F#3"""
    names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    octave = (note // 12) - 1
    name = names[note % 12]
    return f"{name}{octave}"

def get_bin_index(note, note_bins):
    """返回音符所属的音域区间索引（0-based），不在任何区间返回 -1"""
    for i in range(len(note_bins) - 1):
        if note_bins[i] <= note < note_bins[i+1]:
            return i
    return -1

# --------------------- 核心分析 ---------------------
def max_polyphony_and_distribution(midi_file, ignore_drums=True, note_bins=None):
    """
    分析 MIDI 文件的最大复音数、各音域区间音符出现次数及各区间的最大同时发声数。
    参数：
        midi_file   : 文件路径
        ignore_drums: 是否忽略通道 9 (打击乐)
        note_bins   : 音域边界列表（MIDI音符号），如 [0, 48, 60, 72, 84, 96, 108, 128]
    返回：
        max_poly_global   : 全局最大同时发声音符数
        distribution       : dict，区间名称 -> 音符出现次数
        max_per_bin        : dict，区间名称 -> 该区间内最大同时发声数
    """
    if note_bins is None:
        note_bins = [0, 48, 60, 72, 84, 96, 108, 128]

    mid = mido.MidiFile(midi_file)
    events = []  # (abs_tick, type, note, channel)

    # 1. 收集所有 note_on/off 事件
    for track in mid.tracks:
        abs_time = 0
        for msg in track:
            abs_time += msg.time
            if msg.type == 'note_on' and msg.velocity > 0:
                if ignore_drums and msg.channel == 9:
                    continue
                events.append((abs_time, 'on', msg.note, msg.channel))
            elif msg.type == 'note_off' or (msg.type == 'note_on' and msg.velocity == 0):
                if ignore_drums and msg.channel == 9:
                    continue
                events.append((abs_time, 'off', msg.note, msg.channel))

    # 按时间排序（相同时间先关后开，防止瞬时计数偏高）
    events.sort(key=lambda x: (x[0], x[1] != 'off'))

    # 准备区间名称和计数器
    num_bins = len(note_bins) - 1
    bin_labels = []
    for i in range(num_bins):
        low = note_bins[i]
        high = note_bins[i+1]
        if low == 0:
            label = f"< {note_to_name(high)}"
        elif high == 128:
            label = f"≥ {note_to_name(low)}"
        else:
            label = f"{note_to_name(low)} – {note_to_name(high)}"
        bin_labels.append(label)

    bin_counts = [0] * num_bins           # 各区间音符出现总次数
    active_per_bin = [0] * num_bins       # 各区间当前发声数
    max_per_bin = [0] * num_bins          # 各区间历史最大并发数

    active_global = 0
    max_global = 0

    # 2. 遍历事件，更新所有统计量
    for _, etype, note, _ in events:
        idx = get_bin_index(note, note_bins)
        if etype == 'on':
            active_global += 1
            max_global = max(max_global, active_global)

            if idx != -1:
                bin_counts[idx] += 1
                active_per_bin[idx] += 1
                max_per_bin[idx] = max(max_per_bin[idx], active_per_bin[idx])
        else:  # 'off'
            active_global -= 1
            if idx != -1:
                active_per_bin[idx] -= 1

    # 构建返回的字典
    distribution = dict(zip(bin_labels, bin_counts))
    max_per_bin_dict = dict(zip(bin_labels, max_per_bin))

    return max_global, distribution, max_per_bin_dict


def print_distribution(distribution, max_per_bin):
    """打印音符区间分布及各区最大同时发声数"""
    total = sum(distribution.values())
    if total == 0:
        print("没有检测到任何音符。")
        return

    print("\n音符音域区间分布（次数 / 最大同时发声数）:")
    print("-" * 60)
    max_label_len = max(len(l) for l in distribution.keys())
    for label in distribution.keys():
        count = distribution[label]
        max_p = max_per_bin.get(label, 0)
        # 简易柱状图（基于总次数）
        bar = "#" * int(count / total * 40) if total else ""
        print(f"{label:>{max_label_len}s}: {count:5d} 次, 最大同时: {max_p:2d}  {bar}")
    print(f"{'总计':>{max_label_len}s}: {total:5d} 次")


# --------------------- 交互主程序 ---------------------
def main():
    print("=" * 60)
    print(" MIDI 分析工具：最大复音数 & 音域区间分布")
    print(" 用于集成蜂鸣器模块选型")
    print("=" * 60)

    while True:
        print("\n请输入 MIDI 文件路径（输入 q 退出）：", end=" ")
        filepath = input().strip().strip("'\"")

        if filepath.lower() in ('q', 'exit', 'quit'):
            break
        if not filepath:
            continue

        drum_choice = input("是否忽略打击乐通道(通道9)？(y/n，默认y)：").strip().lower()
        ignore_drums = drum_choice != 'n'

        custom = input("使用默认音域分段（<C3, C3–C4, C4–C5, C5–C6, C6–C7, C7–C8, ≥C8）吗？(y/n，默认y)：").strip().lower()
        if custom == 'n':
            bins_str = input("请输入音域边界（MIDI音符号），用逗号分隔，例如 48,60,72,84,96,108：")
            try:
                note_bins = [int(x.strip()) for x in bins_str.split(",")]
                if note_bins[0] != 0:
                    note_bins = [0] + note_bins
                if note_bins[-1] != 128:
                    note_bins.append(128)
                if len(note_bins) < 2:
                    raise ValueError
            except:
                print("格式错误，使用默认分段。")
                note_bins = None
        else:
            note_bins = None

        try:
            max_poly, dist, max_per_bin = max_polyphony_and_distribution(
                filepath, ignore_drums, note_bins
            )
            print(f"\n>>> 全局最大复音数（同时发声音符总数）: {max_poly}")
            print_distribution(dist, max_per_bin)

            if max_poly > 0:
                print(f"\n建议：至少需要 {max_poly} 路蜂鸣器驱动通道才能不丢失音符。")
                print("各区间的「最大同时发声数」决定了该频段至少需要的蜂鸣器数量。")
        except FileNotFoundError:
            print(f"错误：文件 '{filepath}' 未找到。")
        except mido.MidiError as e:
            print(f"MIDI 解析错误：{e}")
        except Exception as e:
            print(f"未知错误：{e}")

if __name__ == "__main__":
    main()