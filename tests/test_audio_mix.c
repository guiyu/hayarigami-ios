#include "../src/audio/mix.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void) {
    int16_t in[6], out[2];
    for (int c = 0; c < 6; c++) {
        memset(in, 0, sizeof in); in[c] = 10000;
        assert(!hy_audio_stereo(in, 6, 1, out));
        int left[] = {7070, 0, 5000, 0, 5000, 0}, right[] = {0, 7070, 5000, 0, 0, 5000};
        assert(out[0] == left[c] && out[1] == right[c]);
    }
    for (int c = 0; c < 6; c++) in[c] = 32767;
    assert(!hy_audio_stereo(in, 6, 1, out) && out[0] == 32767 && out[1] == 32767);
    for (int c = 0; c < 6; c++) in[c] = -32768;
    assert(!hy_audio_stereo(in, 6, 1, out) && out[0] == -32768 && out[1] == -32768);
    in[0] = 123; in[1] = -456;
    assert(!hy_audio_stereo(in, 2, 1, out) && out[0] == 123 && out[1] == -456);
    assert(hy_audio_stereo(in, 3, 1, out) == -1);
    puts("PASS: six-channel downmix isolation, centre/surround routing, saturation and stereo identity");
}
