"""
============================================================
 PC 串口视频流发送器  (双蜂鸣器音频同步版 · 交互式)
============================================================
 功能：将 MP4 视频通过串口实时发送到 STM32 OLED 播放，
       MCU 端首帧到达时自动触发蜂鸣器音乐同步播放。

 协议：[0xAA][0x55][1024字节] → 等待 ACK(0xAC)
 命令：帧间隙发送单字节 → MCU 热切换和声/音量

 键盘快捷键 (播放中):
     h     → 切换和声模式 (OFF → UNI → OCT → 5TH)
     +/-/= → 音量 +/- 10%
     s     → 显示当前状态
     q/ESC → 停止播放并退出

 依赖：Python 3 + Pillow + pyserial + FFmpeg

 使用：python pc_streamer.py [COM端口]
       如：python pc_streamer.py COM10
============================================================
"""

import subprocess
import os
import sys
import time
import threading
from PIL import Image

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("错误: 需要 pyserial 模块，请执行: pip install pyserial")
    sys.exit(1)

# ── 平台适配：单键读取 ──
import platform
IS_WINDOWS = platform.system() == 'Windows'

if IS_WINDOWS:
    import msvcrt
    def _getch():
        return msvcrt.getch().decode('utf-8', errors='ignore')
    def _kbhit():
        return msvcrt.kbhit()
else:
    import termios, tty, select as sel
    def _getch():
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            ch = sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
        return ch
    def _kbhit():
        fd = sys.stdin.fileno()
        return sel.select([fd], [], [], 0) == ([fd], [], [])

# ==================== 用户可配置参数 ====================

SERIAL_BAUDRATE = 921600
START_SEC = 0
DURATION_SEC = 0

# ==================== 固定参数 ====================

W = 128
H = 64
FRAME_SIZE = 1024
SYNC0 = 0xAA
SYNC1 = 0x55
ACK    = 0xAC
TEMP_DIR = "_stream_temp"
DARK_THRESHOLD = 128

CMD_CHARS = {'h', 'H', '+', '=', '-', '_', 's', 'S', 'q', '\x1b'}

HEADER = """
╔══════════════════════════════════════════════════════════╗
║     STM32 OLED 视频流 + 双蜂鸣器音频同步播放器         ║
║     PC 串口发送端  v2.1  交互式                        ║
╚══════════════════════════════════════════════════════════╝"""

HARMONY_NAMES = ["OFF", "UNI", "OCT", "5TH"]

# ==================== 共享状态 ====================

class State:
    def __init__(self):
        self.harmony_idx = 1       # 0=OFF, 1=UNI, 2=OCT, 3=5TH
        self.volume = 50
        self.pending_cmd = None    # 待发送到 MCU 的命令字节
        self.lock = threading.Lock()

state = State()


# ==================== 键盘监听线程 ====================

def keyboard_thread(stop_event):
    """后台线程：监听键盘，更新本地状态和待发送命令"""
    global state
    print("\n  键盘就绪: [h]和声 [+/-]音量 [s]状态 [q]退出\n")
    while not stop_event.is_set():
        if _kbhit():
            ch = _getch()
            with state.lock:
                if ch.lower() == 'q' or ch == '\x1b':
                    state.pending_cmd = 'q'
                    break
                elif ch.lower() == 'h':
                    state.harmony_idx = (state.harmony_idx + 1) % 4
                    state.pending_cmd = 'h'
                    print(f"\r  » 和声: {HARMONY_NAMES[state.harmony_idx]:<4} ", end="", flush=True)
                elif ch in ('+', '='):
                    state.volume = min(100, state.volume + 10)
                    state.pending_cmd = '+'
                    print(f"\r  » 音量: {state.volume}%  ", end="", flush=True)
                elif ch in ('-', '_'):
                    state.volume = max(0, state.volume - 10)
                    state.pending_cmd = '-'
                    print(f"\r  » 音量: {state.volume}%  ", end="", flush=True)
                elif ch.lower() == 's':
                    print(f"\r  » 状态: 和声={HARMONY_NAMES[state.harmony_idx]}, "
                          f"音量={state.volume}%  ", flush=True)
        time.sleep(0.02)


# ==================== 串口工具 ====================

def list_ports():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("⚠  没有检测到串口。请检查 USB-TTL 连接。")
        return None
    print("\n可用串口:")
    print("-" * 55)
    print(f"  {'端口':<10} {'VID:PID':<14} {'描述'}")
    print("-" * 55)
    for i, p in enumerate(ports):
        vid_pid = f"{p.vid:04X}:{p.pid:04X}" if p.vid and p.pid else "N/A"
        marker = " ← 推荐" if (p.vid == 0x1A86 or p.vid == 0x10C4 or p.vid == 0x0403) else ""
        print(f"  [{i}] {p.device:<8} {vid_pid:<14} {p.description}{marker}")
    print("-" * 55)
    return ports


def auto_find_port():
    ports = serial.tools.list_ports.comports()
    if not ports:
        return None
    if len(ports) == 1:
        return ports[0].device
    for p in ports:
        if p.vid == 0x1A86 and p.pid == 0x7523:
            return p.device
    for p in ports:
        if p.vid == 0x10C4:
            return p.device
    for p in ports:
        if p.vid == 0x0403:
            return p.device
    return None


def select_port():
    ports = list_ports()
    if not ports:
        return None
    auto = auto_find_port()
    if auto:
        print(f"\n自动检测到: {auto}")
        ans = input("直接回车使用此端口，或输入序号选择其他: ").strip()
        if ans == "":
            return auto
        try:
            idx = int(ans)
            if 0 <= idx < len(ports):
                return ports[idx].device
        except ValueError:
            pass
    ans = input("\n请输入序号或 COM 端口号 (如 COM3): ").strip()
    if not ans:
        return None
    try:
        idx = int(ans)
        if 0 <= idx < len(ports):
            return ports[idx].device
    except ValueError:
        pass
    if ans.upper().startswith("COM"):
        return ans.upper()
    return ans


# ==================== 帧处理 ====================

def run_ffmpeg(args):
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error"] + args, check=True)


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


def progress_bar(percent, width=40):
    filled = int(width * percent / 100)
    if filled == 0:
        bar = ">" + " " * (width - 1)
    elif filled < width:
        bar = "=" * (filled - 1) + ">" + " " * (width - filled)
    else:
        bar = "=" * width
    return f"[{bar}]"


# ==================== 主流程 ====================

def extract_and_stream(force_port=None):
    global state

    print(HEADER)
    print("\n  提示: 可以直接拖拽视频文件到此窗口，或输入文件路径。")
    print("        MCU 首帧到达时自动触发蜂鸣器。")
    print("        播放中按 [h]切换和声 [+/-]调音量 [q]退出")
    print()

    # ── 选择串口 ──
    if force_port:
        port = force_port
        print(f"使用指定端口: {port}\n")
    else:
        port = select_port()
        if not port:
            print("未选择端口，退出。")
            sys.exit(0)
        print(f"使用端口: {port}\n")

    # ── 和声预设 ──
    while True:
        hm = input("选择初始和声模式 (1=UNI 2=OCT 3=5TH 0=OFF, 直接回车=UNI): ").strip()
        if not hm:
            state.harmony_idx = 1
            break
        if hm in ('0', '1', '2', '3'):
            state.harmony_idx = int(hm)
            break
        print("请输入 0~3")

    # ── 音量预设 ──
    while True:
        vol = input("选择初始音量 (10~100, 直接回车=50): ").strip()
        if not vol:
            state.volume = 50
            break
        try:
            v = int(vol)
            if 10 <= v <= 100:
                state.volume = v
                break
            else:
                print("请输入 10~100")
        except ValueError:
            print("请输入整数")

    print(f"和声: {HARMONY_NAMES[state.harmony_idx]},  音量: {state.volume}%")
    print()

    # ── 选择视频文件 ──
    script_dir = os.path.dirname(os.path.abspath(__file__))
    temp_dir = os.path.join(script_dir, TEMP_DIR)

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

    # ── 帧率 ──
    while True:
        fps_input = input("请输入播放帧率 (1~60, 推荐30fps, 直接回车=30fps): ").strip()
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

    # ── 视频信息 ──
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
    print(f"音频需约: {play_duration:.0f} 秒 → 请确保 music_score.h 时长匹配")
    print()

    os.makedirs(temp_dir, exist_ok=True)

    # ── 提取视频帧 ──
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

    # ── 预处理帧数据 ──
    print("预处理帧数据...")
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
        bar = progress_bar(pct)
        print(f"\r  预处理: {bar} {pct:5.1f}%  ({idx}/{actual_frames})", end="", flush=True)
    print()

    # ── 打开串口 ──
    print(f"\n打开串口 {port} @ {SERIAL_BAUDRATE} bps...")
    try:
        ser = serial.Serial(
            port=port, baudrate=SERIAL_BAUDRATE,
            bytesize=serial.EIGHTBITS, parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE, timeout=0.05, write_timeout=0.1
        )
    except serial.SerialException as e:
        print(f"错误: 无法打开串口 {port}")
        print(f"详细信息: {e}")
        print("\n请检查：")
        print("  1. USB转串口是否正确连接")
        print("  2. 端口号是否正确（可在设备管理器中查看）")
        print("  3. STM32 是否已烧录新固件并上电")
        sys.exit(1)

    # ── 发送初始音量 + 和声到 MCU ──
    print("等待 STM32 就绪 + 同步初始设置...")
    time.sleep(2.0)
    init_cmds = []
    for _ in range(state.harmony_idx):
        init_cmds.append(b'h')
    for _ in range((state.volume - 50) // 10):
        init_cmds.append(b'+')
    for _ in range((50 - state.volume) // 10):
        init_cmds.append(b'-')
    for c in init_cmds:
        ser.write(c)
        time.sleep(0.02)
    print(f"初始和声: {HARMONY_NAMES[state.harmony_idx]}, 音量: {state.volume}%")

    # ── 启动键盘监听线程 ──
    kb_stop = threading.Event()
    kb_t = threading.Thread(target=keyboard_thread, args=(kb_stop,), daemon=True)
    kb_t.start()

    print(f"\n{'='*60}")
    print(f"  开始播放!")
    print(f"  共 {actual_frames} 帧, 预计 {actual_frames / FPS:.1f} 秒")
    print(f"  [h] 和声   [+/-] 音量   [s] 状态   [q] 退出")
    print(f"{'='*60}\n")

    start_time = time.time()
    success_count = 0
    fail_count = 0
    ack_timeouts = 0

    try:
        for i, packet in enumerate(frame_cache):
            frame_start = time.time()

            # ── 先检查待发送命令 ──
            with state.lock:
                cmd = state.pending_cmd
                state.pending_cmd = None

            if cmd == 'q':
                print("\n用户退出播放")
                break
            elif cmd in ('h', '+', '-', '=', '_'):
                try:
                    ser.write(cmd.encode() if isinstance(cmd, str) else cmd)
                except serial.SerialException:
                    pass

            # ── 发送视频帧 ──
            try:
                ser.write(packet)
                ser.flush()
            except serial.SerialException:
                print(f"\n[帧 {i + 1}] 串口写入错误")
                break

            # ── 等待 ACK ──
            try:
                ack_byte = ser.read(1)
                if len(ack_byte) == 0:
                    ack_timeouts += 1
                    if ack_timeouts >= 5:
                        print(f"\n连续 {ack_timeouts} 次 ACK 超时，停止发送")
                        break
                elif ack_byte[0] == ACK:
                    success_count += 1
                    ack_timeouts = 0
                else:
                    fail_count += 1
            except serial.SerialException:
                print(f"\n[帧 {i + 1}] 串口读取错误")
                break

            # ── 进度条 ──
            idx = i + 1
            pct = idx / actual_frames * 100
            bar = progress_bar(pct)
            elapsed = time.time() - start_time
            real_fps = success_count / elapsed if elapsed > 0 else 0
            print(f"\r  播放: {bar} {pct:5.1f}% {idx}/{actual_frames} "
                  f"{elapsed:.0f}s {real_fps:.1f}fps "
                  f"│ {HARMONY_NAMES[state.harmony_idx]} vol={state.volume}% │ ",
                  end="", flush=True)

            # ── 帧率控制 ──
            frame_elapsed = time.time() - frame_start
            target_interval = 1.0 / FPS
            if frame_elapsed < target_interval:
                time.sleep(target_interval - frame_elapsed)

    except KeyboardInterrupt:
        print("\n\n用户中断播放")

    finally:
        kb_stop.set()
        kb_t.join(timeout=0.5)
        ser.close()
        elapsed_total = time.time() - start_time
        print(f"\n\n{'─'*45}")
        print(f"  播放统计")
        print(f"{'─'*45}")
        print(f"  成功发送: {success_count} 帧")
        print(f"  失败/异常: {fail_count} 帧")
        print(f"  ACK 超时: {ack_timeouts} 次")
        print(f"  总耗时: {elapsed_total:.1f} 秒")
        if success_count > 0:
            print(f"  有效帧率: {success_count / elapsed_total:.1f} fps")
        print(f"{'─'*45}")

    # ── 清理临时文件 ──
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
    import argparse
    parser = argparse.ArgumentParser(description="PC 串口视频流发送器 (交互式)")
    parser.add_argument("port", nargs="?", default=None,
                        help="COM 端口号 (如 COM10), 不指定则自动扫描")
    args = parser.parse_args()

    port = args.port.upper() if args.port else None
    extract_and_stream(force_port=port)
