"""
============================================================
 视频帧提取与转换工具
 功能：将 MP4 视频转换为 STM32 OLED 屏幕可用的 C 语言数组
 输出：video_frames.h / video_frames.c
 依赖：Python 3 + Pillow (PIL) + FFmpeg
============================================================
"""

import subprocess
import os
import sys
from PIL import Image

# ==================== 用户可配置参数 ====================

# 视频文件名（需放在本脚本同目录下）
VIDEO_PATH = "3724723-1-208.mp4"

# 输出文件名
OUTPUT_H = "video_frames.h"
OUTPUT_C = "video_frames.c"

# 最大帧数（受 STM32F103C8 64KB Flash 限制，建议 ≤60）
MAX_FRAMES = 60

# 播放帧率（FPS）
FPS = 10

# 视频起始时间偏移（单位：秒，0 表示从头开始）
START_SEC = 0

# ==================== 固定参数（勿修改） ====================

# OLED 屏幕分辨率
W = 128
H = 64

# 临时帧图片存放目录
TEMP_DIR = "_frames_temp"


def run_ffmpeg(args):
    """调用 FFmpeg 命令行工具，执行视频处理操作"""
    full = ["ffmpeg", "-y", "-loglevel", "error"] + args
    subprocess.run(full, check=True)


def main():
    """主函数：完成视频→帧图片→C 数组的完整转换流程"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    video_path = os.path.join(script_dir, VIDEO_PATH)
    output_h = os.path.join(script_dir, OUTPUT_H)
    output_c = os.path.join(script_dir, OUTPUT_C)
    temp_dir = os.path.join(script_dir, TEMP_DIR)

    # 1. 检查视频文件是否存在
    if not os.path.exists(video_path):
        print(f"错误: 找不到视频文件 {video_path}")
        sys.exit(1)

    # 2. 创建临时目录
    os.makedirs(temp_dir, exist_ok=True)

    # 3. 计算提取的视频片段长度
    duration = MAX_FRAMES / FPS
    print(f"提取视频片段: 起始 {START_SEC}s, 时长 {duration}s, {FPS}fps, 最多 {MAX_FRAMES} 帧")

    # 4. 使用 FFmpeg 提取帧：
    #    -ss: 起始时间
    #    -t:  持续时间
    #    -vf: 视频滤镜链（fps 控制帧率，scale 缩放为 128x64 并保持比例，
    #         pad 居中填充黑边）
    run_ffmpeg([
        "-ss", str(START_SEC),
        "-i", video_path,
        "-t", str(duration),
        "-vf", f"fps={FPS},scale={W}:{H}:force_original_aspect_ratio=increase,"
               f"crop={W}:{H}",
        os.path.join(temp_dir, "frame_%04d.png")
    ])

    # 5. 获取所有生成的帧图片文件，按文件名排序
    png_files = sorted([f for f in os.listdir(temp_dir) if f.endswith(".png")])
    frame_count = len(png_files)
    print(f"生成 {frame_count} 个帧文件")

    if frame_count == 0:
        print("错误: 未提取到任何帧")
        sys.exit(1)

    all_frame_data = []

    # 6. 逐帧处理：二值化 → 按 OLED 显存格式重组字节
    for png_name in png_files:
        png_path = os.path.join(temp_dir, png_name)
        # 打开图片并转为灰度模式
        img = Image.open(png_path).convert("L")

        # 如果尺寸不匹配则缩放（正常情况下 FFmpeg 已处理，此为保险）
        if img.size != (W, H):
            img = img.resize((W, H), Image.LANCZOS)

        # 二值化：灰度 ≥128 → 白(255)，<128 → 黑(0)
        # 转为 1-bit 模式（True=白, False=黑）
        img = img.point(lambda x: 255 if x >= 128 else 0, "1")

        # ---------------------------------------------------------------
        # OLED 显存数据格式说明：
        # 纵向 8 个像素点为一组（对应一个字节），高位在下
        # 遍历顺序：先从左到右（列，0~127），再从上到下（页，0~7）
        #
        #   显存布局示意：
        #     B0 B0  ...              B0 B0
        #     B1 B1  ...              B1 B1
        #     ...                     ...
        #     B7 B7  ...              B7 B7
        #     <------ 128 列 ---------->
        #     以上为一页（共 8 页）
        # ---------------------------------------------------------------
        frame_bytes = []
        for page in range(8):           # 遍历 8 页（每页 8 行像素）
            for col in range(128):       # 遍历 128 列
                byte_val = 0
                for bit in range(8):     # 每页内遍历 8 个位（纵向 8 像素）
                    y = page * 8 + bit   # 计算实际 Y 坐标
                    if y < H:
                        pixel = img.getpixel((col, y))
                        # 黑色像素 = 0（False）→ 对应位设为 1（点亮）
                        # 白色像素 = 255（True）→ 对应位设为 0（熄灭）
                        if pixel == 0:
                            byte_val |= (1 << bit)
                frame_bytes.append(byte_val)

        all_frame_data.append(frame_bytes)

    # 7. 生成头文件 video_frames.h
    with open(output_h, "w", encoding="utf-8") as f:
        f.write("#ifndef __VIDEO_FRAMES_H\n")
        f.write("#define __VIDEO_FRAMES_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("/* ---- 视频帧参数宏定义 ---- */\n")
        f.write(f"#define VIDEO_FRAME_COUNT  {frame_count}       /* 总帧数 */\n")
        f.write(f"#define VIDEO_FRAME_WIDTH  {W}                 /* 帧宽度（像素） */\n")
        f.write(f"#define VIDEO_FRAME_HEIGHT {H}                 /* 帧高度（像素） */\n")
        f.write(f"#define VIDEO_PLAY_FPS    {FPS}                /* 播放帧率 */\n")
        f.write(f"#define VIDEO_FRAME_SIZE  1024                /* 单帧字节数 */\n\n")
        f.write("/* 视频帧数据数组（存储在 Flash 中） */\n")
        f.write("extern const uint8_t video_frames[][VIDEO_FRAME_SIZE];\n\n")
        f.write("#endif\n")

    # 8. 生成源文件 video_frames.c
    with open(output_c, "w", encoding="utf-8") as f:
        f.write('/*\n')
        f.write(' * 视频帧数据（由 convert_video.py 自动生成，请勿手动修改）\n')
        f.write(' * 数据格式：纵向 8 点/字节，高位在下，先列后页\n')
        f.write(f' * 帧数：{frame_count}，分辨率：{W}x{H}，帧率：{FPS}fps\n')
        f.write(' */\n\n')
        f.write('#include "video_frames.h"\n\n')
        f.write("const uint8_t video_frames[][VIDEO_FRAME_SIZE] = {\n")
        for idx, frame in enumerate(all_frame_data):
            f.write("    {\n        ")
            line_parts = []
            for b in frame:
                line_parts.append(f"0x{b:02X}")
            # 每行 16 个字节，便于阅读
            for i in range(0, len(line_parts), 16):
                f.write(", ".join(line_parts[i:i+16]))
                if i + 16 < len(line_parts):
                    f.write(",\n        ")
            f.write("\n    }")
            if idx < len(all_frame_data) - 1:
                f.write(",")
            f.write("\n")
        f.write("};\n")

    # 9. 清理临时文件
    for png_name in png_files:
        os.remove(os.path.join(temp_dir, png_name))
    os.rmdir(temp_dir)

    print(f"完成! 生成 {frame_count} 帧, 共 {frame_count * 1024} 字节 ({frame_count}KB)")
    print(f"Flash 占用预估: 代码约 3KB + 帧数据 {frame_count}KB = 约 {3 + frame_count}KB / 64KB")
    print(f"输出文件: {output_h}, {output_c}")


if __name__ == "__main__":
    main()
