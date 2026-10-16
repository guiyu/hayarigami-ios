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
    puts("PASS: command payload snapshot, segment callback/jump, persistent handles and bounds");
}
