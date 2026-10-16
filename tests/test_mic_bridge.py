import pathlib
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'companion'))
from protocol import AUDIO, START, Decoder, Session


def frame(kind=START, seq=1, payload=b''):
    meta = struct.pack('<BBHI', kind, 0, len(payload), seq)
    return b'FPM1' + meta + struct.pack('<I', zlib.crc32(meta + payload)) + payload


class ProtocolTests(unittest.TestCase):
    def test_fragmented_pcm_and_boot_logs(self):
        decoder = Decoder()
        data = b'boot log\r\n' + frame(AUDIO, 5, bytes(range(256)) * 2 + bytes(128))
        actual = []
        for byte in data:
            actual += decoder.feed(bytes([byte]))
        self.assertEqual(actual, [(AUDIO, 5, data[-640:])])

    def test_corrupt_metadata_payload_and_unbounded_noise(self):
        for pos in (4, 8, 20):
            decoder = Decoder()
            bad = bytearray(frame(AUDIO, 2, bytes(640)))
            bad[pos] ^= 1
            self.assertEqual(decoder.feed(bytes(bad) + frame()), [(START, 1, b'')])
        decoder = Decoder()
        decoder.feed(b'x' * 100000)
        self.assertLessEqual(len(decoder.buffer), 3)

    def test_c_wire_fixture(self):
        fixture = pathlib.Path(sys.argv[1]).read_bytes() if len(sys.argv) > 1 else None
        if fixture is None:
            self.skipTest('No C fixture supplied')
        self.assertEqual(Decoder().feed(fixture), [(AUDIO, 42, bytes(i % 256 for i in range(640)))])

    def test_duplicate_events_and_cleanup(self):
        actions = []
        session = Session(lambda: actions.append('key'), lambda: actions.append('clear'))
        session.stop()
        session.start()
        session.start()
        session.stop()
        session.stop()
        self.assertEqual(actions, ['clear', 'key', 'key', 'clear'])

    def test_failed_start_does_not_claim_active(self):
        def failed():
            raise RuntimeError('no accessibility')
        session = Session(failed, lambda: None)
        with self.assertRaises(RuntimeError):
            session.start()
        self.assertFalse(session.active)


class BleTransportTests(unittest.TestCase):
    @staticmethod
    def split(raw, size=176, frame_id=7):
        count = (len(raw) + size - 1) // size
        return [b'\xb5\x1a' + frame_id.to_bytes(2, 'little') + bytes([i, count])
                + raw[i * size:(i + 1) * size] for i in range(count)]

    def test_mtu_23_185_247_roundtrip(self):
        from protocol import BleFrameDecoder
        raw = frame(AUDIO, 9, bytes(range(256)) * 2 + bytes(128))
        for size in (14, 176, 238):
            decoder = BleFrameDecoder()
            actual = []
            for chunk in self.split(raw, size):
                actual += decoder.feed(chunk)
            self.assertEqual(actual, [raw])

    def test_missing_reordered_duplicate_parts_fail(self):
        from protocol import BleFrameDecoder
        raw = frame(AUDIO, 9, bytes(640))
        chunks = self.split(raw)
        for indices in ((1,), (0, 2), (0, 0), (0, 3)):
            decoder = BleFrameDecoder()
            with self.assertRaises(ValueError):
                for i in indices:
                    decoder.feed(chunks[i])

    def test_bad_crc_and_oversized_frames_fail(self):
        from protocol import BleFrameDecoder
        for raw in (frame(START)[:-1] + b'x', b'x' * 657):
            decoder = BleFrameDecoder()
            with self.assertRaises(ValueError):
                for chunk in self.split(raw):
                    decoder.feed(chunk)

    def test_adpcm_fits_one_mtu_185_notification(self):
        from protocol import AUDIO_ADPCM, BleFrameDecoder
        raw = frame(AUDIO_ADPCM, 9, bytes(164))
        # Frame 180 + envelope 6 = 186; MTU 185 requires two, 247 only one.
        for size in (176, 238):
            decoder = BleFrameDecoder()
            actual = []
            for chunk in self.split(raw, size): actual += decoder.feed(chunk)
            self.assertEqual(actual, [raw])
            self.assertEqual(Decoder().feed(raw), [(AUDIO_ADPCM, 9, bytes(164))])

    def test_frame_id_wrap(self):
        from protocol import BleFrameDecoder
        decoder = BleFrameDecoder()
        for seq in (65535, 0, 1):
            raw = frame(START, seq)
            self.assertEqual(decoder.feed(self.split(raw, frame_id=seq)[0]), [raw])

    def test_edit_events_and_recording_guard(self):
        from protocol import UNDO, CONFIRM
        self.assertEqual(Decoder().feed(frame(UNDO) + frame(CONFIRM, 2)),
                         [(UNDO, 1, b''), (CONFIRM, 2, b'')])
        actions = []
        session = Session(lambda: actions.append('toggle'), lambda: None,
                          lambda: actions.append('undo'), lambda: actions.append('confirm'))
        session.start()
        session.undo()
        session.confirm()
        self.assertEqual(actions, ['toggle'])
        session.stop()
        session.undo()
        session.confirm()
        self.assertEqual(actions, ['toggle', 'toggle', 'undo', 'confirm'])


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
