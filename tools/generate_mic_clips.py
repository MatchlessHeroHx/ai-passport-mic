#!/usr/bin/env python3
"""Encode the companion's video clips into a tiled RGB565 animation pack.

Each source video is cropped to its top square, resized to 240 x 240 with
area averaging, sampled at a low frame rate and stored as 16 x 16 RGB565 tiles.
A frame is a 15 x 15 grid of tile indices. Tiles shared by several frames are
stored once, so the static background costs almost nothing, and the device can
draw any frame straight from Flash without a full-frame RAM buffer.

Clarity is preferred over smoothness: there is no lossy codec and no dithering;
only the 8-bit to RGB565 rounding and tiles that are visually identical
(at most MATCH levels apart on any channel) are merged.

Usage: python3 tools/generate_mic_clips.py [video-dir]
Requires ffmpeg, OpenCV (cv2) and numpy. The pack is written to
assets/images/mic_clips.bin; a preview sheet goes to assets/images/mic-clips-preview.png.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'assets/images/mic_clips.bin'
PREVIEW = ROOT / 'assets/images/mic-clips-preview.png'

SIZE, TILE = 240, 16
GRID = SIZE // TILE
FPS = 6
MATCH = 3          # max channel difference (8-bit) for two tiles to count as the same
STILL = 5          # a tile closer than this to the one already shown is not redrawn

# Order matches mic_clip_t. name, source file, first and last 30 fps frame, mode.
# Looping clips play forward then backward, so the generator's slow sway never
# jumps at the wrap. The listen video is split at its attentive head tilt:
# lead-in, a ping-pong loop on the tilt, and the return to a smile. Ranges were
# picked from the clip's distance-to-rest curve. "to_idle" clips record which
# idle frame they end closest to, so idle resumes there without a jump.
CLIPS = (
    ('idle', '待机.mp4', 0, 149, 'loop'),
    ('listen_in', '倾听.mp4', 0, 60, 'once'),
    ('listen', '倾听.mp4', 60, 105, 'loop'),
    ('listen_out', '倾听.mp4', 105, 149, 'to_idle'),
    ('nod', '点头.mp4', 0, 149, 'to_idle'),
)


def read_frames(path, first, last):
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(['ffmpeg', '-v', 'error', '-i', str(path), f'{tmp}/%04d.png'], check=True)
        files = sorted(Path(tmp).glob('*.png'))[first:last + 1]
        picks = np.arange(0, len(files), 30 / FPS)
        frames = []
        for i in picks:
            img = cv2.imread(str(files[int(round(i))]))[:, :, ::-1]
            side = min(img.shape[:2])
            img = cv2.resize(img[:side, :side], (SIZE, SIZE), interpolation=cv2.INTER_AREA)
            frames.append(img)
        return frames


def thumb(img):
    return cv2.resize(img, (60, 60), interpolation=cv2.INTER_AREA).astype(np.float32)


def distance(a, b):
    return float(np.abs(thumb(a) - thumb(b)).mean())


def trim_one_shot(frames, idle, preroll):
    """Optionally drop a still pre-roll, and end where the clip best matches an idle frame.

    Returns (frames, idle frame to continue from, match distance)."""
    start = 0
    if preroll:
        start = max(k for k in range(len(frames) // 2) if distance(frames[k], frames[0]) < 3)
    idle_thumbs = [thumb(f) for f in idle]
    best = None
    for k in range(int(len(frames) * 0.6), len(frames)):
        t = thumb(frames[k])
        d = [float(np.abs(t - it).mean()) for it in idle_thumbs]
        j = int(np.argmin(d))
        if best is None or d[j] < best[0]:
            best = (d[j], k, j)
    # Frame 0 is kept so the clip still starts from its first pose.
    return [frames[0]] + frames[start + 1:best[1] + 1], best[2], best[0]


def quantise(img):
    """Round to RGB565 and back, so tile matching sees what the panel shows."""
    a = img.astype(np.int16)
    r, g, b = (a[..., 0] * 31 + 127) // 255, (a[..., 1] * 63 + 127) // 255, (a[..., 2] * 31 + 127) // 255
    word = (r << 11) | (g << 5) | b
    back = np.stack([r * 255 // 31, g * 255 // 63, b * 255 // 31], axis=-1)
    return word.astype(np.uint16), back


class TileBank:
    def __init__(self):
        self.words, self.colours = [], []
        self.lookup = {}

    def add(self, words, colours):
        key = words.tobytes()
        if key in self.lookup:
            return self.lookup[key]
        self.words.append(words)
        self.colours.append(colours)
        self.lookup[key] = len(self.words) - 1
        return self.lookup[key]


def encode(clips):
    bank = TileBank()
    encoded = []
    for name, frames, loop, exit_frame in clips:
        shown = None
        maps = []
        for img in frames:
            words, back = quantise(img)
            grid = np.zeros((GRID, GRID), np.uint16)
            for ty in range(GRID):
                for tx in range(GRID):
                    sl = (slice(ty * TILE, ty * TILE + TILE), slice(tx * TILE, tx * TILE + TILE))
                    if shown is not None:
                        old = bank.colours[shown[ty, tx]]
                        if np.abs(back[sl] - old).max() <= MATCH or \
                           np.abs(back[sl] - old).max(axis=2).mean() < STILL:
                            grid[ty, tx] = shown[ty, tx]
                            continue
                    grid[ty, tx] = bank.add(words[sl].copy(), back[sl].copy())
            shown = grid
            maps.append(grid)
        encoded.append((name, maps, loop, exit_frame))
    return bank, encoded


def write_pack(bank, encoded):
    """Little-endian pack: header, clip table, frame maps, then tile pixels."""
    clip_table, frame_data = b'', b''
    frame_offset = 0
    for name, maps, loop, exit_frame in encoded:
        clip_table += struct.pack('<HHBxH', frame_offset, len(maps), 1 if loop else 0, exit_frame)
        for grid in maps:
            frame_data += grid.astype('<u2').tobytes()
        frame_offset += len(maps)
    header = struct.pack('<4sHHHHHH', b'MCV1', SIZE, TILE, FPS, len(encoded), frame_offset, len(bank.words))
    tiles = b''.join(w.astype('<u2').tobytes() for w in bank.words)
    OUTPUT.write_bytes(header + clip_table + frame_data + tiles)
    return len(header) + len(clip_table) + len(frame_data), len(tiles)


def preview(bank, encoded):
    rows = []
    for name, maps, _, _ in encoded:
        picks = np.linspace(0, len(maps) - 1, 8).astype(int)
        row = []
        for k in picks:
            img = np.zeros((SIZE, SIZE, 3), np.uint8)
            for ty in range(GRID):
                for tx in range(GRID):
                    img[ty * TILE:ty * TILE + TILE, tx * TILE:tx * TILE + TILE] = bank.colours[maps[k][ty, tx]]
            row.append(img)
        rows.append(np.concatenate(row, axis=1))
    cv2.imwrite(str(PREVIEW), np.concatenate(rows, axis=0)[:, :, ::-1])


def main():
    src = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'VIDEO'
    raw = [(name, read_frames(src / file, a, b), mode) for name, file, a, b, mode in CLIPS]
    idle = raw[0][1]
    clips = []
    for name, frames, mode in raw:
        exit_frame = 0
        if mode == 'to_idle':
            frames, exit_frame, gap = trim_one_shot(frames, idle, preroll=name == 'nod')
            print(f'{name}: {len(frames)} frames, continues into idle frame {exit_frame} (distance {gap:.1f})')
        clips.append((name, frames, mode == 'loop', exit_frame))
    bank, encoded = encode(clips)
    meta, tiles = write_pack(bank, encoded)
    preview(bank, encoded)
    frames = sum(len(m) for _, m, _, _ in encoded)
    print(f'{frames} frames at {FPS} fps, {len(bank.words)} tiles, '
          f'{(meta + tiles) / 1024:.0f} KB ({meta / 1024:.1f} KB maps)')


if __name__ == '__main__':
    main()
