#include "mic_adpcm.h"
#include <assert.h>
#include <stdio.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    FILE *out = fopen(argv[1], "wb");
    assert(out);
    for (int mode = 0; mode < 5; ++mode) {
        int16_t pcm[320];
        for (int i = 0; i < 320; ++i) {
            int triangle = i % 80 < 40 ? i % 80 : 80 - i % 80;
            pcm[i] = mode == 0 ? 0 : mode == 1 ? 32767 : mode == 2 ? -32768 :
                     mode == 3 ? (triangle - 20) * 800 : (i & 1 ? 32767 : -32768);
        }
        uint8_t encoded[164];
        mic_adpcm_encode(pcm, encoded);
        assert(encoded[2] <= 88 && encoded[3] == 0);
        assert(fwrite(pcm, 1, sizeof(pcm), out) == sizeof(pcm));
        assert(fwrite(encoded, 1, sizeof(encoded), out) == sizeof(encoded));
    }
    assert(fclose(out) == 0);
}
