"""生成键控对拍的测试数据, 并比较 C++ 与 Python 的输出。

流程:
    Python 解一帧 -> 保存 RGB 原始数据 -> 用 Python 版键控 -> 保存 RGBA
    再调 C++ 版 keying_parity 处理同一张 RGB -> 保存 RGBA
    比两张 RGBA 的差异(主要是 alpha 通道)

判据: 像素差 > 2 的比例应 < 0.5%, 平均差 < 0.5 (只差在舍入上)。
"""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(r"D:\teachergeng\RemoveGreen")
sys.path.insert(0, str(ROOT))
WORK = ROOT / "native" / "_parity"
EXE = WORK / "keying_parity.exe"

from app.core import ffmpeg as ff                     # noqa: E402
from app.core import keying as K                      # noqa: E402
from app.core.frames import decode_frame              # noqa: E402

CASES = [
    # (名称, 相似度, blend, 羽化, 溢色强度)
    ("plain", 0.30, 0.15, 0.0, 0.85),
    ("feather", 0.30, 0.15, 1.5, 0.85),
    ("wide", 0.45, 0.30, 0.0, 1.00),
]


def main() -> int:
    WORK.mkdir(parents=True, exist_ok=True)
    info = ff.probe(ROOT / "test_assets" / "greenscreen_short.mp4")
    w, h = info.fit(1280, 720)
    frame = decode_frame(info, 1.0, w, h)          # RGB uint8
    rgb_raw = WORK / "frame.rgb"
    rgb_raw.write_bytes(np.ascontiguousarray(frame).tobytes())

    sugg_rgb, _ = K.suggest_key_and_similarity(frame)
    print(f"帧 {w}x{h}  自动键色 {sugg_rgb}")
    key = (0, 177, 64)

    ok = True
    for name, sim, blend, feather, mix in CASES:
        p = K.KeyParams(key_rgb=key, similarity=sim, blend=blend,
                        feather_radius=feather, despill=True, despill_mix=mix)
        py = K.key_rgba(frame, p)
        py_raw = WORK / f"py_{name}.rgba"
        py_raw.write_bytes(np.ascontiguousarray(py).tobytes())

        cpp_raw = WORK / f"cpp_{name}.rgba"
        r = subprocess.run([str(EXE), str(rgb_raw), str(cpp_raw), str(w), str(h),
                            str(key[0]), str(key[1]), str(key[2]),
                            f"{sim}", f"{blend}", f"{feather}", "1", f"{mix}"],
                           capture_output=True, text=True)
        if r.returncode != 0:
            print(f"  {name}: C++ 运行失败 {r.stderr}")
            ok = False
            continue

        cpp = np.frombuffer(cpp_raw.read_bytes(), np.uint8).reshape(h, w, 4)
        diff_a = np.abs(py[..., 3].astype(np.int16) - cpp[..., 3].astype(np.int16))
        diff_rgb = np.abs(py[..., :3].astype(np.int16) - cpp[..., :3].astype(np.int16))
        big = float((diff_a > 2).mean() * 100)
        print(f"  {name}: alpha 平均差 {diff_a.mean():.3f} 最大 {diff_a.max()} "
              f"| 差>2 的像素 {big:.3f}% | RGB 平均差 {diff_rgb.mean():.3f} "
              f"| {r.stdout.strip()}")
        if diff_a.mean() > 0.5 or big > 0.5:
            ok = False
            print("     ^^^ 超出容差, 说明两边算法有实质差异")
    print("\n结论:", "一致" if ok else "存在差异, 需要对齐")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
