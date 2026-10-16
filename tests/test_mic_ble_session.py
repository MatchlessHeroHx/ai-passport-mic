"""Run the actual BLE session with a fake radio; never posts real keyboard events."""
import asyncio
import pathlib
import struct
import sys
import unittest
import zlib
from unittest.mock import patch

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'companion'))
import passport_mic as bridge
from protocol import HELLO, START, AUDIO, STOP, UNDO, CONFIRM, AUDIO_ADPCM
from bleak.exc import BleakError


class FakeStream:
    def __enter__(self): return self
    def __exit__(self, *args): return False


class FakeClient:
    def __init__(self, scenario):
        self.is_connected = True
        self.scenario = scenario
        self.callback = None
        self.sequence = 0
        self.writes = []
        self.busy = 2  # Rejected heartbeats before the device drains its queue.

    def emit(self, kind, payload=b''):
        seq = self.sequence
        self.sequence += 1
        meta = struct.pack('<BBHI', kind, 0, len(payload), seq)
        raw = b'FPM1' + meta + struct.pack('<I', zlib.crc32(meta + payload)) + payload
        count = (len(raw) + 175) // 176
        for i in range(count):
            self.callback(None, b'\xb5\x1a' + seq.to_bytes(2, 'little') + bytes([i, count]) + raw[i*176:(i+1)*176])

    async def start_notify(self, uuid, callback):
        self.callback = callback
        self.emit(HELLO, b'\0')

    async def stop_notify(self, uuid):
        raise BleakError('teardown failed')

    def disconnect(self):
        self.is_connected = False

    async def write_gatt_char(self, uuid, data, response):
        self.writes.append(data)
        if data == b'XP':
            self.emit(HELLO, b'\1')
            if self.scenario == 'idle':
                self.emit(UNDO)
                self.emit(CONFIRM)
                asyncio.get_running_loop().call_later(.1, self.disconnect)
            else:
                self.emit(START)
        elif data == b'P' and self.scenario == 'slow_heartbeat':
            await asyncio.sleep(.8)
        elif data == b'P' and self.scenario == 'congested' and self.busy > 0:
            self.busy -= 1
            raise BleakError('Error Domain=CBATTErrorDomain Code=17 "Resources are insufficient."')
        elif data == b'A':
            if self.scenario == 'gap':
                # A lost frame must not let a key event through before HELLO resyncs.
                self.sequence += 1
                self.emit(CONFIRM)
                asyncio.get_running_loop().call_later(.1, self.disconnect)
            elif self.scenario == 'audio_gap':
                self.emit(AUDIO_ADPCM, bytes(164))
                self.sequence += 1
                self.emit(AUDIO_ADPCM, bytes(164))
                self.emit(STOP)
                self.emit(HELLO, b'\1')
                self.emit(CONFIRM)
                asyncio.get_running_loop().call_later(.7, self.disconnect)
            elif self.scenario == 'big_gap':
                self.sequence += 40
                self.emit(AUDIO_ADPCM, bytes(164))
            elif self.scenario == 'congested':
                self.emit(AUDIO_ADPCM, bytes(164))
                asyncio.get_running_loop().call_later(1.2, lambda: (self.emit(STOP), self.emit(CONFIRM)))
                asyncio.get_running_loop().call_later(1.9, self.disconnect)
            elif self.scenario in ('normal', 'burst', 'slow_heartbeat', 'overflow'):
                def burst():
                    count = 65 if self.scenario == 'overflow' else 40 if self.scenario != 'normal' else 1
                    for _ in range(count): self.emit(AUDIO_ADPCM, bytes(164))
                    self.emit(STOP)
                    self.emit(UNDO)
                    self.emit(CONFIRM)
                if self.scenario == 'slow_heartbeat':
                    asyncio.get_running_loop().call_later(.35, burst)
                    asyncio.get_running_loop().call_later(1.3, self.disconnect)
                else:
                    burst()
                    asyncio.get_running_loop().call_later(.6, self.disconnect)
            else:
                self.emit(UNDO)  # Must be ignored while recording.
                self.emit(CONFIRM)
                asyncio.get_running_loop().call_later(.05, self.disconnect)


class BleSessionTests(unittest.IsolatedAsyncioTestCase):
    async def exercise(self, scenario, expected=bridge.LinkLost):
        actions = []
        client = FakeClient(scenario)
        self.client = client
        with patch.object(bridge, 'toggle', side_effect=lambda: actions.append('toggle')), \
             patch.object(bridge, 'undo', side_effect=lambda: actions.append('undo')), \
             patch.object(bridge, 'confirm', side_effect=lambda: actions.append('confirm')), \
             patch.object(bridge.sd, 'OutputStream', return_value=FakeStream()):
            # Every recoverable fault must surface as LinkLost so run_ble reconnects.
            with self.assertRaises(expected):
                await asyncio.wait_for(bridge.ble_session(client, 0), 3)
        return actions

    async def test_notification_burst_preserves_stop_and_edit_events(self):
        self.assertEqual(await self.exercise('burst'), ['toggle', 'toggle', 'undo', 'confirm'])

    async def test_slow_heartbeat_does_not_block_audio_consumption(self):
        self.assertEqual(await self.exercise('slow_heartbeat'), ['toggle', 'toggle', 'undo', 'confirm'])

    async def test_overflow_stops_without_replaying_commands(self):
        self.assertEqual(await self.exercise('overflow'), ['toggle', 'toggle'])

    async def test_idle_buttons(self):
        self.assertEqual(await self.exercise('idle'), ['undo', 'confirm'])

    async def test_normal_record_stop_then_edit(self):
        self.assertEqual(await self.exercise('normal'), ['toggle', 'toggle', 'undo', 'confirm'])

    async def test_disconnect_stops_once_and_ignores_edits(self):
        self.assertEqual(await self.exercise('disconnect'), ['toggle', 'toggle'])

    async def test_sequence_gap_does_not_send_enter_and_teardown_error_still_stops(self):
        self.assertEqual(await self.exercise('gap'), ['toggle', 'toggle'])

    async def test_audio_gap_keeps_dictating_and_edits_resume_after_hello(self):
        self.assertEqual(await self.exercise('audio_gap'), ['toggle', 'toggle', 'confirm'])

    async def test_large_gap_reconnects(self):
        self.assertEqual(await self.exercise('big_gap'), ['toggle', 'toggle'])

    async def test_congested_heartbeat_is_retried(self):
        self.assertEqual(await self.exercise('congested'), ['toggle', 'toggle', 'confirm'])
        self.assertEqual(self.client.busy, 0)


class ReconnectPolicyTests(unittest.TestCase):
    def test_only_pairing_errors_need_the_user(self):
        self.assertTrue(bridge.needs_user(BleakError('CBErrorDomain Code=14 Peer removed pairing information')))
        self.assertTrue(bridge.needs_user(BleakError('Authentication is insufficient')))
        self.assertFalse(bridge.needs_user(BleakError('CBATTErrorDomain Code=17 Resources are insufficient.')))


if __name__ == '__main__':
    unittest.main()
