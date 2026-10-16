#include "mic_adpcm.h"
static const int steps[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
    73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,
    449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,
    2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,
    7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,
    24623,27086,29794,32767
};
static const int adjust[8] = {-1,-1,-1,-1,2,4,6,8};
void mic_adpcm_encode(const int16_t pcm[320], uint8_t out[MIC_ADPCM_BYTES]) {
    int predictor = pcm[0];
    /* Choose a block-local initial step from the mean sample difference. */
    unsigned delta_sum = 0;
    for (int i = 1; i < 320; ++i) {
        int delta = (int)pcm[i] - pcm[i - 1];
        delta_sum += delta < 0 ? -delta : delta;
    }
    int index = 0;
    while (index < 88 && steps[index] < (int)(delta_sum / 319)) ++index;
    out[0] = (uint16_t)predictor;
    out[1] = (uint16_t)predictor >> 8;
    out[2] = index;
    out[3] = 0;
    for (int i = 0; i < 320; ++i) {
        int delta = (int)pcm[i] - predictor;
        int code = delta < 0 ? 8 : 0;
        if (delta < 0) delta = -delta;
        int step = steps[index], difference = step >> 3;
        if (delta >= step) { code |= 4; delta -= step; difference += step; }
        step >>= 1;
        if (delta >= step) { code |= 2; delta -= step; difference += step; }
        step >>= 1;
        if (delta >= step) { code |= 1; difference += step; }
        predictor += (code & 8) ? -difference : difference;
        if (predictor > 32767) predictor = 32767;
        if (predictor < -32768) predictor = -32768;
        index += adjust[code & 7];
        if (index < 0) index = 0;
        if (index > 88) index = 88;
        if (!(i & 1)) out[4 + i / 2] = code << 4;
        else out[4 + i / 2] |= code;
    }
}
