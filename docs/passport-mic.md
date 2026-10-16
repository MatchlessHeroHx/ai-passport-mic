<p align="right"><a href="passport-mic.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Sound Pod: Passport Mic for Chatterfly

AI Passport captures 16 kHz, signed 16-bit mono PCM with its ES8311 microphone.
The Mac bridge receives IMA ADPCM audio over BLE and outputs 48 kHz stereo to BlackHole
2ch, selected as Chatterfly's microphone. USB remains a wired fallback and the
firmware flashing interface. ESP32-C3 supports BLE, not Classic Bluetooth headset
profiles: this is a custom BLE microphone with a companion, not a system-paired
HFP headset. Chatterfly retains its own recognition/network behavior. The bridge
and firmware do not save recordings; idle PCM is discarded on the device.

## Controls

| Button | Action |
| --- | --- |
| OK | Start dictation; press again to finish. Wait for REC before speaking. |
| UP | While READY, send Command-Z to the focused Mac application. |
| DOWN | While READY, send Return to the focused Mac application. |

UP/DOWN are ignored while disconnected, starting, or recording. Undo applies to
the focused application's undo history, not a guaranteed single transcript;
Return may insert a newline, confirm an input, or submit/send it depending on the
application. Wait for Chatterfly to finish inserting text before using either.
Only physical PRESS events trigger commands; holding/release/click notifications
never duplicate them. UI confirmations mean a command was sent, not that the Mac
application acknowledged it.

The screen saves power while idle: after 15 s without a key press or state change
the backlight dims to 5%, and after 45 s the panel turns off (DISPOFF plus no
backlight; the UI stops redrawing). Starting, recording and pairing keep it on, and
any state change lights it again. While dimmed every key works as usual. While off,
OK lights the screen and starts dictation at once; UP/DOWN only light it.

The UI is built around Xiao Sheng, a companion shown as a 240 × 240 video at the top
of the screen. Three generated clips carry her expressions: idle (a calm loop),
listen (she leans in and holds an attentive tilt while you dictate) and nod (after
a finished recording or an UP/DOWN action). Looping clips play forward then
backward, so the video never jumps at the wrap; one-shot clips hand back to idle
on the matching frame. A speech bubble on her shoulders shows short built-in lines
from `main/mic_lines.c`, chosen by state and never repeated back to back; in idle
and while recording she says something new every 9 seconds and the bubble fades
after about 4 seconds. The bottom card shows the state, the recording timer with a
real PCM waveform, and the next button action. The top-left pill shows the link
with a Bluetooth or USB icon and plain Chinese words (not connected, Bluetooth
connected, cable connected, listening, pairing) instead of "BLE"; the top-right
pill shows battery. The six-digit pairing code
replaces the title during first pairing.

The clips are played at 6 frames per second, without lossy compression, to keep
them sharp. `tools/generate_mic_clips.py` (ffmpeg, OpenCV and numpy) crops each
video in `VIDEO/` to its top square, resizes it to 240 × 240 and stores it as
16 × 16 RGB565 tiles in `assets/images/mic_clips.bin` (about 4.9 MB), embedded in
the firmware. Repeated tiles are stored once. The device draws tiles straight from
Flash and only redraws tiles that changed, so no frame buffer is needed.

On the Mac, `companion/Start.command` starts the bridge as a menu-bar app
(`companion/menubar.py`) and the Terminal window can be closed. The icon shows
not connected, connecting, connected, listening or error. Its menu shows the
current hint, recent events, and Reconnect, Open Bluetooth Settings, Open Log and
Quit. If the bridge exits unexpectedly it is restarted after 3 seconds; pairing,
permission and BlackHole errors stop and show what to fix. The log is
`~/Library/Logs/PassportMic/bridge.log`, rotated at 2 MB.

BLE notifications add `[B5 1A][frame-id u16 LE][part-index u8][part-count u8]`.
Payload is negotiated MTU minus nine, capped at 238 bytes. Reassembly bounds one
656-byte frame and validates ordering and CRC. Under congestion the device skips
whole audio or HELLO frames rather than stalling, keeping 6 NimBLE buffers free for
the Mac's control writes; key and START/STOP frames retry for up to 150 ms. The
bridge fills each skipped audio frame with 20 ms of silence and ignores UP/DOWN
until the next HELLO. A gap above 25 frames, a corrupt fragment or a full receive
queue ends the session and reconnects; commands are never replayed. The initial raw-PCM device test lost fragments. The repair uses independently decodable IMA ADPCM blocks: 164 audio bytes + 16 frame bytes + 6 fragment bytes, fitting one notification at the measured MTU 247, about 9.3 KB/s. The block carries its initial predictor and step index. Sampling remains 16 kHz; compression is lossy. Actual sound quality and sustained throughput need retesting.

The bridge sends heartbeats every 500 ms. Device stops after three seconds without
one, or immediately on loss of the BLE link. Starts time out after two seconds.
Ordinary BLE disconnects and transient write errors stop dictation and scan again
for the same selected device; new sessions start idle. Only pairing,
authentication or permission errors exit with an actionable message. USB unplug still exits the wired bridge; reconnect and relaunch it.

Chatterfly 1.0.7.13415 uses a right-Control tap here. Its public recording state API
has not been established: posting that key is not a recording acknowledgement.
Do not mix manual toggles with device control. If Chatterfly stops independently,
close the bridge, turn dictation off, and relaunch. CoreBluetooth pairing behavior,
range, audio quality, throughput and long sessions remain hardware acceptance items.

## Verification

```bash
source ../activate-esp-idf.sh
./tools/validate.sh
.venv/bin/python tests/test_mic_audio.py
.venv/bin/python tests/test_mic_session.py
.venv/bin/python tests/test_mic_ble_session.py
cmake -S tests/mic_ui -B build/ui -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/ui
(cd build/ui && ./render_mic)
```

The full gate includes C state/button/MTU checks and C-to-Python wire tests. Protocol
tests cover all negotiated fragment sizes, missing/duplicate/reordered fragments,
CRC, rollover and recording guards. Mock BLE tests exercise the actual bridge loop,
start/stop, undo/Return and teardown failures without sending real keys. LVGL renders
13 frames, checking Chinese glyphs, bounds, long durations and 1000 updates.

## Current firmware

Only the latest build is kept, in `build/firmware/<SHA256>/` with its ELF, map and
`manifest.json`; never flash anything else as though it were the latest.

```text
Full SHA-256: 20b6c8c7c7bb9d35b9c8f280c093cc796c47457481df857bcde027b7a81d48ec
App SHA-256:  b1ba6dea96f0bcca3729a4688e7d7b35e26d177dc4509bc3a6f242eff1446938
ELF SHA-256:  929bf0f0966a3325b9800642382788f48f21259a513192905eeb163b65d629e0
Archive: build/firmware/20b6c8c7c7bb9d35b9c8f280c093cc796c47457481df857bcde027b7a81d48ec/
```

To restore it after flashing other firmware, write the full image at 0x0, then
forget Passport Mic in macOS Bluetooth settings and pair again:
`esptool.py --chip esp32c3 --port /dev/cu.usbmodem1101 --baud 460800 write_flash 0x0 <FoloToy-AI-Passport-full.bin>`.
