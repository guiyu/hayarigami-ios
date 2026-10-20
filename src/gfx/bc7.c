#include "bc7.h"
#define BCDEC_IMPLEMENTATION
#include "../../third_party/bcdec/bcdec.h"
#include <string.h>
int hy_bc7_decode(const void *blocks,size_t source_size,uint32_t width,uint32_t height,
                  void *pixels,size_t pitch,size_t destination_size) {
    if (!blocks || !pixels || !width || !height || width>16384 || height>16384 || pitch<(size_t)width*4) return -1;
    size_t columns=(width+3)/4,rows=(height+3)/4;
    if (columns*rows*16>source_size || pitch>SIZE_MAX/height ||
        (height-1)*pitch+(size_t)width*4>destination_size) return -1;
    for (size_t y=0;y<rows;y++) for (size_t x=0;x<columns;x++) {
        _Alignas(8) uint8_t encoded[16],decoded[64];
        memcpy(encoded,(const uint8_t *)blocks+(y*columns+x)*16,16);
        bcdec_bc7(encoded,decoded,16);
        size_t w=width-x*4,h=height-y*4;if(w>4)w=4;if(h>4)h=4;
        for(size_t row=0;row<h;row++) memcpy((uint8_t *)pixels+(y*4+row)*pitch+x*16,decoded+row*16,w*4);
    }
    return 0;
}
