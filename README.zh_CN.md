[English](README.md) · **简体中文**

# Passport Mic

基于开源硬件 [FoloToy AI Passport](https://github.com/FoloToy/ai-passport) 的无线 vibe coding 麦克风固件。

把设备别在身上，按确定键就能说话。声音通过蓝牙 LE 传到 Mac，由 Chatterfly 转成文字，输入到当前焦点所在的窗口，
比如 AI 编程工具、终端或编辑器。上键撤销刚输入的内容，下键按回车发送。不用连线，也不用碰键盘。

<p align="center">
  <img src="assets/images/mic-clips-preview.png" alt="屏幕伙伴「小声」的视频帧：待机、倾听和点头。" width="100%">
</p>

## 硬件

FoloToy AI Passport，一块开源的可穿戴开发板：

| 部件 | 说明 |
| --- | --- |
| 主控 | ESP32-C3，BLE 5，8 MB Flash，无 PSRAM |
| 屏幕 | ST7789P3，240 × 320 |
| 麦克风 | ES8311 编解码芯片 |
| 按键 | 上 / 下 / 确定，另有独立电源键 |
| 电池 | 500 mAh，CW2017 电量计 |
| 接口 | USB-C，用于充电、刷机和有线备用 |

## 工作原理

```text
AI Passport  ── BLE（16 kHz IMA ADPCM，已绑定）──▶  Mac 菜单栏程序  ──▶  BlackHole 2ch  ──▶  Chatterfly
   确定 / 上 / 下                                   回车 / Command-Z / 听写快捷键
```

- **固件**（`main/mic_*.c`）：采音、ADPCM 封帧、LE Secure Connections 蓝牙、伙伴界面、待机暗屏和电量上报。
- **Mac 端**（`companion/`）：菜单栏桥接程序，自动重连，把音频送进 BlackHole 2ch，并发送按键。

## 快速开始

1. 安装 [BlackHole 2ch](https://existential.audio/blackhole/) 和 Chatterfly，把 Chatterfly 的麦克风设为 BlackHole 2ch。
2. 编译 Mac 端：`companion/build-mac.sh`。
3. 用 ESP-IDF 5.5.3 编译并刷入固件：`./tools/validate.sh --firmware` 会生成 `build/FoloToy-AI-Passport-full.bin`，然后运行
   `esptool.py --chip esp32c3 write_flash 0x0 build/FoloToy-AI-Passport-full.bin`。
4. 运行 `companion/Start.command`，输入设备屏幕上显示的六位配对码。

## 按键

| 按键 | 动作 |
| --- | --- |
| 确定 | 开始听写，再按一次结束。 |
| 上 | 就绪时撤销（Command-Z）。 |
| 下 | 就绪时按回车。 |

15 秒后屏幕变暗，45 秒后熄灭。熄屏时按确定键会亮屏并直接开始听写；按上下键只亮屏。

## 文档

- [Passport Mic 说明](docs/passport-mic.zh_CN.md)：行为、协议、电量记录和验证。
- [固件文案](docs/firmware-copy.zh_CN.md)：屏幕上的全部文字。
- [硬件开发指南](docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md)：开发板的引脚、外设和 BSP 约定。
- [FoloToy AI Passport](https://github.com/FoloToy/ai-passport)：本项目所基于的上游平台。

## 许可证

[MIT](LICENSE)，与 FoloToy AI Passport 基线一致。
