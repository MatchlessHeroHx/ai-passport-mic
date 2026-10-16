<p align="right"><a href="firmware-copy.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Passport Mic firmware copy

User-visible strings for the Passport Mic firmware, from `main/mic_ui.c` (screen
copy) and `main/mic_lines.c` (speech-bubble lines). The device UI is Chinese only.
The Simplified Chinese version is the source of truth: it is the owner's revised
copy, listing every string with the state that shows it and the reason for each
change.

## Principles of the revision

1. No fake emotion or excessive personification; the companion is a quiet
   listener, suitable for work.
2. No cold engineering jargon either; plain, calm wording.
3. Every bubble line fits the bubble (about 12 CJK characters), and button names
   are separated from actions with a middle dot, for example "press OK · start".

## Hardware adjustments made when implementing

Confirmed with the owner: the power button turns the device off after about two
seconds and on after half a second, so the error state says to power off and on
again instead of "hold to restart". The Mac app is named Passport Mic everywhere,
matching the menu bar and Bluetooth.

## Behavior fixes shipped with this copy

The just-connected greeting now stays for about four seconds instead of being
replaced within one refresh, and the bubble skips any line that repeats the
current title or caption.
