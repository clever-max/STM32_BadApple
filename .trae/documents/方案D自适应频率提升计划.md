# 方案D：自适应多声部频率提升 — 实施计划

## 背景

`bad_apple_explict.mid` 的低频分布经分析：

| 声部 | 音符数 | <200Hz | 典型低频 |
|------|--------|--------|---------|
| PA0 高声部 | 892 | 187 个 | **78Hz** |
| PA1 中声部 | 458 | 19 个 | **39Hz** |
| PA3 低声部 | 119 | 2 个 | 185/196Hz |

**多声部同时低频的唯一模式**：`(78, 39, 0)` — 16 次出现。
其余 176 次都是单声部低频（仅 PA0 或仅 PA1）。

核心问题：78Hz×2=156Hz，39Hz×4=156Hz → **两个声部撞到同一频率**。

---

## 方案D 算法

### 输入

阈值参数（通过 `--min-freq` / `--max-freq` 传入）：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `MIN_FREQ` | 200 | 蜂鸣器最低清晰频率 |
| `MAX_FREQ` | 3000 | 蜂鸣器最高有效频率 |

### 处理流程（对每个和弦 `(f0, f1, f2, dur)`）

```
Step 1. 各声部独立升至阈值以上
       对每个 fi > 0:
         while fi < MIN_FREQ: fi *= 2
         while fi > MAX_FREQ: fi //= 2

Step 2. 碰撞检测与消解
        将非零频率按升序排序 → sorted_freqs[]
        对 sorted_freqs 中相邻的相等项:
          将较高的原始声部再升一个八度 (×2)
          重复检测直到无碰撞

Step 3. 输出
        生成转换后的 (f0', f1', f2', dur)
```

### Step 2 详例

```
原始: (78, 39, 0)
Step1: 78×2=156, 39×4=156 → (156, 156, 0)
Step2: 检测到 156==156, PA1 再×2 → (156, 312, 0) ✓

原始: (78, 0, 0)     ← 单声部
Step1: 78×2=156 → (156, 0, 0) ✓   无碰撞

原始: (294, 220, 185) ← PA3 略低于阈值
Step1: 185×2=370 → (294, 220, 370)
Step2: 无碰撞 ✓
```

### 极端保护

- 如果声部升八度后超出 `MAX_FREQ`：降回，选择其他声部升八度
- 如果所有声部都超出上限：还原为仅升一次八度（接受碰撞，但不产生超声波）

---

## 代码改动

### 文件：`video/convert_score.py`

**改动点1：新增 `_transpose_chord` 函数 (~30行)**

```python
def _transpose_chord(freqs, min_freq, max_freq):
    """自适应多声部频率提升 + 碰撞消解"""
    # Step 1: 各声部独立升至[min_freq, max_freq]
    result = list(freqs)
    for i, f in enumerate(result):
        if f == 0:
            continue
        while f < min_freq:
            f *= 2
        while f > max_freq:
            f //= 2
        result[i] = f

    # Step 2: 碰撞消解
    # 将 (声部索引, 频率) 按频率降序排列
    # 如果相邻两项频率相同，将较低原始索引的声部升八度
    non_zero = [(i, result[i]) for i in range(len(result)) if result[i] > 0]
    non_zero.sort(key=lambda x: x[1], reverse=True)

    # 检测碰撞
    changed = True
    while changed:
        changed = False
        for j in range(len(non_zero) - 1):
            if non_zero[j][1] == non_zero[j + 1][1]:
                idx = non_zero[j + 1][0]  # 取较低的声部
                new_f = result[idx] * 2
                if new_f <= max_freq:
                    result[idx] = new_f
                    changed = True
                non_zero = [(i, result[i]) for i in range(len(result)) if result[i] > 0]
                non_zero.sort(key=lambda x: x[1], reverse=True)
                break  # 重新扫描

    return tuple(result)
```

**改动点2：在 `generate_chord_header` 调用前插入转换**

位置：`main()` 函数中，`voices > 1` 分支的 `generate_chord_header(...)` 调用前。

```python
if args.min_freq > 0 or args.max_freq > 0:
    min_f = args.min_freq if args.min_freq > 0 else 1
    max_f = args.max_freq if args.max_freq > 0 else 99999
    chords = [(_transpose_chord(freqs, min_f, max_f), dur) for freqs, dur in chords]
```

**改动点3：新增 CLI 参数**

```python
parser.add_argument("--min-freq", type=int, default=0,
                    help="蜂鸣器最低清晰频率(Hz), 低于此值自动升八度 (默认: 不转换)")
parser.add_argument("--max-freq", type=int, default=0,
                    help="蜂鸣器最高有效频率(Hz), 高于此值自动降八度 (默认: 不转换)")
```

---

## 使用方式

```bash
# 默认阈值 (min=200, max=3000)
python convert_score.py bad_apple_explict.mid --voices 3 --min-freq 200 --max-freq 3000

# 只检查不生成
python convert_score.py bad_apple_explict.mid --voices 3 --min-freq 200 --count-only

# 自定义阈值（如果你的蜂鸣器甜蜜区不同）
python convert_score.py bad_apple_explict.mid --voices 3 --min-freq 300 --max-freq 2500
```

---

## 验证

| 检查项 | 方法 |
|--------|------|
| 无碰撞 | `assert len(set(f for f in chord if f>0)) == count_nonzero` |
| 无超范围 | `assert all(f==0 or min_freq<=f<=max_freq for f in chord)` |
| 声部保持 | 每个声部经升八度后 MIDI note number mod 12 不变（音高class不变） |

---

## 不改动的部分

- `audio_pwm.c/h` — 无需改动，`StartChord` 接受任意频率
- `video_stream.c` — 无需改动
- `User/main.c` — 无需改动
- 已生成的 `Hardware/music_score.h` — 需重新运行 `convert_score.py` 生成

---

## 预估工作量

| 步骤 | 内容 | 行数 |
|------|------|------|
| 1 | `_transpose_chord` 函数 | ~30 | 
| 2 | CLI 参数 `--min-freq` `--max-freq` | ~5 |
| 3 | `main()` 中调用转换 | ~5 |
| 4 | 验证测试 | ~10 |
| **合计** | | **~50 行** |
