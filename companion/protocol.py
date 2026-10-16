"""Bounded, self-resynchronizing Passport microphone wire decoder."""
import struct
import zlib

MAGIC = b"FPM1"
HELLO, START, AUDIO, STOP, ERROR, UNDO, CONFIRM, AUDIO_ADPCM = range(1, 9)


class Decoder:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while len(self.buffer) >= 16:
            start = self.buffer.find(MAGIC)
            if start < 0:
                del self.buffer[:-3]
                break
            del self.buffer[:start]
            if len(self.buffer) < 16:
                break
            kind, reserved, size, sequence, crc = struct.unpack_from("<BBHII", self.buffer, 4)
            valid_size = (kind == HELLO and size == 1 or
                          kind == AUDIO and size == 640 or
                          kind == AUDIO_ADPCM and size == 164 or
                          kind in (START, STOP, ERROR, UNDO, CONFIRM) and size == 0)
            if reserved or not valid_size:
                del self.buffer[:4]
                continue
            if len(self.buffer) < 16 + size:
                break
            payload = bytes(self.buffer[16:16 + size])
            if zlib.crc32(self.buffer[4:12] + payload) != crc:
                del self.buffer[:4]
                continue
            frames.append((kind, sequence, payload))
            del self.buffer[:16 + size]
        return frames


class Session:
    """At most one start/stop key pair for each device recording session."""
    def __init__(self, toggle, reset_audio, undo=None, confirm=None):
        self.active = False
        self.toggle = toggle
        self.reset_audio = reset_audio
        self.undo_action = undo or (lambda: None)
        self.confirm_action = confirm or (lambda: None)

    def start(self):
        if not self.active:
            self.reset_audio()
            self.toggle()
            self.active = True

    def stop(self):
        if self.active:
            self.toggle()
            self.active = False
            self.reset_audio()

    def undo(self):
        if not self.active:
            self.undo_action()

    def confirm(self):
        if not self.active:
            self.confirm_action()

class BleFrameDecoder:
    """Strict ordered reassembly, bounded to one 656-byte frame (up to 47 parts)."""
    def __init__(self):
        self.frame_id = None
        self.total = 0
        self.next_index = 0
        self.data = bytearray()

    def feed(self, notification):
        if len(notification) < 7 or len(notification) > 244 or notification[:2] != b"\xB5\x1A":
            raise ValueError("Invalid BLE fragment")
        frame_id = int.from_bytes(notification[2:4], "little")
        index, total = notification[4], notification[5]
        if not 1 <= total <= 47 or index >= total:
            raise ValueError("Invalid BLE fragment count")
        if self.frame_id is None:
            if index != 0:
                raise ValueError("Missing first BLE fragment")
            self.frame_id, self.total = frame_id, total
            self.next_index = 0
            self.data.clear()
        if (frame_id, total, index) != (self.frame_id, self.total, self.next_index):
            raise ValueError("Missing or reordered BLE fragment")
        self.data.extend(notification[6:])
        if len(self.data) > 656:
            raise ValueError("Oversized BLE frame")
        self.next_index += 1
        if self.next_index != total:
            return []
        raw = bytes(self.data)
        self.frame_id = None
        self.data.clear()
        frames = Decoder().feed(raw)
        if len(frames) != 1 or len(raw) != 16 + len(frames[0][2]) or not raw.startswith(MAGIC):
            raise ValueError("Invalid BLE frame CRC or length")
        return [raw]
