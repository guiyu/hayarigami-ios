#include "../src/gfx/shader.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    uint32_t control[0x880/4]={0};
    uint32_t header[]={0x98761234,1,0x10,0x120,0xb,0x878,0,0,0x880};
    memcpy(control,header,sizeof header);
    control[0x6f8/4]=0xd0;control[0x6fc/4]=16;
    control[0x700/4]=0x100;control[0x704/4]=0x200;control[0x708/4]=0x30;
    HyShaderLayout layout;
    assert(hy_shader_layout(control,sizeof control,&layout));
    assert(layout.code_size==0xd0 && layout.constant_size==16 && layout.constant_offset==0x100 && layout.extent==0x200 && layout.stage==1);
    assert(!hy_shader_layout(control,sizeof control-1,&layout));
    control[0x6fc/4]=257;assert(!hy_shader_layout(control,sizeof control,&layout));
    control[0x6fc/4]=16;control[0x714/4]=1;assert(hy_shader_layout(control,sizeof control,&layout)&&layout.stage==5);
    control[0x714/4]=3;assert(!hy_shader_layout(control,sizeof control,&layout));
    control[0x714/4]=0;control[0x6f8/4]=0xffffffff;assert(!hy_shader_layout(control,sizeof control,&layout));
    puts("PASS: shader layout separates payloads, validates padded extents and rejects unsupported stages/overflow");
}
