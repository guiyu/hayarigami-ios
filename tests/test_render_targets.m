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
        void *memory=NULL;assert(!posix_memalign(&memory,0x4000,0x8000));memset(memory,0,0x8000);
        PoolBuilder pb={.device=&d,.storage=memory,.size=0x8000,.flags=0x24};Pool *pool=NULL;
        assert(pool_initialize(&pool,&pb));
        TextureBuilder tb={.device=&d};texture_defaults(&tb);texture_size2d(&tb,19,17);texture_format(&tb,0x25);texture_storage(&tb,&pool,0);
        Texture texture;assert(texture_initialize(&texture,&tb));void *original=texture.image;
        uint32_t (*init_commands)(HyCommands *,void *)=lookup("nvnCommandBufferInitialize");
        void (*targets)(HyCommands *,int,Texture *const *,const TextureView *const *,const Texture *,const TextureView *)=lookup("nvnCommandBufferSetRenderTargets");
        HyCommands c;assert(init_commands(&c,&d));command_memory(&c,&pool,0x4000,0x1000);hy_commands_begin(&c);
        Texture *colors[]={&texture};targets(&c,1,colors,NULL,NULL,NULL);colors[0]=NULL;hy_commands_end(&c);
        assert(pool->textures->references==2);texture_finalize(&texture);assert(pool->textures->references==1);
        Texture alias;assert(texture_initialize(&alias,&tb));assert(alias.image==original);texture_finalize(&alias);
        // Overwriting just the old target record's header must release its complete resource lease.
        assert(init_commands(&c,&d));command_memory(&c,&pool,0x4000,0x1000);hy_commands_begin(&c);hy_commands_end(&c);
        assert(!pool->textures);
        assert(texture_initialize(&texture,&tb));
        void *command_storage=NULL;assert(!posix_memalign(&command_storage,0x4000,0x4000));
        PoolBuilder cpb={.device=&d,.storage=command_storage,.size=0x4000,.flags=0x24};Pool *command_pool=NULL;
        assert(pool_initialize(&command_pool,&cpb));
        assert(init_commands(&c,&d));command_memory(&c,&command_pool,0,0x1000);hy_commands_begin(&c);
        colors[0]=&texture;targets(&c,1,colors,NULL,NULL,NULL);
        void (*uniform)(HyCommands *,int,int,uint64_t,size_t)=lookup("nvnCommandBufferBindUniformBuffer");
        uint64_t address=pool_address(&pool)+0x100;
        if(argc>1 && !strcmp(argv[1],"--reject-uniform-range")) address=pool_address(&pool)+pool->size-16;
        size_t uniform_offset=c.used;uniform(&c,0,3,address,64);
        HyBoundBuffer *bound=(__bridge HyBoundBuffer *)*(void **)((uint8_t *)command_storage+uniform_offset+8);
        assert(bound->stage==0 && bound->index==3 && bound->length==64 && pool->buffers==1);
        ((uint8_t *)memory)[0x100]=0x91;
        assert(((uint8_t *)bound->buffer.contents)[bound->offset]==0x91);bound=nil;
        void (*vertices)(HyCommands *,int,int,const HyGpuBufferRange *)=lookup("nvnCommandBufferBindVertexBuffers");
        HyGpuBufferRange ranges[]={{pool_address(&pool)+0x200,96},{0,0}};
        size_t vertex_offset=c.used;vertices(&c,5,2,ranges);ranges[0].address=0;
        HyBoundBuffer *vertex=(__bridge HyBoundBuffer *)*(void **)((uint8_t *)command_storage+vertex_offset+8);
        assert(vertex->stage==-1 && vertex->index==5 && vertex->offset==pool->offset+0x200 && vertex->length==96 && pool->buffers==2);vertex=nil;
        void (*draw)(HyCommands *,int,int,int,uint64_t)=lookup("nvnCommandBufferDrawElements");
        size_t draw_offset=c.used;draw(&c,4,1,3,pool_address(&pool)+0x300);hy_commands_end(&c);
        HyDrawIndexed *indexed=(__bridge HyDrawIndexed *)*(void **)((uint8_t *)command_storage+draw_offset+8);
        assert(indexed->primitive==4 && indexed->index_type==1 && indexed->count==3 && indexed->indices->length==6 && pool->buffers==3);indexed=nil;
        texture_finalize(&texture);
        // Releasing referenced storage invalidates its old command leases, even when command bytes live in another pool.
        pool_finalize(&pool);assert(!pool);free(memory);
        pool_finalize(&command_pool);free(command_storage);
        puts("PASS: recorded targets retain backing across object finalization; partial overwrite and command-pool release retire references");
    }
}
