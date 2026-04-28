"""
============================================================
 PC 串口视频流发送器
 功能：将 MP4 视频通过串口实时发送到 STM32 OLED 播放
 协议：[0xAA][0x55][1024字节] → 等待 ACK(0xAC)
 依赖：Python 3 + Pillow + pyserial + FFmpeg
============================================================
"""

import subprocess
import os
import sys
import time
from PIL import Image

# ==================== 用户可配置参数 ====================

SERIAL_PORT = "COM10"
SERIAL_BAUDRATE = 921600
START_SEC = 0
DURATION_SEC = 0

# ==================== 固定参数（勿修改） ====================

W = 128
H = 64
FRAME_SIZE = 1024
SYNC0 = 0xAA
SYNC1 = 0x55
ACK    = 0xAC
TEMP_DIR = "_stream_temp"
DARK_THRESHOLD = 128


def run_ffmpeg(args):
    full = ["ffmpeg", "-y", "-loglevel", "error"] + args
    subprocess.run(full, check=True)


def process_frame(img):
    frame_bytes = bytearray(FRAME_SIZE)
    idx = 0
    for page in range(8):
        for col in range(128):
            byte_val = 0
            for bit in range(8):
                y = page * 8 + bit
                if img.getpixel((col, y)) == 0:
                    byte_val |= (1 << bit)
            frame_bytes[idx] = byte_val
            idx += 1
    return bytes(frame_bytes)


def extract_and_stream():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    temp_dir = os.path.join(script_dir, TEMP_DIR)

    print("=" * 60)
    print("  STM32 OLED 视频流播放器 — PC 串口发送端")
    print("=" * 60)
    print()
    print("提示：可以直接拖拽视频文件到此窗口，或输入文件路径。")
    print()

    while True:
        user_input = input("请输入视频文件路径: ").strip().strip('"').strip("'")
        if not user_input:
            print("未输入路径，已取消。")
            sys.exit(0)
        if os.path.isabs(user_input):
            video_path = user_input
        else:
            video_path = os.path.join(script_dir, user_input)
        if os.path.exists(video_path):
            print(f"找到视频: {video_path}\n")
            break
        else:
            print(f"错误: 找不到文件 \"{video_path}\"，请重新输入。\n")

    while True:
        fps_input = input("请输入播放帧率 (1~60, 推荐30, 直接回车=30fps): ").strip()
        if not fps_input:
            FPS = 30
            print(f"使用默认帧率: {FPS}fps\n")
            break
        try:
            FPS = int(fps_input)
            if 1 <= FPS <= 60:
                print(f"帧率设为: {FPS}fps\n")
                break
            else:
                print("帧率超出范围，请输入 1~60 之间的整数。")
        except ValueError:
            print("输入无效，请输入一个整数。")

    print("分析视频信息...")
    result = subprocess.run(
        ["ffprobe", "-v", "error", "-show_entries", "format=duration",
         "-of", "default=noprint_wrappers=1:nokey=1", video_path],
        capture_output=True, text=True
    )
    video_duration = float(result.stdout.strip())
    print(f"视频总时长: {video_duration:.1f} 秒")

    if DURATION_SEC > 0:
        play_duration = min(DURATION_SEC, video_duration - START_SEC)
    else:
        play_duration = video_duration - START_SEC
    play_duration = max(play_duration, 1)

    total_frames = int(play_duration * FPS)
    print(f"播放时长: {play_duration:.1f} 秒, 帧率: {FPS}fps, 总帧数: {total_frames}")

    os.makedirs(temp_dir, exist_ok=True)

    print(f"提取视频帧中... (起始 {START_SEC}s, 时长 {play_duration}s)")
    run_ffmpeg([
        "-ss", str(START_SEC),
        "-i", video_path,
        "-t", str(play_duration),
        "-vf", f"fps={FPS},scale={W}:{H}:force_original_aspect_ratio=increase,"
               f"crop={W}:{H}",
        os.path.join(temp_dir, "frame_%06d.png")
    ])

    png_files = sorted([f for f in os.listdir(temp_dir) if f.endswith(".png")])
    actual_frames = len(png_files)
    print(f"实际提取 {actual_frames} 帧")

    if actual_frames == 0:
        print("错误: 未提取到任何帧")
        sys.exit(1)

    print("预处理帧数据...")
    BAR_WIDTH = 40
    frame_cache = []
    for i, png_name in enumerate(png_files):
        png_path = os.path.join(temp_dir, png_name)
        img = Image.open(png_path).convert("L")
        if img.size != (W, H):
            img = img.resize((W, H), Image.LANCZOS)
        img = img.point(lambda x: 255 if x >= DARK_THRESHOLD else 0, "1")
        frame_data = process_frame(img)
        packet = bytes([SYNC0, SYNC1]) + frame_data
        frame_cache.append(packet)

        idx = i + 1
        pct = idx / actual_frames * 100
        filled = int(BAR_WIDTH * idx / actual_frames)
        if filled == 0:
            bar = ">" + " " * (BAR_WIDTH - 1)
        elif filled < BAR_WIDTH:
            bar = "=" * (filled - 1) + ">" + " " * (BAR_WIDTH - filled)
        else:
            bar = "=" * BAR_WIDTH
        print(f"\r  预处理: [{bar}] {pct:5.1f}%  ({idx}/{actual_frames})", end="", flush=True)
    print()

    print(f"\n打开串口 {SERIAL_PORT} @ {SERIAL_BAUDRATE} bps...")
    try:
        import serial
    except ImportError:
        print("错误: 需要 pyserial 模块，请执行: pip install pyserial")
        sys.exit(1)

    try:
        ser = serial.Serial(
            port=SERIAL_PORT, baudrate=SERIAL_BAUDRATE,
            bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE, timeout=5.0, write_timeout=5.0
        )
    except serial.SerialException as e:
        print(f"错误: 无法打开串口 {SERIAL_PORT}")
        print(f"详细信息: {e}")
        print("\n请检查：")
        print("  1. USB转串口是否正确连接")
        print("  2. 端口号是否正确（可在设备管理器中查看）")
        print("  3. STM32 是否已烧录新固件并上电")
        sys.exit(1)

    print("等待 STM32 就绪...")
    time.sleep(2.0)

    print(f"\n开始播放! 共 {actual_frames} 帧, 预计 {actual_frames / FPS:.1f} 秒")
    print("按 Ctrl+C 可随时停止\n")

    start_time = time.time()
    success_count = 0
    fail_count = 0
    ack_timeouts = 0

    try:
        for i, packet in enumerate(frame_cache):
            frame_start = time.time()
            try:
                ser.write(packet)
                ser.flush()
            except serial.SerialException:
                print(f"\n[帧 {i + 1}] 串口写入错误")
                break

            try:
                ack_byte = ser.read(1)
                if len(ack_byte) == 0:
                    ack_timeouts += 1
                    print(f"\n[帧 {i + 1}] ACK 超时 (已 {ack_timeouts} 次)")
                    if ack_timeouts >= 5:
                        print("连续 5 次 ACK 超时，停止发送")
                        break
                elif ack_byte[0] == ACK:
                    success_count += 1
                    ack_timeouts = 0
                else:
                    fail_count += 1
                    print(f"\n[帧 {i + 1}] 异常应答: 0x{ack_byte[0]:02X}")
            except serial.SerialException:
                print(f"\n[帧 {i + 1}] 串口读取错误")
                break

            idx = i + 1
            pct = idx / actual_frames * 100
            filled = int(BAR_WIDTH * idx / actual_frames)
            elapsed = time.time() - start_time
            if filled == 0:
                bar = ">" + " " * (BAR_WIDTH - 1)
            elif filled < BAR_WIDTH:
                bar = "=" * (filled - 1) + ">" + " " * (BAR_WIDTH - filled)
            else:
                bar = "=" * BAR_WIDTH
            real_fps = success_count / elapsed if elapsed > 0 else 0
            print(f"\r  播放中: [{bar}] {pct:5.1f}%  ({idx}/{actual_frames})  "
                  f"耗时 {elapsed:.0f}s  {real_fps:.1f}fps", end="", flush=True)

            frame_elapsed = time.time() - frame_start
            target_interval = 1.0 / FPS
            if frame_elapsed < target_interval:
                time.sleep(target_interval - frame_elapsed)

    except KeyboardInterrupt:
        print("\n\n用户中断播放")

    finally:
        ser.close()
        elapsed_total = time.time() - start_time
        print(f"\n--- 播放统计 ---")
        print(f"成功发送: {success_count} 帧")
        print(f"失败/异常: {fail_count} 帧")
        print(f"ACK 超时: {ack_timeouts} 次")
        print(f"总耗时: {elapsed_total:.1f} 秒")
        if success_count > 0:
            print(f"有效帧率: {success_count / elapsed_total:.1f} fps")

    for png_name in png_files:
        try:
            os.remove(os.path.join(temp_dir, png_name))
        except OSError:
            pass
    try:
        os.rmdir(temp_dir)
    except OSError:
        pass

    print("完成。")


if __name__ == "__main__":
    extract_and_stream()
