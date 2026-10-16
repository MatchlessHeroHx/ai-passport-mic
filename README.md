**English** · [简体中文](README.zh_CN.md)

# Passport Mic

Wireless vibe-coding microphone firmware based on the open-source
[FoloToy AI Passport](https://github.com/FoloToy/ai-passport) hardware.

Clip the device on, press OK and talk. It streams your voice over Bluetooth LE to
a Mac, where Chatterfly turns it into text in whatever window has focus: your AI
coding tool, terminal or editor. UP undoes the last insertion, DOWN presses Return
to send it. No cable, no keyboard.

<p align="center">
  <img src="assets/images/mic-clips-preview.png" alt="Frames of the on-screen companion Xiao Sheng: idle, listening and nodding." width="100%">
</p>

## Hardware

FoloToy AI Passport, an open wearable board:

| Part | Detail |
| --- | --- |
| MCU | ESP32-C3, BLE 5, 8 MB Flash, no PSRAM |
| Display | ST7789P3, 240 × 320 |
| Microphone | ES8311 codec |
| Buttons | UP / DOWN / OK, plus a separate power key |
| Battery | 500 mAh with a CW2017 fuel gauge |
| Port | USB-C for charging, flashing and a wired fallback |

## How it works

```text
AI Passport  ── BLE (16 kHz IMA ADPCM, bonded) ──▶  Mac menu-bar app  ──▶  BlackHole 2ch  ──▶  Chatterfly
   OK / UP / DOWN                                   Return / Command-Z / dictation key
```

- **Firmware** (`main/mic_*.c`): capture, ADPCM framing, BLE with LE Secure
  Connections, the companion UI, idle screen dimming and battery reports.
- **Mac app** (`companion/`): a menu-bar bridge that reconnects on its own, plays
  the audio into BlackHole 2ch and sends the keys.

## Quick start

1. Install [BlackHole 2ch](https://existential.audio/blackhole/) and Chatterfly,
   and pick BlackHole 2ch as Chatterfly's microphone.
2. Build the Mac app: `companion/build-mac.sh`.
3. Build and flash the firmware with ESP-IDF 5.5.3: `./tools/validate.sh --firmware`
   writes `build/FoloToy-AI-Passport-full.bin`; then
   `esptool.py --chip esp32c3 write_flash 0x0 build/FoloToy-AI-Passport-full.bin`.
4. Run `companion/Start.command` and enter the six-digit code shown on the device.

## Controls

| Key | Action |
| --- | --- |
| OK | Start dictation; press again to finish. |
| UP | Undo (Command-Z) while ready. |
| DOWN | Return while ready. |

The screen dims after 15 s and turns off after 45 s. From off, OK wakes it and
starts dictation at once; UP/DOWN only wake it.

## Documentation

- [Passport Mic guide](docs/passport-mic.md): behavior, protocol, battery logging and verification.
- [Firmware copy](docs/firmware-copy.md): every on-screen string.
- [Upstream platform docs](docs/README.md): the FoloToy AI Passport base this project builds on.

## License

[MIT](LICENSE), as the FoloToy AI Passport base.
