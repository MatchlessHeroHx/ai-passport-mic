"""Cross-check firmware encoder and bridge decoder against Python's audioop codec."""
import pathlib
import struct
import sys
import unittest
import warnings
with warnings.catch_warnings():
    warnings.simplefilter('ignore', DeprecationWarning)
    import audioop
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'companion'))
from adpcm import decode


class CodecTests(unittest.TestCase):
    def test_c_encoder_and_python_decoder_against_audioop(self):
        data = pathlib.Path(sys.argv[1]).read_bytes()
        self.assertEqual(len(data), 5 * 804)
        for offset in range(0, len(data), 804):
            raw, block = data[offset:offset + 640], data[offset + 640:offset + 804]
            predictor, index, _ = struct.unpack_from('<hBB', block)
            expected, state = audioop.lin2adpcm(raw, 2, (predictor, index))
            self.assertEqual(block[4:], expected)
            pcm, _ = audioop.adpcm2lin(block[4:], 2, (predictor, index))
            self.assertEqual(decode(block), pcm)
            if offset == 3 * 804:
                before = struct.unpack('<320h', raw)
                after = struct.unpack('<320h', pcm)
                mae = sum(abs(a-b) for a,b in zip(before,after))/320
                self.assertLess(mae, 300)

    def test_bad_blocks_rejected(self):
        for block in (bytes(163), bytes(165), bytes(2)+b'\x59\0'+bytes(160), bytes(3)+b'\1'+bytes(160)):
            with self.assertRaises(ValueError): decode(block)


if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0]])
