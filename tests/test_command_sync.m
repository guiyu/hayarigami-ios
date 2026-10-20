#include "../src/gfx/platform.m"
#include <assert.h>
#include <stdarg.h>
HyModule hy_modules[4];size_t hy_module_count;
const char *const hy_nvn_names[]={"nvnUnknown"};void *const hy_nvn_gates[]={NULL};const size_t hy_nvn_name_count=1;
void *hy_symbol(const char *name) {(void)name;abort();}
_Noreturn void hy_fail(const char *format,...) {va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);exit(42);}
int main(void) {
    @autoreleasepool {
        initialize(NULL,0);uint64_t d=1;GpuSync *sync=NULL;assert(sync_initialize(&sync,&d));
        _Alignas(8) uint8_t memory[64];HyCommands c;assert(command_initialize(&c,&d));
        hy_commands_memory(&c,memory,sizeof memory);hy_commands_begin(&c);
        command_fence(&c,&sync,1,1);hy_commands_end(&c);
        assert(sync_wait(&sync,0)==3); // Recording does not submit or arm a fence.
        HyRecordedSync *record=(__bridge HyRecordedSync *)*(void **)(memory+16);
        assert(record->condition==1 && record->flags==1 && sync->references==2);
        sync_finalize(&sync);assert(!sync && record->sync->references==1);
        __weak HyRecordedSync *retained=record;record=nil;
        command_initialize(&c,&d);hy_commands_memory(&c,memory,sizeof memory);hy_commands_begin(&c);hy_commands_end(&c);
        assert(!retained);
        puts("PASS: recorded sync retains its host state without arming a fence and releases it when command bytes are replaced");
    }
}
