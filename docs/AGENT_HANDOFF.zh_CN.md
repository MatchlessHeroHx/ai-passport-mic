<p align="right"><strong>简体中文</strong> · <a href="AGENT_HANDOFF.md">English</a></p>

# Agent 交接：Passport Mic 连接 Chatterfly

更新时间：2026-10-16 10:57。工作树：`/Users/xuanhe/Documents/FoloToy/ai-passport-mic`。
它是 `../ai-passport` 的 git worktree，分支 `feature/mac-voice-mic`，基于 `a06235e`。
功能已提交为 `eecb9ad`，并推送到公开的 GitHub 仓库 https://github.com/MatchlessHeroHx/ai-passport-mic
（远端 `github`，分支 `feature/mac-voice-mic`，也是那边的默认分支）。`origin` 是 FoloToy 的 Gitee 仓库，
不要往那里推送。只在用户要求时提交或推送；不要 reset、clean 或 stash pop 工作区。

## 下一步（新会话从这里开始）

1. **询问用户对最新屏幕省电固件 `20b6c8c7` 的反馈。** 它在 10:56 刷入：15 秒暗屏到 5%，45 秒灭屏。
   问 5% 亮度是否合适、暗屏时有没有闪烁。如果仍然偏亮，下一步可以试 2–3%；占空比太低时，
   部分背光驱动可能会闪烁。
2. 新文案固件上还有两件事用户没确认：设备上的文案、约 4 秒的问候气泡。菜单栏图标和菜单的显示效果也没确认。
   用户为了推进先跳过了，方便时问一次即可。
3. 后续可做的省电工作，用户尚未要求（见下方「省电」）：待机时挂起 ES8311，长时间离线后进入深睡。
   用户说蓝牙参数暂时不动。
4. 之后按「待测项目」继续。

## 当前设备与 Mac 端状态

- 目标：ESP32-C3、ESP-IDF v5.5.3、8 MB Flash。
- 设备 app：`20b6c8c7c7bb9d35b9c8f280c093cc796c47457481df857bcde027b7a81d48ec`
  （完整镜像；app 镜像 `b1ba6dea96f0bcca3729a4688e7d7b35e26d177dc4509bc3a6f242eff1446938`，
  6,124,576 字节；ELF `929bf0f0966a3325b9800642382788f48f21259a513192905eeb163b65d629e0`）。
  2026-10-16 10:56 只刷 app 分区（0x10000），用原有绑定自动重连。
- Bootloader、分区表和绑定来自约 09:55 的一次完整刷写（该构建的归档已删除）；用户 09:58 重新配对。
  - 分区表 hash（`420931e3...`）与 `20b6c8c7` 归档一致，所以只刷 app 是安全的。
  - 设备上的 bootloader（`292667e4...`）与归档里的（`d1368bd6...`）不同。推测只是构建元数据，
    没有逐字节比对。完整刷写 `20b6c8c7` 会替换它。
- `build/FoloToy-AI-Passport-full.bin` 当前是 `20b6c8c7`。
- 20 次短启停通过（10:00:11–10:01:42）：20 次开始与 20 次结束一一配对，无丢帧、无 `LinkLost`、无报错。
- 中间的一版屏幕固件用的是 10%、30 秒、90 秒。用户在它上面测了暗屏、灭屏、灭屏按确定、
  灭屏按上键，没发现问题，然后要求改成现在的时间。
- 2026-10-15 待机时断开过两次（21:00 与 21:07），原因仍未查明。屏幕亮着却断开的话需要插 USB 读日志。

Mac 端桥接以菜单栏程序运行，不依赖 Agent 会话，交接时它仍在运行（刷机后于 10:56 重启）。
- `companion/Start.command` 启动 `companion/menubar.py`，不会重复启动第二份。
- 菜单里有状态、最近事件、重新连接、打开蓝牙设置、打开日志、退出（⌘Q）。
- 桥接意外退出 3 秒后自动重启，已实测。配对、权限、BlackHole 类错误会停下并提示处理方法。
- 日志：`~/Library/Logs/PassportMic/bridge.log`，超过 2 MB 轮转。每次连接都会打印配对码提示那一行，
  它不代表发生了新的配对。
- 需要只看终端输出调试时，先从菜单退出，再运行 `.venv/bin/python companion/passport_mic.py --transport ble`。
  同一时间只运行一个桥接。

2026-10-16 11:05 按用户确认做了清理：只保留最新固件、相关文档和 Mac 端软件。
- `build/firmware/` 只剩 `20b6c8c7` 归档。所有旧固件归档、旧备份和 `build/ui` 缓存都已删除；
  旧版本从未提交，无法恢复。
- `build/ui` 由下方 LVGL 主机渲染的 configure 步骤重新生成。

备份：`/Users/xuanhe/Documents/FoloToy/backups/20261016-1105-latest/` 是唯一一份，做于屏幕省电改动之后。
- `ai-passport-mic.tar.gz`：工作区，含 `VIDEO/`、`companion/`、`build/mac/chatterfly-key`；
  不含 `.venv`、`managed_components` 和构建产物。
- `firmware-20b6c8c7.tar.gz`：`20b6c8c7` 归档。
- 做有风险的操作前先再备份一次。

## 刷机

每次刷机都要请用户确认。每个新构建都要先归档、绑定确切 hash，再刷写。刷机前请用户插 USB
（串口 `/dev/cu.usbmodem1101`），然后从菜单栏退出 Passport Mic，或结束 `companion/menubar.py` 进程
（桥接是它的子进程）。

- **只刷 app（现在的默认方式，保留绑定）：** 校验归档里 `FoloToy-AI-Passport.bin` 的 hash，然后执行
  `esptool.py --chip esp32c3 --port /dev/cu.usbmodem1101 --baud 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 8MB 0x10000 <app.bin>`。
  之后 `open companion/Start.command`，不用输配对码就会重连。只在分区表 hash 不变时使用。
- **完整合并镜像（清除绑定）：** `write_flash 0x0 <full.bin>`。刷过其他固件后要恢复 `20b6c8c7` 就用这种方式，
  因为别的固件可能改了分区表。之后请用户在 Mac 蓝牙设置里忽略
  Passport Mic，运行 `companion/Start.command`，输入六位配对码。

## 产品行为

- 确定键 PRESS：开始或结束听写。就绪时上键 PRESS 发 Command-Z，下键 PRESS 发回车；
  离线、启动中、录音中忽略上下键。
- 屏幕省电（`main/mic_screen.c`）：
  - 15 秒没有按键也没有状态变化，背光降到 5%；45 秒后灭屏。正常亮度 70%。
  - 启动中、录音中、配对中保持常亮，模型状态的任何变化都会重新亮屏。
  - 暗屏时三个键照常工作。灭屏时按确定键会亮屏并直接开始听写；按上下键只亮屏，不发送任何指令。
- 板上没有固件能控制的 LED。所有 GPIO 都被屏幕、按键、I2C 和音频占用；文案里的「录音指示灯」是画出来的红点。
- BLE 使用带绑定的 LE Secure Connections，设备显示六位配对码。合并镜像刷机会清除设备绑定；
  CoreBluetooth 报 `CBErrorDomain Code=14 Peer removed pairing information` 时，在 Mac 蓝牙设置忽略
  Passport Mic 后重新配对。
- 音频：16 kHz 单声道，每 20 ms 一个 164 字节 IMA ADPCM 块，输出到 BlackHole 2ch。
  Chatterfly 的麦克风必须选 BlackHole 2ch。
- 有线备用：`.venv/bin/python companion/passport_mic.py --transport usb`。
- 电源键（硬件独立按键）：按住约 0.5 秒开机，约 2 秒关机，不能重启。
- 界面布局：
  - 上方是 240 × 240 视频伙伴「小声」，有待机、倾听、点头三段动画。
  - 一个显示短句的气泡。
  - 底部三行卡片。
  - 左上角连接胶囊（蓝牙/USB 图标加文字），右上角电量。
- 全部界面文案见 `docs/firmware-copy.zh_CN.md`。这是负责人修订后的定稿，以它为准。

## 不明显的设计决定

1. 合成的回车和 Command-Z 发到 `.cgSessionEventTap`。不用 HID 层，是因为输入法（WeType、Chatterfly）
   在 HID 层的事件钩子会吞掉发到那里的合成编辑键。开始/结束听写的右 Ctrl 仍走 HID 层，可以正常工作。
   `companion/chatterfly_key.swift` 里要保留这种分工。
2. 无线拥塞时固件宁可丢整帧也不阻塞。
   - NimBLE 空闲 msys 块低于 6 块余量时，跳过音频和周期 HELLO，给 Mac 控制写入的响应留空间。
   - 按键和 START/STOP 帧最多重试 150 ms。
   - 只有链路断开才进入离线（`main/mic_app.c` 的 `send_frame`，`main/mic_ble.c` 的 `mic_ble_send`）。
   - `sdkconfig.defaults` 设 `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT=24`。
3. 主机命令（`P` 心跳、`A` 开始确认、`X` 复位）用原子标志位，主循环再忙也不会拒收 Mac 的写入。
   同一次写入中 `X` 会覆盖之前的位。
4. 桥接把 25 帧以内的缺口补静音，并打印 `蓝牙丢失 N 帧，已补静音`；在下一个 HELLO 之前忽略上下键。
   - 更大缺口、分片损坏、队列溢出、过期音频、心跳连续 4 次失败，都以 `LinkLost` 结束会话并自动重连。
     每个 `LinkLost` 都带原因文字。
   - 控制写入重试 3 次，间隔 50 ms。
   - 只有配对、认证、权限错误（`needs_user`）才退出。
5. 视频是图块包，不要手改。
   - `assets/images/mic_clips.bin`：约 4.9 MB，每秒 6 帧，未压缩 RGB565，16 × 16 图块，用 `EMBED_FILES` 嵌入。
   - 由 `tools/generate_mic_clips.py` 从 `VIDEO/` 生成；需要系统 `python3` 里的 ffmpeg、OpenCV、numpy，
     `.venv` 里没有。片段范围在这个脚本里改。
   - 负责人选择清晰优先于流畅。
   - 倾听视频拆成进入、循环、返回三段；循环片段正放后倒放；单次片段在最接近的待机帧上交回。
   - `main/mic_ui.c` 直接从 Flash 画图块，只重绘变化的图块。每个 LVGL setter 都会触发重绘，所以只在值变化时调用。
6. 气泡短句在 `main/mic_lines.c`，按状态分组。
   - 不会连续重复，也会跳过与当前标题或说明行相同的句子。
   - 刚连上的问候气泡保持约 4 秒。
   - 改了 `main/mic_ui.c` 或 `main/mic_lines.c` 里的任何中文，都要重新运行 `tools/generate_mic_font.py`。
   - 主机渲染会检查每句的字形覆盖，以及宽度不超过 196 像素。
7. 灭屏是可逆的，不让面板睡眠，做法参照 `docs/reference/shinku-chen/ble-active-idle-power.zh_CN.md`。
   - 灭屏 = 背光 0 加 DISPOFF（`bsp_display_set_on(false)`），帧存保留。Sleep In 只留给深睡路径。
   - 灭屏期间 `mic_app.c` 跳过 `mic_ui_update`，视频和 LVGL 都停止重绘。
   - 亮屏顺序：先 `lv_refr_now`，再 DISPON，最后开背光，所以不会闪出旧画面。
   - 计时逻辑是 `main/mic_screen.c` 里的纯 C 代码，有主机测试。门限是 `MIC_SCREEN_DIM_MS` 和
     `MIC_SCREEN_OFF_MS`，亮度是 `main/mic_app.c` 里的 `BACKLIGHT_ON` 和 `BACKLIGHT_DIM`。
   - 按键先经过 `mic_screen_key` 再执行动作。它会亮屏，只有灭屏时的上下键返回 false。

## 省电

已完成：屏幕的暗屏和灭屏两档。没有实测功耗；设备只能报 CW2017 的电量百分比和电压，
可以比较待机一小时前后的电量。

候选方案，按预计收益从大到小：
1. 待机时挂起 ES8311。`main/mic_app.c` 不录音时也每 20 ms 读一次 I2S，灭屏期间也一样。
   `bsp_audio_sleep()` 和 `bsp_audio_wake()` 已有。唤醒必须在 START 之前完成；需要实测增加的开始延迟。
2. 长时间离线后深睡，任意键唤醒（GPIO0 低电平）。复用 `main/demo_low_power.c` 的序列和
   `main/key_guard.h` 的唤醒守卫。设备会重启，并用已有绑定重连。连接期间不能这样做。
3. `CONFIG_PM_ENABLE` 只开 DFS（空闲时 CPU 降到 40 MHz）。上面的参考文档说它和活跃的 BLE 链路兼容。
   自动浅睡（`light_sleep_enable`）会让链路断开，不要开。
4. 待机时放宽 BLE 连接 latency。用户说暂时不动。

## 代码索引

- 固件：
  - `main/mic_app.c`：主循环、按键、传输、协议帧、屏幕档位。
  - `main/mic_screen.c/.h`：待机暗屏/灭屏计时和按键规则。
  - `main/mic_ble.c/.h`：NimBLE、分片、可丢弃发送、命令标志位。
  - `main/mic_ui.c`：界面。
  - `main/mic_clips.c/.h`：视频包解析与播放。
  - `main/mic_lines.c/.h`：气泡短句。
  - `main/mic_adpcm.c/.h`、`main/mic_protocol.c/.h`：编解码与帧格式。
  - `components/bsp/src/bsp_display.c`：`bsp_display_set_on`（DISPON/DISPOFF）与背光。
- Mac 端：
  - `companion/menubar.py`：菜单栏外壳（PyObjC）。
  - `companion/bridge_status.py`：把桥接输出行转成状态。
  - `companion/passport_mic.py`：桥接本体。
  - `companion/protocol.py`、`companion/adpcm.py`：解析与解码。
  - `companion/chatterfly_key.swift`：按键助手，由 `companion/build-mac.sh` 编译到 `build/mac/chatterfly-key`。
  - `companion/Start.command`：启动脚本。
- 素材：
  - `assets/images/mic-character.png`：原图。
  - `assets/images/mic-character-1x1.png`：生成视频用的 1:1 首帧。
  - `assets/images/mic_clips.bin`、`assets/images/mic-clips-preview.png`。
  - 原始视频在 `VIDEO/`。
- 测试：
  - `tests/test_mic_clips.c`、`tests/test_mic_screen.c`、`tests/test_mic_menubar.py`：都在静态门禁里。
  - `tests/test_mic_ble_session.py`：11 项，不在门禁里，用 `.venv/bin/python` 运行。
  - `tests/mic_ui/render.c`：LVGL 主机渲染，包含截图和短句检查。

## 构建与验证备注

- 固件：`source /Users/xuanhe/Documents/FoloToy/activate-esp-idf.sh`，然后 `./tools/validate.sh --firmware`。
  归档写到 `build/firmware/<完整镜像 sha256>/`。
- 主机测试必须指定 SDK，否则默认的 MacOSX27 SDK 会导致链接失败（`ld: tapi error`）：
  `SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk ./tools/validate.sh --static`。
  `xcrun swiftc` 也用同一个 `SDKROOT`。
- LVGL 主机渲染：`PATH=/Users/xuanhe/Documents/FoloToy/tooling/python/bin:$PATH` 加上同一个 `SDKROOT`，
  运行 `cmake --build build/ui && (cd build/ui && ./render_mic)`。第一次需要先
  `cmake -S tests/mic_ui -B build/ui -G Ninja`。
- 字库：`PATH=/Users/xuanhe/Documents/FoloToy/tooling/node_modules/.bin:$PATH python3 tools/generate_mic_font.py /Users/xuanhe/Documents/FoloToy/tooling/NotoSansCJKsc-Regular.otf`。
- 仓库规则：
  - Markdown 只能放在 `docs/` 或 `.github/` 下，中英成对，并互相带语言链接。
  - 英文版正文不能有中文。
- 最近结果（`20b6c8c7`）：固件门禁 PASS，静态门禁 PASS（含屏幕测试），BLE 会话测试 11/11 PASS，
  仓库检查 PASS。

## 待测项目

1. 在 `20b6c8c7` 上确认 5% 暗屏亮度合适、没有闪烁。
2. 可选：在设备上确认新文案和问候气泡。
3. 连续听写 5–10 分钟（用户推迟过）；顺带确认长时间录音时屏幕保持常亮。
4. 离开 5–10 米再返回，测试自动重连（用户在 2026-10-16 跳过了）。
5. Mac 睡眠唤醒后菜单栏桥接能否恢复。
6. 重启设备，确认在建立新的认证会话前保持空闲。

测试时焦点不要停在 Agent 的终端上，否则下键会给 Agent 发空消息；用户发来的空消息通常是这个原因，不是回复。
只有刷机、读日志或切换有线备用时才插 USB。
