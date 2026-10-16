"""Audio path checks; requires companion/requirements.txt, no audio device I/O."""
import pathlib
import sys
import unittest
import numpy as np
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'companion'))
from passport_mic import AudioBuffer


class AudioTests(unittest.TestCase):
    def test_silence_stereo_and_partial_consumption(self):
        audio = AudioBuffer()
        out = np.ones((480, 2), dtype=np.float32)
        audio.callback(out, 480, None, None)
        self.assertTrue(np.all(out == 0))
        audio.push(np.full(320, 16384, dtype='<i2').tobytes())
        audio.callback(out, 480, None, None)
        self.assertEqual(audio.samples, 480)
        self.assertTrue(np.array_equal(out[:, 0], out[:, 1]))
        self.assertTrue(np.allclose(out[3:], 0.5))
        audio.callback(out, 480, None, None)
        self.assertTrue(np.allclose(out, 0.5))
        self.assertEqual(audio.samples, 0)

    def test_stream_continuity_bounded_latency_and_clear(self):
        audio = AudioBuffer()
        pcm = np.full(320, -32768, dtype='<i2').tobytes()
        audio.push(pcm)
        audio.push(pcm)
        out = np.empty((1920, 2), dtype=np.float32)
        audio.callback(out, 1920, None, None)
        self.assertTrue(np.allclose(out[3:], -1))
        for _ in range(100):
            audio.push(pcm)
        self.assertLessEqual(audio.samples, 12000)
        self.assertGreater(audio.overflows, 0)
        audio.clear()
        audio.callback(out, 1920, None, None)
        self.assertTrue(np.all(out == 0))


if __name__ == '__main__':
    unittest.main()
