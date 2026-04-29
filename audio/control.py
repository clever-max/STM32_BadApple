"""
audio_control.py — STM32 音乐播放器串口遥控器
==============================================
通过串口发送单字符命令控制 STM32 AudioPlayer。
支持多位数选歌 (如 12 + Enter → 第12首)。

命令:
  <数字>    → 输入歌曲编号 (按 Enter 确认)
  Enter     → 确认/播放输入的歌曲编号
  n         → 下一首
  p         → 上一首
  s         → 停止
  Space     → 播放/暂停
  + / -     → 音量 ±10%
  h         → 切换和声 (OFF→UNI→OCT→5TH)
  v         → 切换声道 (1CH→2CH→3CH)
  l         → 列出所有歌曲
  q / ESC   → 退出程序

使用:  python audio_control.py [COM端口]
如:    python audio_control.py COM3
依赖:  pip install pyserial
"""

import sys
import platform
import time
import threading

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("错误: 需要 pyserial 模块，请执行: pip install pyserial")
    sys.exit(1)

# ── 平台适配：单键读取 ──
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
            return sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
    def _kbhit():
        fd = sys.stdin.fileno()
        return sel.select([fd], [], [], 0) == ([fd], [], [])

# ── 串口相关 ──
BAUDRATE = 115200
TIMEOUT  = 0.05
SONG_COUNT = 2   # 可在运行时修改

HELP_TEXT = """
╔══════════════════════════════════════╗
║    STM32 Audio Player 遥控器         ║
╚══════════════════════════════════════╝
  <数字> + Enter 选歌    [n] 下一首
  [p]      上一首          [Space] 暂停/继续
  [s]      停止            [+]/[-] 音量±10%
  [h]      和声切换         [v]  声道切换
  [l]      列出歌曲
  [q]      退出
────────────────────────────────────────
  按键即发，无需回车
  选歌: 输入编号后按 Enter (或等 1 秒自动确认)
"""


def list_ports():
    ports = serial.tools.list_ports.comports()
    if not ports:
        print("⚠ 没有检测到串口。")
        return None
    print("\n可用串口:")
    print("-" * 45)
    print(f"{'端口':<12} {'描述'}")
    print("-" * 45)
    for p in ports:
        print(f"{p.device:<12} {p.description}")
    return ports


def auto_find_port():
    ports = serial.tools.list_ports.comports()
    if not ports:
        return None
    if len(ports) == 1:
        return ports[0].device
    for p in ports:
        if p.vid in (0x1A86, 0x10C4, 0x0403):
            return p.device
    return None


def reader_thread(ser, stop_event):
    buf = b''
    while not stop_event.is_set():
        try:
            data = ser.read(128)
            if data:
                buf += data
                while b'\n' in buf:
                    line, buf = buf.split(b'\n', 1)
                    text = line.replace(b'\r', b'').decode('utf-8', errors='replace')
                    print(f"\r  [STM32] {text}   ", end="", flush=True)
                    print()
            else:
                time.sleep(0.01)
        except (serial.SerialException, OSError):
            break


def interactive(ser):
    stop_event = threading.Event()
    t = threading.Thread(target=reader_thread, args=(ser, stop_event), daemon=True)
    t.start()

    print(HELP_TEXT)
    print(f"  (共 {SONG_COUNT} 首歌)")

    num_acc = ""        # 数字缓冲
    num_last = 0.0      # 最后按键时间

    while True:
        now = time.time()

        # 数字缓冲超时自动发送 Enter
        if num_acc and (now - num_last) > 1.0:
            ser.write(b'\r')
            print(f"\r  » 选歌: {num_acc} (自动确认)", flush=True)
            num_acc = ""

        if _kbhit():
            ch = _getch()

            # 数字: 累积
            if ch.isdigit():
                num_acc += ch
                num_last = now
                ser.write(ch.encode())
                print(f"\r  » 输入: {num_acc}   ", end="", flush=True)
                continue

            # Backspace: 删除最后一个数字
            if ch in ('\b', '\x7f'):
                if num_acc:
                    num_acc = num_acc[:-1]
                    num_last = now
                    # 发送 Backspace 通知 MCU 清缓冲 (MCU 不支持回退, 但超时后 Enter 会下发新数)
                    # 实际上 MCU 每次只认最后收到的数字, 回退需要在 PC 端处理
                    # 更好的方法: 重新发送完整数字 (先清 MCU 缓冲再发)
                    ser.write(b'\r')          # 先确认之前的 (MCU 会执行并清缓冲)
                    ser.write(b's')            # 停止
                    time.sleep(0.05)
                    for d in num_acc:          # 重发剩余数字
                        ser.write(d.encode())
                        time.sleep(0.02)
                    print(f"\r  » 输入: {num_acc or '(空)'}   ", end="", flush=True)
                continue

            # Enter: 确认选歌
            if ch in ('\r', '\n'):
                if num_acc:
                    ser.write(b'\r')
                    print(f"\r  » 选歌: {num_acc}", flush=True)
                    num_acc = ""
                continue

            # q/ESC: 退出
            if ch.lower() == 'q' or ch == '\x1b':
                ser.write(b'q')
                print("\n退出。")
                break

            # 非数字命令: 清数字缓冲并发命令
            if num_acc:
                num_acc = ""
            ser.write(ch.encode())
            display = ch.replace('\r', '<CR>').replace('\n', '<LF>').replace(' ', '<Space>')
            print(f"\r  » 发送: '{display}'   ", end="", flush=True)
            # 换行显示
            print()

        time.sleep(0.02)

    stop_event.set()
    t.join(timeout=0.5)


def main():
    global SONG_COUNT
    if len(sys.argv) > 1:
        port_name = sys.argv[1]
    else:
        port_name = auto_find_port()
        if port_name is None:
            list_ports()
            print()
            port_name = input("请输入 COM 端口号 (如 COM3): ").strip()
            if not port_name:
                print("未输入端口，退出。")
                sys.exit(1)

    # 可选参数: 指定歌曲数量
    if len(sys.argv) > 2:
        SONG_COUNT = int(sys.argv[2])

    print(f"连接 {port_name} @ {BAUDRATE} baud ...", end='', flush=True)

    try:
        ser = serial.Serial(port_name, BAUDRATE, timeout=TIMEOUT)
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        print(" 已连接!")
        interactive(ser)
    except serial.SerialException as e:
        print(f"\n✗ 打开串口失败: {e}")
        list_ports()
        sys.exit(1)
    finally:
        if ser and ser.is_open:
            ser.close()
            print("串口已关闭。")


if __name__ == '__main__':
    main()
