#include "../src/gfx/commands.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
_Noreturn void hy_fail(const char *format, ...) { (void)format; abort(); }
static _Alignas(8) uint8_t first[64], second[128];
static unsigned callbacks;
static void replenish(HyCommands *c, int event, size_t requested, void *context) {
    assert(event == 0 && requested == 56 && context == first);
    callbacks++; hy_commands_memory(c, second, sizeof second);
}
int main(void) {
    HyCommands c; assert(hy_commands_init(&c, (void *)1));
    hy_commands_memory(&c, first, sizeof first);
    hy_commands_callback(&c, replenish); hy_commands_context(&c, first);
    hy_commands_begin(&c);
    uint64_t source = 123;
    memcpy(hy_commands_record(&c, 100, sizeof source), &source, sizeof source); source = 999;
    memset(hy_commands_record(&c, 101, 32), 0x5a, 32);
    uint64_t handle = hy_commands_end(&c);
    assert(callbacks == 1 && handle == (uintptr_t)first && !c.recording);
    HyRecord *r = (HyRecord *)first;
    assert(r->op == HY_BEGIN && r->size == 8);
    r = (HyRecord *)(first + 8); assert(r->op == 100 && r->size == 16);
    assert(*(uint64_t *)(first + 16) == 123);
    r = (HyRecord *)(first + 24); assert(r->op == HY_JUMP && r->size == 16);
    assert(*(uintptr_t *)(first + 32) == (uintptr_t)second);
    r = (HyRecord *)second; assert(r->op == 101 && r->size == 40);
    assert(second[39] == 0x5a && ((HyRecord *)(second + 40))->op == HY_END);
    assert(c.used == 48 && c.size == 128);
    hy_commands_begin(&c); assert(hy_commands_end(&c) == (uintptr_t)(second + 48));
    _Alignas(8) uint8_t depth_bytes[256]; HyCommands depth;
    hy_commands_init(&depth,(void *)1);hy_commands_memory(&depth,depth_bytes,sizeof depth_bytes);
    hy_commands_begin(&depth);hy_commands_depth_range(&depth,0.75f,0.25f);hy_commands_depth_clamp(&depth,1);hy_commands_viewport(&depth,3,4,1920,1080);hy_commands_scissor(&depth,11,12,100,200);
    size_t clear_offset=depth.used;float color[]={0.25f,0.5f,0.75f,1};
    hy_commands_clear_color(&depth,2,color,5);memset(color,0,sizeof color);size_t depth_clear_offset=depth.used;
    hy_commands_clear_depth_stencil(&depth,0.375f,1,83,0x5a);
    size_t stencil_offset=depth.used;hy_commands_stencil_mask(&depth,3,0x1a5);
    size_t value_offset=depth.used;hy_commands_stencil_value_mask(&depth,2,0x15a);
    size_t alpha_offset=depth.used;hy_commands_alpha_ref(&depth,0.625f);
    size_t ref_offset=depth.used;hy_commands_stencil_ref(&depth,1,0x153);hy_commands_end(&depth);
    assert(*(uint32_t *)(depth_bytes+ref_offset+8)==1 && *(uint32_t *)(depth_bytes+ref_offset+12)==83);
    float alpha;memcpy(&alpha,depth_bytes+alpha_offset+8,4);assert(alpha==0.625f);
    assert(((HyRecord *)(depth_bytes+value_offset))->op==HY_STENCIL_VALUE_MASK && *(uint32_t *)(depth_bytes+value_offset+12)==0x5a);
    assert(*(uint32_t *)(depth_bytes+stencil_offset+8)==3 && *(uint32_t *)(depth_bytes+stencil_offset+12)==0xa5);
    HyClearDepthRecord *depth_clear=(HyClearDepthRecord *)(depth_bytes+depth_clear_offset+8);
    assert(depth_clear->depth==0.375f && depth_clear->depth_mask==1 && depth_clear->stencil==83 && depth_clear->stencil_mask==0x5a);
    HyClearColorRecord *clear=(HyClearColorRecord *)(depth_bytes+clear_offset+8);
    assert(clear->index==2 && clear->mask==5 && clear->color[0]==0.25f && clear->color[3]==1);
    assert(((HyRecord *)(depth_bytes+8))->op==HY_DEPTH_RANGE);
    assert(((HyRecord *)(depth_bytes+24))->op==HY_DEPTH_CLAMP && *(uint32_t *)(depth_bytes+32)==1);
    int32_t rect[4];memcpy(rect,depth_bytes+48,sizeof rect);assert(!memcmp(rect,(int32_t[]){3,4,1920,1080},sizeof rect));
    memcpy(rect,depth_bytes+72,sizeof rect);assert(!memcmp(rect,(int32_t[]){11,12,100,200},sizeof rect));
    float range[2];memcpy(range,depth_bytes+16,sizeof range);assert(range[0]==0.75f && range[1]==0.25f);
    _Alignas(8) uint8_t vertex_bytes[256];HyCommands vertex;
    hy_commands_init(&vertex,(void *)1);hy_commands_memory(&vertex,vertex_bytes,sizeof vertex_bytes);
    hy_commands_begin(&vertex);uint32_t attributes[]={0x1234,0x9876};
    hy_commands_attributes(&vertex,2,attributes);memset(attributes,0,sizeof attributes);
    size_t streams_offset=vertex.used;uint32_t streams[]={32,0,16,2};
    hy_commands_streams(&vertex,2,streams);memset(streams,0,sizeof streams);hy_commands_end(&vertex);
    assert(*(uint32_t *)(vertex_bytes+streams_offset+16)==32 && *(uint32_t *)(vertex_bytes+streams_offset+28)==2);
    assert(*(uint32_t *)(vertex_bytes+16)==2 && *(uint32_t *)(vertex_bytes+24)==0x1234 && *(uint32_t *)(vertex_bytes+28)==0x9876);
    puts("PASS: command payload snapshot, segment callback/jump, persistent handles and bounds");
}
