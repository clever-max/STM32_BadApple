"""
buzzer_control.py — STM32 蜂鸣器串口遥控器
===========================================
通过串口发送单字符命令控制 STM32 上的蜂鸣器测试程序。
支持单键触发，无需按回车。

命令:
  1  → 音阶测试 (C4→C5→C4)
  2  → 小星星
  3  → Bad Apple 完整版
  s  → 停止当前播放
  h  → 切换和声模式 (OFF/UNI/OCT/5TH)
  q  → 退出程序

使用:  python buzzer_control.py [COM端口]
如:    python buzzer_control.py COM3

依赖:  pip install pyserial
"""

import sys
import platform
import time
import threading
import serial
import serial.tools.list_ports

# ── 平台适配：单键读取 ──
IS_WINDOWS = platform.system() == 'Windows'

if IS_WINDOWS:
    import msvcrt
    def _getch():
        """Windows: 读取单个按键 (阻塞)"""
        return msvcrt.getch().decode('utf-8', errors='ignore')

    def _kbhit():
        """Windows: 检查是否有按键"""
        return msvcrt.kbhit()
else:
    import termios
    import tty
    import select

    def _getch():
        """Linux/macOS: 读取单个按键 (阻塞)"""
        fd = sys.stdin.fileno()
        old = termios.tcgetattr(fd)
        try:
            tty.setraw(fd)
            ch = sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old)
        return ch

    def _kbhit():
        """Linux/macOS: 检查是否有按键 (非阻塞)"""
        fd = sys.stdin.fileno()
        return select.select([fd], [], [], 0) == ([fd], [], [])

# ── 串口相关 ──
BAUDRATE = 115200
TIMEOUT  = 0.05   # 串口读取超时 (秒)，50ms 足够非阻塞轮询
CMD_CHARS = {'1', '2', '3', 's', 'q', '+', '=', '-', '_', 'h', 'H'}


def list_ports():
    """列出系统中的可用串口"""
    ports = serial.tools.list_ports.comports()

    if not ports:
        print("⚠ 没有检测到串口。请检查 USB-TTL 连接。")
        return

    print("可用串口:")
    print("-" * 45)
    print(f"{'端口':<12} {'VID:PID':<14} {'描述'}")
    print("-" * 45)
    for p in ports:
        vid_pid = f"{p.vid:04X}:{p.pid:04X}" if p.vid and p.pid else "N/A"
        print(f"{p.device:<12} {vid_pid:<14} {p.description}")


def auto_find_port():
    """自动查找串口：只有一个时直接使用，否则让用户选"""
    ports = serial.tools.list_ports.comports()
    if not ports:
        return None
    if len(ports) == 1:
        return ports[0].device

    # CH340 优先 (常见 USB-TTL)
    for p in ports:
        if p.vid == 0x1A86 and p.pid == 0x7523:
            return p.device
    # CP210x
    for p in ports:
        if p.vid == 0x10C4:
            return p.device
    # FTDI
    for p in ports:
        if p.vid == 0x0403:
            return p.device

    return None  # 多个但都没匹配


def reader_thread(ser, stop_event):
    """后台线程：持续从串口读取 STM32 发来的文本并打印"""
    buf = b''
    while not stop_event.is_set():
        try:
            data = ser.read(128)
            if data:
                buf += data
                # 遇到换行就输出一行
                while b'\n' in buf:
                    line, buf = buf.split(b'\n', 1)
                    # 去掉 \r
                    text = line.replace(b'\r', b'').decode('utf-8', errors='replace')
                    print(text, flush=True)
            else:
                time.sleep(0.01)
        except (serial.SerialException, OSError):
            break


def interactive(ser):
    """交互式主循环：显示界面 + 处理按键"""
    stop_event = threading.Event()

    # 启动后台读取线程
    t = threading.Thread(target=reader_thread, args=(ser, stop_event), daemon=True)
    t.start()

    print()
    print("=" * 40)
    print("   STM32 Buzzer Control Panel")
    print("=" * 40)
    print("  [1] 音阶测试    (C4 → C5 → C4)")
    print("  [2] 小星星      (Twinkle Twinkle)")
    print("  [3] Bad Apple   (739 音符)")
    print("  [+]/[=] 音量+10%    [-] 音量-10%")
    print("  [h] 切换和声   (OFF→UNI→OCT→5TH)")
    print("  [s] 停止播放    [q] 退出")
    print("=" * 40)
    print("提示: 直接按键即可发送，无需回车")
    print()

    while True:
        if _kbhit():
            ch = _getch().lower()

            if ch == 'q':
                print("\n退出。")
                break
            elif ch in CMD_CHARS:
                ser.write(ch.encode())
                print(f"  » 已发送: '{ch}'", flush=True)
            # 忽略其他按键

        time.sleep(0.02)

    stop_event.set()
    t.join(timeout=0.5)


def main():
    # 解析命令行参数
    if len(sys.argv) > 1:
        port_name = sys.argv[1]
    else:
        port_name = auto_find_port()
        if port_name is None:
            print("未指定端口，扫描结果:")
            list_ports()
            print()
            port_name = input("请输入 COM 端口号 (如 COM3): ").strip()
            if not port_name:
                print("未输入端口，退出。")
                sys.exit(1)

    print(f"连接 {port_name} @ {BAUDRATE} baud ...", end='', flush=True)

    ser = None
    try:
        ser = serial.Serial(port_name, BAUDRATE, timeout=TIMEOUT)
        # 清空缓冲区
        ser.reset_input_buffer()
        ser.reset_output_buffer()
        print(" 已连接!")
        interactive(ser)
    except serial.SerialException as e:
        print(f"\n✗ 打开串口失败: {e}")
        print("\n常见原因:")
        print("  1. 串口被其他程序占用 (检查串口助手是否已关闭)")
        print("  2. COM 端口号不正确")
        print("  3. 驱动程序未安装 (CH340/CP210x 驱动)")
        list_ports()
        sys.exit(1)
    except KeyboardInterrupt:
        print("\n中断退出。")
    finally:
        if ser and ser.is_open:
            ser.close()
            print("串口已关闭。")


if __name__ == '__main__':
    main()
