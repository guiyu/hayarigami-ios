#include "../src/gfx/bc7.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    const uint8_t solid[16]={0xc0,0xff,0x1f,0,0,0,0xfe,0x7f,0,0,0,0,0,0,0,0};
    uint8_t blocks[33]={0},out[5*3*4+7];memcpy(blocks+1,solid,16);memcpy(blocks+17,solid,16);
    memset(out,0x69,sizeof out);
    assert(!hy_bc7_decode(blocks+1,32,5,3,out+1,20,60));
    for(unsigned i=0;i<15;i++) assert(!memcmp(out+1+i*4,(uint8_t[]){254,0,0,254},4));
    assert(out[0]==0x69 && out[61]==0x69);
    assert(hy_bc7_decode(blocks+1,31,5,3,out+1,20,60));
    assert(hy_bc7_decode(blocks+1,32,5,3,out+1,19,60));
    assert(hy_bc7_decode(blocks+1,32,5,3,out+1,20,59));
    assert(hy_bc7_decode(blocks+1,32,0,3,out+1,20,60));
    const uint8_t ramp[16]={0x40,0xc0,0x1f,0xf0,0x07,0xfc,0x01,0x7f,0x10,0x32,0x54,0x76,0x98,0xba,0xdc,0xfe};
    const uint8_t values[16]={0,16,36,52,67,83,103,119,135,151,171,187,202,218,238,254};
    uint8_t pixels[64];assert(!hy_bc7_decode(ramp,16,4,4,pixels,16,64));
    for(unsigned i=0;i<16;i++) for(unsigned c=0;c<4;c++) assert(pixels[i*4+c]==values[i]);
    puts("PASS: BC7 solid/ramp goldens, odd edges, unaligned buffers and range rejection");
}
