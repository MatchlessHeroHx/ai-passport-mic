<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files and generated font sources in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.


The microphone UI uses `fonts/mic_subset.otf` and `fonts/mic_font_16.c` /
`fonts/mic_font_20.c`: Noto Sans CJK SC Regular, 176 glyphs, 16/20 px, 4 bpp,
uncompressed. Source: [Noto CJK](https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf);
license: [SIL OFL](fonts/OFL.txt). Regenerate using
`python tools/generate_mic_font.py <upstream-otf>` with fonttools and
lv_font_conv 1.5.3 on PATH. The script derives the inventory from `main/mic_ui.c`.
The fonts are compiled by `main/CMakeLists.txt`; Chinese widgets explicitly
select them. The host renderer checks glyph coverage and label bounds.

## Images

Store reusable source images and generated display assets in `images/`.

| File | Dimensions and format | Use and source |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160, JPEG | Product hero image embedded in both project README files to foreground AI Passport and its open, maker-oriented identity. |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724, PNG RGBA | Optional technical infographic retained as a reference asset; it is no longer used as the homepage hero. Generated for this repository with the built-in image generation tool on 2026-09-17; the six labels and values were checked against the documented hardware contract. |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336, PNG RGBA | Transparent black wordmark extracted from the repository's original `images/logo.png`; embedded in both project README files for light backgrounds. |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336, PNG RGBA | White version of the extracted wordmark, used by the README `<picture>` element when GitHub is in dark mode. |
| [`images/mic-character.png`](images/mic-character.png) | 1199 × 1312, PNG RGB | Source art for the microphone UI companion, provided by the project owner; used as the first frame when generating the clips. |
| [`images/mic-character-1x1.png`](images/mic-character-1x1.png) | 1199 × 1199, PNG RGB | Top-aligned square crop of the source art, for 1:1 image-to-video generation. |
| [`images/mic_clips.bin`](images/mic_clips.bin) | 240 × 240 tiled RGB565, about 4.9 MB | Companion animation pack built by `tools/generate_mic_clips.py` from the owner's generated videos in `VIDEO/` (idle, listen, nod); embedded by `main/CMakeLists.txt`. `images/mic-clips-preview.png` is its review sheet. |

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.
