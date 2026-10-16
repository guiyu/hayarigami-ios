#include "dsp_adpcm.h"
int hy_dsp_decode(const uint8_t *data, size_t bytes, const int16_t coefficients[16],
                  int16_t history[2], int16_t *output, size_t sample_count) {
    size_t position = 0, produced = 0;
    int32_t previous = history[0], preceding = history[1];
    while (produced < sample_count) {
        size_t count = sample_count - produced; if (count > 14) count = 14;
        size_t required = 1 + (count + 1) / 2;
        if (position > bytes || required > bytes - position) return -1;
        unsigned header = data[position], predictor = header >> 4;
        if (predictor >= 8) return -1;
        int32_t scale = 1 << (header & 15);
        int16_t a = coefficients[predictor * 2], b = coefficients[predictor * 2 + 1];
        for (size_t i = 0; i < count; i++) {
            unsigned packed = data[position + 1 + i / 2];
            int32_t nibble = i & 1 ? packed & 15 : packed >> 4;
            if (nibble >= 8) nibble -= 16;
            int64_t value = (int64_t)nibble * scale * 2048 + (int64_t)a * previous + (int64_t)b * preceding + 1024;
            value = value >= 0 ? value / 2048 : -((-value + 2047) / 2048);
            if (value > 32767) value = 32767;
            if (value < -32768) value = -32768;
            output[produced++] = (int16_t)value; preceding = previous; previous = (int32_t)value;
        }
        position += 8;
    }
    history[0] = (int16_t)previous; history[1] = (int16_t)preceding;
    return 0;
}
