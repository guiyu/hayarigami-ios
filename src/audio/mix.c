#include "mix.h"
static int16_t clamp(int32_t value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return (int16_t)value;
}
int hy_audio_stereo(const int16_t *p, int channels, size_t frames, int16_t *out) {
    if (channels != 2 && channels != 6) return -1;
    for (size_t i = 0; i < frames; i++) {
        if (channels == 2) { out[i * 2] = p[i]; out[i * 2 + 1] = p[frames + i]; continue; }
        // Six-channel convention: FL FR C LFE SL SR. Stereo Lo/Ro with 3 dB
        // headroom, centre/surround preserved and the dedicated LFE omitted.
        int32_t left = 23170 * (int32_t)p[i] + 16384 * ((int32_t)p[2 * frames + i] + p[4 * frames + i]);
        int32_t right = 23170 * (int32_t)p[frames + i] + 16384 * ((int32_t)p[2 * frames + i] + p[5 * frames + i]);
        out[i * 2] = clamp(left / 32768); out[i * 2 + 1] = clamp(right / 32768);
    }
    return 0;
}
