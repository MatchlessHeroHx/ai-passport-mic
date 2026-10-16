#!/usr/bin/env python3
"""Generate the UI's Noto Sans CJK SC subset from mic_ui.c and mic_lines.c; fonttools + lv_font_conv 1.5.3."""
from pathlib import Path
import re
import subprocess
import sys
from fontTools import subset
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[1]
font = TTFont(sys.argv[1])
characters = set(chr(c) for c in range(32, 127))
for source in ('main/mic_ui.c', 'main/mic_lines.c'):
    characters.update(re.findall(r'[\u3400-\u9fff，·？…]', (ROOT / source).read_text()))
assert not {ord(c) for c in characters} - font.getBestCmap().keys()
subsetter = subset.Subsetter()
subsetter.populate(unicodes=[ord(c) for c in characters])
subsetter.subset(font)
font.save(ROOT / 'assets/fonts/mic_subset.otf')
for size in (16, 20):
    subprocess.run(['lv_font_conv', '--font', 'assets/fonts/mic_subset.otf',
                    '--symbols', ''.join(sorted(characters)), '--size', str(size),
                    '--bpp', '4', '--format', 'lvgl', '--no-compress', '--no-kerning',
                    '--lv-font-name', f'mic_font_{size}', '--lv-include', 'lvgl.h',
                    '--output', f'assets/fonts/mic_font_{size}.c'], cwd=ROOT, check=True)
print(f'{len(characters)} glyphs in each size')
