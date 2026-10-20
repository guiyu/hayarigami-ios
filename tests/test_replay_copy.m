#include "../src/gfx/platform.m"
#include <assert.h>
#include <stdarg.h>
HyModule hy_modules[4];size_t hy_module_count;
const char *const hy_nvn_names[]={"nvnUnknown"};void *const hy_nvn_gates[]={NULL};const size_t hy_nvn_name_count=1;
void *hy_symbol(const char *name) {(void)name;abort();}
_Noreturn void hy_fail(const char *format,...) {va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);exit(42);}
int main(void) {
    @autoreleasepool {
        initialize(NULL,0);uint64_t d=1;void *memory=NULL;
        assert(!posix_memalign(&memory,0x4000,0x8000));memset(memory,0,0x8000);
        PoolBuilder pb={.device=&d,.storage=memory,.size=0x8000,.flags=0x24};Pool *pool=NULL;assert(pool_initialize(&pool,&pb));
        TextureBuilder tb={.device=&d};texture_defaults(&tb);texture_size2d(&tb,19,17);texture_format(&tb,0x3d);texture_storage(&tb,&pool,0);
        Texture source,destination;assert(texture_initialize(&source,&tb));texture_size2d(&tb,24,24);texture_format(&tb,0x25);texture_storage(&tb,&pool,0x2000);assert(texture_initialize(&destination,&tb));
        uint32_t packed[19*17];
        for(unsigned y=0;y<17;y++)for(unsigned x=0;x<19;x++)packed[y*19+x]=((x&1)?1023u:0u)|((y&1)?(1023u<<10):0u)|(512u<<20)|(((x+y)&3u)<<30);
        [(__bridge id<MTLTexture>)source.image replaceRegion:MTLRegionMake2D(0,0,19,17) mipmapLevel:0 withBytes:packed bytesPerRow:19*4];
        HyCommands c;assert(command_initialize(&c,&d));command_memory(&c,&pool,0x4000,0x1000);hy_commands_begin(&c);
        HyCopyRegion from={1,2,0,5,6,1},to={3,4,0,5,6,1};
        command_copy_texture(&c,&source,NULL,&from,&destination,NULL,&to,0);uint64_t handle=hy_commands_end(&c);
        QueueBuilder qb={.device=&d};void *q=NULL;assert(queue_initialize(&q,&qb));queue_submit(&q,1,&handle);queue_finalize(&q);
        uint8_t pixels[24*24*4];[(__bridge id<MTLTexture>)destination.image getBytes:pixels bytesPerRow:24*4 fromRegion:MTLRegionMake2D(0,0,24,24) mipmapLevel:0];
        for(unsigned y=0;y<24;y++)for(unsigned x=0;x<24;x++) {
            uint8_t expected[4]={0,0,0,0};
            if(x>=3 && x<8 && y>=4 && y<10) {unsigned sx=x-3+1,sy=y-4+2;expected[0]=(sx&1)?255:0;expected[1]=(sy&1)?255:0;expected[2]=128;expected[3]=(uint8_t)(((sx+sy)&3)*85);}
            assert(!memcmp(pixels+(y*24+x)*4,expected,4));
        }
        texture_finalize(&source);texture_finalize(&destination);pool_finalize(&pool);free(memory);
        puts("PASS: recorded RGB10A2 to RGBA8 GPU color conversion, source/destination offsets and untouched pixels");
    }
}
