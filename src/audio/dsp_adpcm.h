#ifndef HY_DSP_ADPCM_H
#define HY_DSP_ADPCM_H
#include <stddef.h>
#include <stdint.h>
// Decode mono Nintendo DSP ADPCM. history = {most recent, preceding sample}.
// Output must hold sample_count int16 values; returns -1 on malformed input.
int hy_dsp_decode(const uint8_t *data, size_t bytes, const int16_t coefficients[16],
                  int16_t history[2], int16_t *output, size_t sample_count);
#endif
