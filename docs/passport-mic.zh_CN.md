<p align="right"><strong>简体中文</strong> · <a href="passport-mic.md">English</a></p>

# 声波舱：连接 Chatterfly 的 Passport Mic

AI Passport 使用 ES8311 麦克风采集 16 kHz、16 位单声道 PCM，通过 IMA ADPCM 压缩后经 BLE 传到
Mac 桥接程序，再输出 48 kHz 双声道至 BlackHole 2ch，由 Chatterfly 选作麦克风。
USB 保留作有线备用和刷机。ESP32-C3 支持 BLE，不支持经典蓝牙耳机协议，因此需要
配套桥接程序，不能像 HFP 耳机一样直接成为系统麦克风。Chatterfly 沿用自身识别和
联网方式；固件、桥接均不保存录音，空闲音频在设备内丢弃。

## 三键操作

| 按键 | 动作 |
| --- | --- |
| 确定键 | 开始听写，再按结束；等 REC 出现后说话。 |
| 上键 | 就绪时，向当前 Mac 应用发送 Command-Z。 |
| 下键 | 就绪时，向当前 Mac 应用发送回车。 |

断连、启动中和录音中忽略上下键。撤销作用于当前应用的撤销历史，不能保证只删除
一段听写；回车可能换行、确认输入或发送消息，取决于当前应用。
请等 Chatterfly 完成文字上屏后再操作。仅 PRESS 触发动作，长按、松开、单击通知
不会重复触发。屏幕反馈表示已发送指令，不表示 Mac 应用已确认执行。

待机时屏幕会省电：15 秒内没有按键或状态变化，背光降到 5%；45 秒后关闭面板（DISPOFF 并关背光，界面停止刷新）。
启动中、录音中和配对中保持常亮，任何状态变化都会重新亮屏。暗屏时三个键照常工作。
灭屏时按确定键会亮屏并直接开始听写；按上下键只亮屏。

界面以伙伴「小声」为中心，屏幕上方是 240 × 240 的视频画面。三段生成视频负责她的表情：待机（平静循环）、
倾听（你听写时她微微前倾、歪头专注地听）、点头（录音结束或按上下键后）。循环片段正放后倒放，衔接处不会跳；
单次片段在最接近的待机帧上交回待机。肩部的气泡显示 `main/mic_lines.c` 里的内置短句，按状态挑选，不会连续重复；
待机和录音时每 9 秒换一句，约 4 秒后自动收起。底部卡片显示状态、录音计时与真实 PCM 波形，以及下一步按键操作。
左上角用蓝牙或 USB 图标加文字显示连接状态（未连接、蓝牙已连接、数据线已连接、正在听、配对中），右上角显示电量。
首次配对时六位配对码替换标题。

视频以每秒 6 帧播放，不做有损压缩，优先保证清晰。`tools/generate_mic_clips.py`（需 ffmpeg、OpenCV 和 numpy）
把 `VIDEO/` 里的每段视频裁成上方正方形、缩放到 240 × 240，按 16 × 16 的 RGB565 图块存进
`assets/images/mic_clips.bin`（约 4.9 MB）并嵌入固件。重复图块只存一份。设备直接从 Flash 绘制图块，
只重绘变化的图块，不需要整帧缓冲。

在 Mac 上，`companion/Start.command` 会把桥接作为菜单栏程序（`companion/menubar.py`）启动，之后可以关掉终端窗口。
菜单栏图标显示未连接、正在连接、已连接、正在听或出错。菜单里有当前提示、最近事件，以及「重新连接」「打开蓝牙设置」
「打开日志」「退出」。桥接意外退出时 3 秒后自动重启；配对、权限和 BlackHole 错误会停下来提示怎么处理。
日志在 `~/Library/Logs/PassportMic/bridge.log`，超过 2 MB 自动轮换。

连接期间，设备在连上时立刻上报一次电量，之后每分钟一次，内容包括：CW2017 电量（精度 1/256 %）、电池电压、设备状态、
屏幕档位、是否插着 USB。桥接把每条记录追加到 `~/Library/Logs/PassportMic/battery.csv`。运行
`.venv/bin/python companion/battery_report.py [--since 2026-10-16T12:00]` 可以按状态和屏幕档位算出耗电速度，
单位是每小时百分比，以及按 500 mAh 电池估算的电流。插着 USB（在充电）的时段和超过 3 分钟的断档会被跳过。
测量时拔掉 USB，在相同的电量区间内对比；不到约 1 小时或电量变化小于 0.5% 的结果当作噪声。链路断开期间不会记录。

## 无线传输与恢复

设备广播名为 `Passport Mic`。服务 UUID 为 `12345678-1234-5678-1234-56789abcdef0`，
音频通知特征末尾为 `def1`，受保护的读写控制特征末尾为 `def2`。
广播包包含 flags 与服务 UUID，扫描响应包含名称，避免超过 31 字节。
Mac 读取受保护特征触发配对。错误或过期绑定需在 Mac 更正后重试，固件不会默默删除旧绑定。

FPM1 帧头 16 字节：魔数、类型 u8、保留位 u8、长度 u16 小端、序号 u32 小端、
CRC-32 u32 小端。CRC 覆盖头部 4..11 字节和负载。类型为 HELLO=1（一字节状态）、
START=2、AUDIO=3（USB 用 640 字节 PCM，20 ms）、STOP=4、ERROR=5、UNDO=6、CONFIRM=7、AUDIO_ADPCM=8（BLE 用 164 字节，20 ms）
（控制事件空负载）。状态为 OFFLINE=0、READY=1、STARTING=2、RECORDING=3、
音频错误=255。P 为心跳，A 为快捷键发送后的开始确认，X 为复位到待机。

BLE 通知增加 `[B5 1A][帧号 u16 小端][分片序号 u8][分片总数 u8]`，每片负载为
协商 MTU 减九，最大 238 字节。重组限制为一帧 656 字节，检查顺序和 CRC。
拥塞时设备宁可跳过整帧音频或 HELLO 也不阻塞，并为 Mac 的控制写入保留 6 个 NimBLE 缓冲；按键和开始/结束帧最多重试 150 ms。
桥接为每个跳过的音频帧补 20 ms 静音，并在下一个 HELLO 之前忽略上下键。缺口超过 25 帧、分片损坏或接收队列满时结束本次会话并重连，不回放旧控制事件。首轮原始 PCM 真机测试出现缺片。修复版将每帧压缩至 164 字节，加上 FPM1 与分片头共 186 字节，在实测 MTU 247 下每帧只发一条通知，约 9.3 KB/s。每块携带初始预测值和步长索引，可独立解码；采样率仍为 16 kHz，ADPCM 属于有损压缩。实际音质和稳定性待复测。

主机每 500 ms 发送心跳；设备三秒没有心跳停止，BLE 断开立即停止，启动请求两秒超时。
普通蓝牙断连和临时写入错误会结束听写，并重新扫描已选定的同一设备，重连回到待机。
只有配对、认证或权限错误才会退出并提示处理。USB 拔线仍会退出有线桥接，需要重连并重新启动。

本机 Chatterfly 1.0.7.13415 使用单按右 Ctrl。尚未确认其公开录音状态接口，发出
快捷键不代表录音状态得到确认。不要混用手动切换；若 Chatterfly 自行停止，退出桥接、
关闭听写后重开。CoreBluetooth 配对、距离、音质、吞吐和长时间运行均需真机验收。

## 验证

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

完整门禁包括 C 状态／三键／MTU 测试和 C/Python 协议互通。协议测试覆盖不同 MTU、
缺片、重复片、乱序、CRC、序号回绕和录音保护。模拟 BLE 测试运行实际桥接循环，
覆盖启停、撤销、回车及清理失败，不发出真实按键。LVGL 渲染 13 个画面，检查中文字形、
边界、长时计时与 1000 次更新。

## 当前固件

只保留最新构建，放在 `build/firmware/<SHA256>/`，附带 ELF、map 和 `manifest.json`；
不要把其他产物当作最新版烧录。

```text
Full SHA-256: 20b6c8c7c7bb9d35b9c8f280c093cc796c47457481df857bcde027b7a81d48ec
App SHA-256:  b1ba6dea96f0bcca3729a4688e7d7b35e26d177dc4509bc3a6f242eff1446938
ELF SHA-256:  929bf0f0966a3325b9800642382788f48f21259a513192905eeb163b65d629e0
Archive: build/firmware/20b6c8c7c7bb9d35b9c8f280c093cc796c47457481df857bcde027b7a81d48ec/
```

刷过其他固件后要恢复，从 0x0 写入完整镜像，然后在 Mac 蓝牙设置里忽略 Passport Mic 并重新配对：
`esptool.py --chip esp32c3 --port /dev/cu.usbmodem1101 --baud 460800 write_flash 0x0 <FoloToy-AI-Passport-full.bin>`。
