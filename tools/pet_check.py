"""用 pet_player 播放指定的视频文件, 判断背景是否真透明。

用法: python native/tools/pet_check.py <视频1> [视频2 ...]
判据: 播放前后截屏, 窗口区域四角颜色若与桌面一致 => 透明。
"""
from __future__ import annotations

import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from PIL import ImageGrab

PET_DIR = Path(r"D:\teachergeng\QTAVPlayer\QtAVPlayer\examples\pet_player\build")
PET_EXE = PET_DIR / "pet_player.exe"


def grab():
    return np.asarray(ImageGrab.grab()).copy()


def check(video: Path) -> str:
    # 关键: 先把残留的播放器关掉, 否则"播放前"基线里已经有了上一个窗口
    subprocess.run(["taskkill", "/F", "/IM", "pet_player.exe"], capture_output=True)
    time.sleep(1.0)
    before = grab()
    proc = subprocess.Popen([str(PET_EXE), str(video)], cwd=str(PET_DIR))
    shots = []
    try:
        for delay in (3.0, 1.2, 1.2):
            time.sleep(delay)
            shots.append(grab())
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    time.sleep(0.8)

    # 取变化最大的那一次快照作为判断依据(片段播完后窗口会清空)
    best = None
    for after in shots:
        diff = np.abs(after.astype(np.int16) - before.astype(np.int16)).max(axis=2)
        changed = diff > 10
        if changed.any() and (best is None or changed.sum() > best[1].sum()):
            best = (after, changed)
    if best is None:
        return "无窗口(播放器没显示画面)"

    after, changed = best
    ys, xs = np.where(changed)
    y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
    ratio = float(changed[y0:y1 + 1, x0:x1 + 1].mean())
    if ratio < 0.02:
        return f"不确定 (窗口区域只变了 {ratio*100:.1f}%, 可能没渲染)"

    same = True
    for yy, xx in ((y0 + 6, x0 + 6), (y0 + 6, x1 - 6), (y1 - 6, x0 + 6), (y1 - 6, x1 - 6)):
        b = np.median(before[max(0, yy - 3):yy + 4, max(0, xx - 3):xx + 4].reshape(-1, 3), axis=0)
        a = np.median(after[max(0, yy - 3):yy + 4, max(0, xx - 3):xx + 4].reshape(-1, 3), axis=0)
        if np.abs(b - a).max() > 6:
            same = False
    verdict = "透明 ✅" if (same and ratio < 0.4) else "不透明 ❌"
    return f"{verdict}  (区域变化 {ratio*100:.1f}%, 四角一致={same})"


def main() -> int:
    if not PET_EXE.exists():
        print("找不到 pet_player.exe")
        return 2
    for arg in sys.argv[1:]:
        p = Path(arg)
        if not p.exists():
            print(f"{p.name}: 不存在")
            continue
        print(f"{p.name:28s} {check(p)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
