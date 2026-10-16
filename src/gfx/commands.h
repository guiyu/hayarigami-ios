#ifndef HY_COMMANDS_H
#define HY_COMMANDS_H
#include "../runtime/runtime.h"
#include <string.h>
typedef struct { uint32_t op, size; } HyRecord;
enum { HY_BEGIN = 1, HY_END, HY_JUMP };
typedef struct HyCommands HyCommands;
typedef void (*HyMemoryCallback)(HyCommands *, int, size_t, void *);
struct HyCommands {
    void *device, *context;
    HyMemoryCallback callback;
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
    HyRecord header = {op, (uint32_t)size}; memcpy(p, &header, 8);
    memset(p + 8, 0, size - 8); c->used += size;
    return p + 8;
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
