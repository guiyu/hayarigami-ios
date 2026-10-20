#include "../src/gfx/platform.m"
#include <assert.h>
#include <stdarg.h>
HyModule hy_modules[4];size_t hy_module_count;
const char *const hy_nvn_names[]={"nvnUnknown"};void *const hy_nvn_gates[]={NULL};const size_t hy_nvn_name_count=1;
void *hy_symbol(const char *name) {(void)name;abort();}
_Noreturn void hy_fail(const char *format,...) {va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);exit(42);}
int main(int argc,char **argv) {
    assert(argc==2);
    @autoreleasepool {
        initialize(NULL,0);uint64_t d=1;void *memory=NULL;
        assert(!posix_memalign(&memory,0x4000,0x4000));memset(memory,0,0x4000);
        PoolBuilder pb={.device=&d,.storage=memory,.size=0x4000,.flags=0x24};Pool *pool=NULL;assert(pool_initialize(&pool,&pb));
        QueueBuilder qb={.device=&d};void *q=NULL;assert(queue_initialize(&q,&qb));
        if(!strcmp(argv[1],"type")) {
            HyCommands c;command_initialize(&c,&d);command_memory(&c,&pool,0,0x4000);hy_commands_begin(&c);
            command_targets(&c,0,NULL,NULL,NULL,NULL);hy_commands_end(&c);
            ((HyRecord *)((uint8_t *)memory+8))->op=HY_COPY_TEXTURE;
            uint64_t handle=(uintptr_t)memory;queue_submit(&q,1,&handle);abort();
        }
        HyRecord *records=memory;
        records[0]=(HyRecord){HY_BEGIN,8};records[1]=(HyRecord){(uint32_t)strtoul(argv[1],NULL,10),8};
        records[2]=(HyRecord){HY_END,8};uint64_t handle=(uintptr_t)memory;
        queue_submit(&q,1,&handle);
        abort();
    }
}
