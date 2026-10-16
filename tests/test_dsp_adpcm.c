#include "../src/audio/dsp_adpcm.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    int16_t coeff[16] = {0}, history[2] = {0}, out[28];
    uint8_t frame[8] = {0, 0x12, 0x78, 0x9f, 0, 0, 0, 0};
    assert(hy_dsp_decode(frame, sizeof frame, coeff, history, out, 14) == 0);
    assert(out[0] == 1 && out[1] == 2 && out[2] == 7 && out[3] == -8 && out[4] == -7 && out[5] == -1);
    coeff[0] = 2048; history[0] = 10; history[1] = 0;
    assert(hy_dsp_decode(frame, sizeof frame, coeff, history, out, 2) == 0 && out[0] == 11 && out[1] == 13);
    frame[0] = 15; frame[1] = 0x78; coeff[0] = 0; history[0] = history[1] = 0;
    assert(hy_dsp_decode(frame, sizeof frame, coeff, history, out, 2) == 0 && out[0] == 32767 && out[1] == -32768);
    frame[0] = 0x80;
    assert(hy_dsp_decode(frame, sizeof frame, coeff, history, out, 14) != 0);
    frame[0] = 0;
    assert(hy_dsp_decode(frame, 7, coeff, history, out, 14) != 0);
    puts("PASS: DSP ADPCM signed nibbles, history, saturation and malformed frames");
}
