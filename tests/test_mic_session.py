"""Exercise the actual bridge loop with a fake USB transport and audio sink."""
import pathlib
import struct
import sys
import unittest
from unittest.mock import patch
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'companion'))
import passport_mic as bridge
from protocol import AUDIO, HELLO, START, STOP


def frame(kind, seq, payload=b''):
    meta = struct.pack('<BBHI', kind, 0, len(payload), seq)
    return b'FPM1' + meta + struct.pack('<I', zlib.crc32(meta + payload)) + payload


class FakeSerial:
    def __init__(self, packets):
        self.packets = iter(packets)
        self.writes = []
        self.is_open = False

    def open(self):
        self.is_open = True

    def close(self):
        self.is_open = False

    def read(self, count):
        try:
            return next(self.packets)
        except StopIteration:
            raise KeyboardInterrupt()

    def write(self, data):
        self.writes.append(data)
        return len(data)


class FakeStream:
    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False


class BridgeTests(unittest.TestCase):
    def exercise(self, packets, expected_error):
        serial = FakeSerial(packets)
        keys = []
        with patch.object(bridge.serial, 'Serial', return_value=serial), \
             patch.object(bridge.sd, 'OutputStream', return_value=FakeStream()), \
             patch.object(bridge, 'toggle', side_effect=lambda: keys.append('key')):
            with self.assertRaises(expected_error):
                bridge.run('/fake', 0)
        self.assertFalse(serial.is_open)
        self.assertIn(b'XP', serial.writes)
        self.assertIn(b'A', serial.writes)
        self.assertEqual(serial.writes[-1], b'X')
        self.assertEqual(keys, ['key', 'key'])

    def test_normal_start_stop(self):
        self.exercise([frame(HELLO, 0, b'\0'), frame(START, 1),
                       frame(AUDIO, 2, bytes(640)), frame(STOP, 3)], KeyboardInterrupt)

    def test_disconnect_while_recording(self):
        self.exercise([frame(HELLO, 0, b'\0'), frame(START, 1)], KeyboardInterrupt)

    def test_corrupt_stop_cannot_leave_dictation_running(self):
        self.exercise([frame(HELLO, 0, b'\0'), frame(START, 1),
                       frame(HELLO, 3, b'\1')], RuntimeError)


if __name__ == '__main__':
    unittest.main()
