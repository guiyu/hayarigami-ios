#ifndef HY_AUDIO_MIX_H
#define HY_AUDIO_MIX_H
#include <stdint.h>
#include <stddef.h>
int hy_audio_stereo(const int16_t *planar, int channels, size_t frames, int16_t *stereo);
#endif
