#include "../src/gfx/platform.m"
#include <assert.h>
#include <stdarg.h>
HyModule hy_modules[4];size_t hy_module_count;
const char *const hy_nvn_names[]={"nvnUnknown"};void *const hy_nvn_gates[]={NULL};const size_t hy_nvn_name_count=1;
void *hy_symbol(const char *name) {(void)name;abort();}
_Noreturn void hy_fail(const char *format,...) {va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);exit(42);}
int main(int argc,char **argv) {
    @autoreleasepool {
        initialize(NULL,0);uint64_t d=1;void *memory=NULL;
        assert(!posix_memalign(&memory,0x4000,0x8000));memset(memory,0,0x8000);
        PoolBuilder pb={.device=&d,.storage=memory,.size=0x8000,.flags=0x24};Pool *pool=NULL;assert(pool_initialize(&pool,&pb));
        TextureBuilder tb={.device=&d};texture_defaults(&tb);texture_size2d(&tb,19,17);texture_format(&tb,0x25);texture_storage(&tb,&pool,0);
        if(argc>1 && !strcmp(argv[1],"--reject-linear-destination")){texture_format(&tb,1);texture_flags(&tb,0x10);texture_stride(&tb,32);}
        Texture source,destination;assert(texture_initialize(&source,&tb));texture_size2d(&tb,24,24);texture_storage(&tb,&pool,0x2000);assert(texture_initialize(&destination,&tb));
        HyCommands c;assert(command_initialize(&c,&d));command_memory(&c,&pool,0x4000,0x1000);hy_commands_begin(&c);
        HyCopyRegion from={1,2,0,5,6,1},to={3,4,0,5,6,1};
        if(argc>1 && !strcmp(argv[1],"--reject-copy-range")) from.x=18;
        command_copy_texture(&c,&source,NULL,&from,&destination,NULL,&to,0);hy_commands_end(&c);from.x=99;
        HyTextureCopy *copy=(__bridge HyTextureCopy *)*(void **)((uint8_t *)memory+0x4010);
        assert(copy->from.x==1 && copy->to.x==3 && copy->from.width==5 && copy->flags==0);
        texture_finalize(&source);texture_finalize(&destination);assert(pool->textures && pool->textures->next);
        assert(copy->source->image.width==19 && copy->destination->image.width==24);copy=nil;
        command_initialize(&c,&d);command_memory(&c,&pool,0x4000,0x1000);hy_commands_begin(&c);hy_commands_end(&c);
        assert(!pool->textures);pool_finalize(&pool);free(memory);
        puts("PASS: texture-copy regions and image lifetimes survive source mutation and object finalization");
    }
}
