#include "../src/gfx/platform.m"
#include <assert.h>
#include <stdarg.h>
HyModule hy_modules[4];size_t hy_module_count;
const char *const hy_nvn_names[]={"nvnUnknown"};void *const hy_nvn_gates[]={NULL};const size_t hy_nvn_name_count=1;
void *hy_symbol(const char *name) {(void)name;abort();}
_Noreturn void hy_fail(const char *format,...) {va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);exit(42);}
int main(int argc,char **argv) {
    @autoreleasepool {
        initialize(NULL,0);uint64_t d=1;
        int alignment=0;integer(&d,0x19,&alignment);assert(alignment==256);
        void *memory=NULL;assert(!posix_memalign(&memory,0x4000,0x4000));memset(memory,0,0x4000);
        PoolBuilder pb={.device=&d,.storage=memory,.size=0x4000,.flags=0x24};Pool *pool=NULL;
        assert(pool_initialize(&pool,&pb));
        uint32_t *control=memory;
        uint32_t header[]={0x98761234,1,0x10,0x120,0xb,0x878,0,0,0x880};memcpy(control,header,sizeof header);
        control[0x6f8/4]=0xd0;control[0x6fc/4]=16;control[0x700/4]=0x100;control[0x704/4]=0x200;control[0x708/4]=0x30;
        uint8_t *code=(uint8_t *)memory+0x1000;memset(code,0x69,0x200);
        uint32_t sph=0x610461;memcpy(code+0x30,&sph,4);
        HyShaderData input={(uintptr_t)code,control};void *program=NULL;
        assert(program_initialize(&program,&d));
        if(argc>1 && !strcmp(argv[1],"--reject-code-range")) input.address=(uintptr_t)memory+0x3f00;
        if(argc>1 && !strcmp(argv[1],"--reject-stage")) control[0x714/4]=1;
        assert(program_shaders(&program,1,&input));
        NSArray *snapshot=(__bridge NSArray *)program;HyShader *captured=snapshot[0];
        assert(captured->stage==1 && captured->code.length==0xd0 && captured->constants.length==16 && strlen(captured->key)==64);
        assert(!memcmp(captured->code.bytes,code+0x30,0xd0));memset(code,0,0x200);
        assert(((const uint8_t *)captured->code.bytes)[4]==0x69);
        HyCommands commands;assert(command_initialize(&commands,&d));
        command_memory(&commands,&pool,0x2000,0x1000);hy_commands_begin(&commands);
        void (*bind_program)(HyCommands *,void *const *,uint32_t)=lookup("nvnCommandBufferBindProgram");
        bind_program(&commands,&program,3);hy_commands_end(&commands);
        __weak NSArray *recorded=snapshot;program_finalize(&program);assert(!program);snapshot=nil;
        assert(recorded && ((HyShader *)recorded[0])->code.length==0xd0);
        command_initialize(&commands,&d);command_memory(&commands,&pool,0x2000,0x1000);
        hy_commands_begin(&commands);hy_commands_end(&commands);assert(!recorded);
        pool_finalize(&pool);free(memory);
        assert(((const uint8_t *)captured->constants.bytes)[0]==0x69);
        puts("PASS: shader snapshots survive source mutation, program finalization and pool release");
    }
}
