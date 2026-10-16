#include "../src/gfx/nvn_deswizzle.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void) {
    nvn_texdesc d = {.target=1,.format=0x25,.flags=9,.width=1920,.height=1080,.depth=1,.levels=1};
    nvn_texlayout layout; nvn_tex_layout(&d, &layout);
    // 120 GOB columns * 9 block rows * (16 GOBs * 512 bytes).
    assert(layout.size == 8847360 && layout.tileH == 4);
    assert(nvn_bl_offset(0,0,0,128,128,4,0) == 0);
    assert(nvn_bl_offset(16,0,0,128,128,4,0) == 32);
    assert(nvn_bl_offset(32,0,0,128,128,4,0) == 256);
    assert(nvn_bl_offset(0,1,0,128,128,4,0) == 16);
    assert(nvn_bl_offset(0,2,0,128,128,4,0) == 64);
    assert(nvn_bl_offset(0,8,0,128,128,4,0) == 512);
    assert(nvn_bl_offset(64,0,0,128,128,4,0) == 8192);
    d.width = 19; d.height = 17; d.levels = 5;
    nvn_tex_layout(&d, &layout);
    uint8_t *storage = calloc(1, layout.size); assert(storage);
    for (unsigned level = 0; level < d.levels; level++) {
        unsigned w = d.width >> level, h = d.height >> level; if (!w) w=1; if (!h) h=1;
        size_t bytes = w*h*4; uint8_t *source=malloc(bytes), *dest=malloc(bytes); assert(source && dest);
        for (size_t i=0;i<bytes;i++) source[i]=(uint8_t)(i*17+level);
        assert(!nvn_swizzle(&d, level, 0, source, w*4, storage));
        assert(!nvn_deswizzle(&d, level, 0, storage, dest, w*4));
        assert(!memcmp(source,dest,bytes)); free(source);free(dest);
    }
    free(storage);
    // Sizes from this game's original SDK CPU layout routines, including sparse-tail boundaries.
    const unsigned widths[]={1,19,128,127,256,64,128,129,1920,1920};
    const unsigned heights[]={1,16,127,128,64,256,128,129,1080,64};
    const uint64_t expected[]={512,2048,65536,65536,65536,65536,65536,262144,8847360,491520};
    d.flags=0x28; d.format=0x3d; d.levels=1;
    for (unsigned k=0;k<10;k++) {
        d.width=widths[k]; d.height=heights[k]; nvn_tex_layout(&d,&layout);
        assert(layout.size==expected[k]);
    }
    d.width=d.height=129; nvn_tex_layout(&d,&layout);
    storage=calloc(1,layout.size); uint8_t *source=malloc(129*129*4), *dest=malloc(129*129*4);
    assert(storage && source && dest);
    for (size_t i=0;i<129*129*4;i++) source[i]=(uint8_t)(i*11+3);
    assert(!nvn_swizzle(&d,0,0,source,129*4,storage));
    // y=128 starts the second sparse tile row, after two 64 KiB tiles.
    assert(!memcmp(storage+131072,source+128*129*4,16));
    assert(!nvn_deswizzle(&d,0,0,storage,dest,129*4));
    assert(!memcmp(source,dest,129*129*4)); free(source); free(dest); free(storage);
    puts("PASS: independent GOB offsets, 1080p storage extent and odd-size mip round trips");
}
