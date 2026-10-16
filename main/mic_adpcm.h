#pragma once
#include <stdint.h>
/* Independently decodable 20 ms block: initial predictor LE, index, reserved,
   then 320 IMA ADPCM codes, high nibble first. */
#define MIC_ADPCM_BYTES 164
void mic_adpcm_encode(const int16_t pcm[320], uint8_t out[MIC_ADPCM_BYTES]);
