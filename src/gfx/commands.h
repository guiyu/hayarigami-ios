#ifndef HY_COMMANDS_H
#define HY_COMMANDS_H
#include "../runtime/runtime.h"
#include <string.h>
typedef struct { uint32_t op, size; } HyRecord;
enum { HY_BEGIN = 1, HY_END, HY_JUMP, HY_TEXTURE_POOL, HY_SAMPLER_POOL,
    HY_MULTISAMPLE_STATE, HY_COLOR_STATE, HY_BLEND_STATE, HY_CHANNEL_MASK_STATE,
    HY_POLYGON_STATE, HY_DEPTH_STENCIL_STATE, HY_REPORT_COUNTER, HY_DEPTH_RANGE, HY_DEPTH_CLAMP, HY_VIEWPORT, HY_SCISSOR, HY_RENDER_TARGETS, HY_CLEAR_COLOR, HY_CLEAR_DEPTH, HY_BIND_PROGRAM, HY_UNIFORM_BUFFER, HY_STENCIL_MASK, HY_STENCIL_VALUE_MASK, HY_ALPHA_REF, HY_STENCIL_REF, HY_VERTEX_ATTRIBUTES, HY_VERTEX_STREAMS, HY_VERTEX_BUFFER, HY_DRAW_ELEMENTS, HY_COPY_TEXTURE, HY_TILED_CACHE, HY_BARRIER, HY_FENCE_SYNC };
typedef struct { uint64_t address; int type; } HyCounterRecord;
typedef struct { uint64_t counter,timestamp; } HyCounterData;
typedef struct { int32_t x,y,z,width,height,depth; } HyCopyRegion;
_Static_assert(sizeof(HyCopyRegion)==24,"NVN copy region ABI");
typedef struct { int32_t index; uint32_t mask; float color[4]; } HyClearColorRecord;
typedef struct { void *shaders; uint32_t stages; } HyProgramRecord;
typedef struct { float depth; uint32_t depth_mask; int32_t stencil; uint32_t stencil_mask; } HyClearDepthRecord;
typedef struct HyCommands HyCommands;
typedef void (*HyMemoryCallback)(HyCommands *, int, size_t, void *);
struct HyCommands {
    void *device, *context;
    HyMemoryCallback callback;
    void (*overwrite)(void *,size_t);
    uint8_t *memory, *begin;
    size_t size, used;
    void *control;
    size_t control_size;
    int recording;
};
_Static_assert(sizeof(HyCommands) <= 160, "NVN command buffer ABI size");
static inline uint32_t hy_commands_init(HyCommands *c, void *device) {
    memset(c, 0, sizeof *c); c->device = device; return device != NULL;
}
static inline void hy_commands_callback(HyCommands *c, HyMemoryCallback callback) { c->callback = callback; }
static inline void hy_commands_context(HyCommands *c, void *context) { c->context = context; }
static inline void hy_commands_memory(HyCommands *c, void *memory, size_t size) {
    if (!memory || ((uintptr_t)memory & 7) || (size & 7) || size < 32)
        hy_fail("invalid command memory size/alignment");
    if (c->recording && c->memory) {
        if (c->size - c->used < 16) hy_fail("command jump reservation exhausted");
        if (c->overwrite) c->overwrite(c->memory+c->used,16);
        HyRecord jump = {HY_JUMP, 16};
        memcpy(c->memory + c->used, &jump, sizeof jump);
        memcpy(c->memory + c->used + 8, &memory, sizeof memory);
    }
    c->memory = memory; c->size = size; c->used = 0;
}
static inline void hy_commands_control(HyCommands *c, void *memory, size_t size) {
    if (!memory || ((uintptr_t)memory & 7) || size < 16) hy_fail("invalid control memory");
    c->control = memory; c->control_size = size;
}
static inline void *hy_commands_record(HyCommands *c, uint32_t op, size_t bytes) {
    if (!c->recording || bytes > UINT32_MAX - 31) hy_fail("invalid command recording");
    size_t size = (bytes + 15) & ~(size_t)7;
    for (unsigned attempt = 0; !c->memory || c->size - c->used < size + 16; attempt++) {
        if (!c->callback || attempt == 2) hy_fail("NVN command memory exhausted");
        c->callback(c, 0, size + 16, c->context);
    }
    uint8_t *p = c->memory + c->used;
    if (c->overwrite) c->overwrite(p,size);
    HyRecord header = {op, (uint32_t)size}; memcpy(p, &header, 8);
    memset(p + 8, 0, size - 8); c->used += size;
    return p + 8;
}
static inline void hy_commands_depth_range(HyCommands *c, float near, float far) {
    float range[]={near,far}; memcpy(hy_commands_record(c,HY_DEPTH_RANGE,sizeof range),range,sizeof range);
}
static inline void hy_commands_depth_clamp(HyCommands *c, uint32_t enabled) {
    enabled=!!(enabled&255); memcpy(hy_commands_record(c,HY_DEPTH_CLAMP,sizeof enabled),&enabled,sizeof enabled);
}
static inline void hy_commands_viewport(HyCommands *c, int32_t x, int32_t y, int32_t width, int32_t height) {
    int32_t rect[]={x,y,width,height}; memcpy(hy_commands_record(c,HY_VIEWPORT,sizeof rect),rect,sizeof rect);
}
static inline void hy_commands_scissor(HyCommands *c, int32_t x, int32_t y, int32_t width, int32_t height) {
    int32_t rect[]={x,y,width,height}; memcpy(hy_commands_record(c,HY_SCISSOR,sizeof rect),rect,sizeof rect);
}
static inline void hy_commands_clear_color(HyCommands *c,int index,const float *color,uint32_t mask) {
    if (index<0 || index>=8 || (mask&~15u) || !color) hy_fail("invalid color clear");
    HyClearColorRecord *record=hy_commands_record(c,HY_CLEAR_COLOR,sizeof *record);
    record->index=index;record->mask=mask;memcpy(record->color,color,sizeof record->color);
}
static inline void hy_commands_clear_depth_stencil(HyCommands *c,float depth,uint32_t depth_mask,int32_t stencil,uint32_t stencil_mask) {
    HyClearDepthRecord *record=hy_commands_record(c,HY_CLEAR_DEPTH,sizeof *record);
    *record=(HyClearDepthRecord){depth,!!(depth_mask&255),stencil,stencil_mask};
}
static inline void hy_commands_stencil_mask(HyCommands *c,uint32_t faces,uint32_t mask) {
    if (faces&~3u) hy_fail("invalid stencil face mask");
    uint32_t state[]={faces,mask&255};memcpy(hy_commands_record(c,HY_STENCIL_MASK,sizeof state),state,sizeof state);
}
static inline void hy_commands_stencil_value_mask(HyCommands *c,uint32_t faces,uint32_t mask) {
    if (faces&~3u) hy_fail("invalid stencil face mask");
    uint32_t state[]={faces,mask&255};memcpy(hy_commands_record(c,HY_STENCIL_VALUE_MASK,sizeof state),state,sizeof state);
}
static inline void hy_commands_alpha_ref(HyCommands *c,float reference) {
    memcpy(hy_commands_record(c,HY_ALPHA_REF,sizeof reference),&reference,sizeof reference);
}
static inline void hy_commands_stencil_ref(HyCommands *c,uint32_t faces,uint32_t reference) {
    if (faces&~3u) hy_fail("invalid stencil face mask");
    uint32_t state[]={faces,reference&255};memcpy(hy_commands_record(c,HY_STENCIL_REF,sizeof state),state,sizeof state);
}
static inline void hy_commands_attributes(HyCommands *c,int count,const uint32_t *attributes) {
    if (count<0 || count>16 || (count && !attributes)) hy_fail("unsupported vertex attribute count");
    uint32_t *record=hy_commands_record(c,HY_VERTEX_ATTRIBUTES,8+(size_t)count*4);
    record[0]=(uint32_t)count;if (count) memcpy(record+2,attributes,(size_t)count*4);
}
static inline void hy_commands_streams(HyCommands *c,int count,const void *streams) {
    if (count<0 || count>16 || (count && !streams)) hy_fail("unsupported vertex stream count");
    uint32_t *record=hy_commands_record(c,HY_VERTEX_STREAMS,8+(size_t)count*8);
    record[0]=(uint32_t)count;if (count) memcpy(record+2,streams,(size_t)count*8);
}
static inline void hy_commands_tiled_cache(HyCommands *c,uint32_t action) {
    if (action<1 || action>6) hy_fail("unsupported tiled cache action");
    memcpy(hy_commands_record(c,HY_TILED_CACHE,sizeof action),&action,sizeof action);
}
static inline void hy_commands_barrier(HyCommands *c,uint32_t flags) {
    if (flags&~255u) hy_fail("unsupported barrier flags");
    memcpy(hy_commands_record(c,HY_BARRIER,sizeof flags),&flags,sizeof flags);
}
static inline void hy_commands_begin(HyCommands *c) {
    if (c->recording) hy_fail("nested NVN command recording");
    c->recording = 1;
    c->begin = (uint8_t *)hy_commands_record(c, HY_BEGIN, 0) - 8;
}
static inline uint64_t hy_commands_end(HyCommands *c) {
    hy_commands_record(c, HY_END, 0); c->recording = 0;
    return (uintptr_t)c->begin;
}
#endif
