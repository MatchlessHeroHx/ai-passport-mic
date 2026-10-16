#!/usr/bin/env python3
"""Wireless/USB microphone bridge to BlackHole and Chatterfly."""
import argparse
from collections import deque
from pathlib import Path
import subprocess
import sys
import threading
import time
import asyncio
from contextlib import suppress

import numpy as np
import serial
from serial.tools import list_ports
import sounddevice as sd

from protocol import AUDIO, ERROR, HELLO, START, STOP, UNDO, CONFIRM, AUDIO_ADPCM, POWER, BleFrameDecoder, Decoder, Session, record_power

from adpcm import decode as decode_adpcm

BLE_NAME = "Passport Mic"
BLE_AUDIO_UUID = "12345678-1234-5678-1234-56789abcdef1"
BLE_CONTROL_UUID = "12345678-1234-5678-1234-56789abcdef2"

ROOT = Path(__file__).resolve().parents[1]
KEY = ROOT / "build/mac/chatterfly-key"


class AudioBuffer:
    def __init__(self):
        self.lock = threading.Lock()
        self.blocks = deque()
        self.samples = 0
        self.offset = 0
        self.previous = 0.0
        self.overflows = 0

    def clear(self):
        with self.lock:
            self.blocks.clear()
            self.samples = self.offset = 0
            self.previous = 0.0

    def push(self, pcm):
        mono = np.frombuffer(pcm, dtype="<i2").astype(np.float32) / 32768.0
        # Continuous linear 16 kHz -> 48 kHz conversion, including block boundaries.
        with self.lock:
            points = np.concatenate(([self.previous], mono))
            block = np.interp(np.arange(960) / 3, np.arange(321), points).astype(np.float32)
            self.previous = float(mono[-1])
            if self.samples + len(block) > 12000:
                self.blocks.clear()
                self.samples = self.offset = 0
                self.overflows += 1
            self.blocks.append(block)
            self.samples += len(block)

    def callback(self, outdata, frames, timing, status):
        outdata.fill(0)
        written = 0
        with self.lock:
            while self.blocks and written < frames:
                block = self.blocks[0]
                count = min(frames - written, len(block) - self.offset)
                outdata[written:written + count, :] = block[self.offset:self.offset + count, None]
                written += count
                self.offset += count
                self.samples -= count
                if self.offset == len(block):
                    self.blocks.popleft()
                    self.offset = 0


def toggle():
    subprocess.run([str(KEY)], check=True)

def undo():
    subprocess.run([str(KEY), "--undo"], check=True)

def confirm():
    subprocess.run([str(KEY), "--confirm"], check=True)


def audio_device():
    found = [(i, d) for i, d in enumerate(sd.query_devices())
             if d["name"] == "BlackHole 2ch" and d["max_output_channels"] >= 2]
    if len(found) != 1:
        raise RuntimeError("请安装 BlackHole 2ch，重启后在 Chatterfly 中选择它作为麦克风。")
    return found[0][0]


def select_port(explicit):
    ports = [p.device for p in list_ports.comports() if p.vid == 0x303A and p.pid == 0x1001]
    if explicit:
        if explicit not in ports:
            raise RuntimeError("指定串口不是已枚举的 ESP32-C3 USB Serial/JTAG 设备。")
        return explicit
    if len(ports) != 1:
        raise RuntimeError("请连接一台 AI Passport；有多台时使用 --port 指定串口。")
    return ports[0]


def run(port, device):
    audio = AudioBuffer()
    session = Session(toggle, audio.clear, undo, confirm)
    decoder = Decoder()
    connected = last_frame = time.monotonic()
    last_ping = 0.0
    last_sequence = None
    verified = False
    # pyserial's default DTR/RTS can reset an ESP32: configure before open.
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.05, write_timeout=0.5)
    connection.dtr = connection.rts = False
    connection.port = port
    try:
        connection.open()
        with sd.OutputStream(device=device, samplerate=48000, channels=2,
                             dtype="float32", blocksize=960, callback=audio.callback):
            print("已连接。请先在 Chatterfly 选择 BlackHole 2ch，并保持听写关闭。按设备确定键开始。", flush=True)
            while True:
                now = time.monotonic()
                if verified and now - last_ping >= 0.5:
                    connection.write(b"P")
                    last_ping = now
                frames = decoder.feed(connection.read(4096))
                for kind, sequence, payload in frames:
                    if not verified:
                        if kind != HELLO:
                            continue
                        verified = True
                        # Reconnection is always idle, never resume an old session.
                        connection.write(b"XP")
                    last_frame = time.monotonic()
                    if last_sequence is not None and sequence != (last_sequence + 1) & 0xffffffff:
                        raise RuntimeError("音频传输中断，已停止听写。重新打开程序再试。")
                    last_sequence = sequence
                    if kind == HELLO and payload == b"\xff" or kind == ERROR:
                        raise RuntimeError("设备麦克风初始化或采音失败，请重启设备。")
                    if kind == START:
                        session.start()
                        time.sleep(0.2)
                        connection.write(b"A")
                        print("正在听写…", flush=True)
                    elif kind in (AUDIO, AUDIO_ADPCM) and session.active:
                        audio.push(decode_adpcm(payload) if kind == AUDIO_ADPCM else payload)
                    elif kind == POWER:
                        record_power(payload, "usb")
                    elif kind == UNDO:
                        session.undo()
                        print("已发送撤销快捷键。", flush=True)
                    elif kind == CONFIRM:
                        session.confirm()
                        print("已发送回车。", flush=True)
                    elif kind == STOP or kind == HELLO and payload[0] in (0, 1) and session.active:
                        # Let the already queued last syllable reach the audio input.
                        deadline = time.monotonic() + 0.3
                        while audio.samples and time.monotonic() < deadline:
                            time.sleep(0.01)
                        time.sleep(0.04)
                        session.stop()
                        print("已结束，按确定键继续。", flush=True)
                if not verified and now - connected > 5:
                    raise RuntimeError("串口尚未运行 Passport Mic 固件；请先完成烧录。")
                if verified and now - last_frame > 2:
                    raise RuntimeError("USB 连接中断，已停止听写。重新连接并打开程序。")
    finally:
        try:
            session.stop()
        finally:
            if connection.is_open:
                try:
                    connection.write(b"X")
                except serial.SerialException:
                    pass
                connection.close()



class LinkLost(RuntimeError):
    """A disconnected device can reconnect after the active session is stopped."""


async def run_ble(device_index, address=None):
    from bleak import BleakClient, BleakScanner
    from bleak.exc import BleakError
    # Keep the selected peripheral identity across reconnects; never silently switch.
    selected = address
    initial_attempts = 0
    while True:
        print("正在查找 Passport Mic…", flush=True)
        try:
            devices = await BleakScanner.discover(timeout=5, return_adv=True,
                service_uuids=["12345678-1234-5678-1234-56789abcdef0"])
            initial_attempts = 0
        except BleakError as exc:
            initial_attempts += 1
            if "turned off" in str(exc) and initial_attempts < 3:
                await asyncio.sleep(2)
                continue
            raise RuntimeError(f"蓝牙扫描失败：{exc}。请检查系统蓝牙状态与权限。") from exc
        matches = [device for device, adv in devices.values()
                   if (selected is None or device.address == selected)
                   and "12345678-1234-5678-1234-56789abcdef0" in
                   [uuid.lower() for uuid in adv.service_uuids]]
        if len(matches) > 1:
            raise RuntimeError("附近有多台 Passport Mic，请使用 --ble-address 指定设备 UUID。")
        if not matches:
            print("等待设备开机或回到蓝牙范围；Ctrl-C 退出。", flush=True)
            await asyncio.sleep(2)
            continue
        selected = matches[0].address
        try:
            async with BleakClient(matches[0], timeout=60) as client:
                # Protected read lets CoreBluetooth show its pairing/passkey dialog.
                print("首次连接请在 Mac 输入设备屏幕上的六位配对码。", flush=True)
                identity = await client.read_gatt_char(BLE_CONTROL_UUID)
                if bytes(identity) != b"FPM1":
                    raise RuntimeError("蓝牙设备协议不匹配。")
                await ble_session(client, device_index)
        except LinkLost as exc:
            print(f"{str(exc) or '蓝牙已断开'}，本次听写已停止。正在重连…", flush=True)
            await asyncio.sleep(2)
        except BleakError as exc:
            # Pairing/permission errors need an explicit user correction.
            if needs_user(exc):
                raise RuntimeError(f"蓝牙连接失败：{exc}。检查蓝牙权限和配对信息后重新打开。") from exc
            print(f"蓝牙通信错误：{exc}。本次听写已停止。正在重连…", flush=True)
            await asyncio.sleep(2)
        except asyncio.TimeoutError:
            print("蓝牙控制写入超时，本次听写已停止。正在重连…", flush=True)
            await asyncio.sleep(2)


def needs_user(exc):
    """Bond, encryption and permission failures cannot be fixed by reconnecting."""
    text = str(exc).lower()
    return any(marker in text for marker in (
        "pairing", "authentication", "encryption", "not authorized", "unauthorized",
        "cberrordomain code=14"))


async def ble_session(client, device_index):
    from bleak.exc import BleakError
    audio = AudioBuffer()
    session = Session(toggle, audio.clear, undo, confirm)
    # CoreBluetooth may deliver several connection intervals in one callback burst.
    # Bound memory separately from audio age; controls must never be dropped.
    queue = asyncio.Queue(maxsize=64)
    chunks = BleFrameDecoder()
    failure = None
    verified = False
    ready = False
    resync = False
    last_sequence = None
    last_frame = time.monotonic()
    write_lock = asyncio.Lock()

    async def key_action(action):
        # Cancellation must not leave a background key press racing stop/cleanup.
        task = asyncio.create_task(asyncio.to_thread(action))
        try:
            await asyncio.shield(task)
        except asyncio.CancelledError:
            await task
            raise

    async def write_control(data):
        # The device rejects writes while its command queue or NimBLE buffers are full
        # (CBATT "Resources are insufficient"); a short retry clears that congestion.
        async with write_lock:
            for attempt in range(3):
                try:
                    await asyncio.wait_for(client.write_gatt_char(BLE_CONTROL_UUID, data, response=True), 1.5)
                    return
                except BleakError:
                    if not client.is_connected:
                        raise LinkLost("蓝牙已断开")
                    if attempt == 2:
                        raise
                    await asyncio.sleep(0.05)

    async def heartbeat():
        # The device drops the link after 3 s without a ping; tolerate up to 2 s of misses.
        misses = 0
        while True:
            if verified:
                try:
                    await write_control(b"P")
                    misses = 0
                except (BleakError, asyncio.TimeoutError):
                    misses += 1
                    if misses >= 4:
                        raise LinkLost("蓝牙心跳连续失败")
            await asyncio.sleep(0.5)

    def on_notification(_characteristic, payload):
        nonlocal failure
        if failure is not None:
            return
        try:
            for wire in chunks.feed(bytes(payload)):
                queue.put_nowait((time.monotonic(), wire))
        except asyncio.QueueFull:
            failure = LinkLost("蓝牙接收队列超过 64 帧")
        except ValueError as exc:
            failure = LinkLost(f"蓝牙音频数据损坏（{exc}）")

    await client.start_notify(BLE_AUDIO_UUID, on_notification)
    ping_task = asyncio.create_task(heartbeat())
    try:
        with sd.OutputStream(device=device_index, samplerate=48000, channels=2,
                             dtype="float32", blocksize=960, callback=audio.callback):
            print("蓝牙已连接。保持 Chatterfly 听写关闭，等设备就绪后按确定键。", flush=True)
            while True:
                if not client.is_connected:
                    raise LinkLost("蓝牙已断开")
                if failure:
                    raise failure
                now = time.monotonic()
                if ping_task.done():
                    await ping_task
                try:
                    received, wire = queue.get_nowait()
                except asyncio.QueueEmpty:
                    try:
                        received, wire = await asyncio.wait_for(queue.get(), timeout=0.05)
                    except asyncio.TimeoutError:
                        wire = None
                if wire is not None:
                    for kind, sequence, payload in Decoder().feed(wire):
                        if not verified:
                            if kind != HELLO:
                                continue
                            verified = True
                            await write_control(b"XP")
                        last_frame = time.monotonic()
                        if last_sequence is not None and sequence != (last_sequence + 1) & 0xffffffff:
                            # The firmware drops a frame it cannot send. Usually that is 20 ms
                            # of audio: keep timing with silence. A lost START/STOP is repaired
                            # by the next HELLO state, so edit keys wait until then.
                            missing = (sequence - last_sequence - 1) & 0xffffffff
                            if missing > 25:
                                raise LinkLost(f"蓝牙丢失 {missing} 帧")
                            print(f"蓝牙丢失 {missing} 帧，已补静音。", flush=True)
                            if session.active:
                                for _ in range(missing):
                                    audio.push(bytes(640))
                            ready = False
                            resync = True
                        last_sequence = sequence
                        if kind == ERROR or (kind == HELLO and payload == b"\xff"):
                            raise RuntimeError("设备采音失败，请重启设备。")
                        if kind == HELLO:
                            ready = payload == b"\x01"
                            resync = False
                        elif resync and kind in (UNDO, CONFIRM):
                            continue
                        if kind == START:
                            ready = False
                            await key_action(session.start)
                            await asyncio.sleep(0.2)
                            await write_control(b"A")
                            print("正在听写（BLE）…", flush=True)
                        elif kind in (AUDIO, AUDIO_ADPCM) and session.active:
                            if time.monotonic() - received > 0.5:
                                raise LinkLost("蓝牙音频等待超过 500 ms")
                            audio.push(decode_adpcm(payload) if kind == AUDIO_ADPCM else payload)
                        elif kind == POWER:
                            record_power(payload, "ble")
                        elif kind == UNDO and ready and not session.active:
                            await key_action(session.undo)
                            print("已发送撤销快捷键。", flush=True)
                        elif kind == CONFIRM and ready and not session.active:
                            await key_action(session.confirm)
                            print("已发送回车。", flush=True)
                        elif kind == STOP or (kind == HELLO and payload[0] in (0, 1) and session.active):
                            deadline = time.monotonic() + 0.3
                            while audio.samples and time.monotonic() < deadline:
                                await asyncio.sleep(0.01)
                            await asyncio.sleep(0.04)
                            await key_action(session.stop)
                            ready = kind == STOP or payload == b"\x01"
                            print("已结束，按确定键继续。", flush=True)
                if now - last_frame > 3:
                    raise LinkLost("超过 3 秒没收到设备数据")
    except BleakError:
        if not client.is_connected:
            raise LinkLost("蓝牙已断开")
        raise
    finally:
        # Always release dictation, even if BLE teardown raises after disconnect.
        ping_task.cancel()
        with suppress(asyncio.CancelledError, Exception):
            await ping_task
        session.stop()
        if client.is_connected:
            try:
                await asyncio.wait_for(client.write_gatt_char(BLE_CONTROL_UUID, b"X", response=True), 1)
                await asyncio.wait_for(client.stop_notify(BLE_AUDIO_UUID), 1)
            except (BleakError, asyncio.TimeoutError):
                pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--ble-address", help="CoreBluetooth device UUID when multiple microphones are nearby")
    parser.add_argument("--transport", choices=("ble", "usb"), default="ble", help="Default: BLE wireless; use usb for wired fallback")
    parser.add_argument("--check", action="store_true", help="Check dependencies without opening USB or sending keys")
    args = parser.parse_args()
    try:
        device = audio_device()
        subprocess.run([str(KEY), "--check"], check=True)
        if args.transport == "ble":
            try:
                import bleak  # noqa: F401
            except ImportError as exc:
                raise RuntimeError("无线模式需要 bleak：请重新运行 companion/build-mac.sh。") from exc
            if args.check:
                print("Ready: BLE dependencies, BlackHole 2ch, accessibility granted (radio/pairing not tested)")
            else:
                asyncio.run(run_ble(device, args.ble_address))
        else:
            port = select_port(args.port)
            if args.check:
                print(f"Ready: {port}, USB Passport Mic, BlackHole 2ch, accessibility granted")
            else:
                run(port, device)
    except KeyboardInterrupt:
        return 0
    except (RuntimeError, OSError, subprocess.CalledProcessError, sd.PortAudioError) as exc:
        print(f"Passport Mic: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
