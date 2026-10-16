#ifndef HY_AUDIO_OUTPUT_H
#define HY_AUDIO_OUTPUT_H
#include <stddef.h>
#include <stdint.h>
int hy_audio_output_open(void);
int hy_audio_output_start(void);
void hy_audio_output_stop(void);
int hy_audio_output_push(const int16_t *interleaved, size_t frames);
size_t hy_audio_output_pending(void);
size_t hy_audio_output_target(void);
void hy_audio_output_close(void);
#endif
