#include "../src/gfx/platform.m"
#include <assert.h>
#include <stdarg.h>
HyModule hy_modules[4];size_t hy_module_count;
const char *const hy_nvn_names[]={"nvnUnknown"};void *const hy_nvn_gates[]={NULL};const size_t hy_nvn_name_count=1;
void *hy_symbol(const char *name) {(void)name;abort();}
_Noreturn void hy_fail(const char *format,...) {va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);exit(42);}
int main(void) {
 @autoreleasepool {
  initialize(NULL,0);uint64_t d=1;void *memory=NULL;assert(!posix_memalign(&memory,0x4000,0x10000));memset(memory,0,0x10000);
  PoolBuilder pb={.device=&d,.storage=memory,.size=0x10000,.flags=0x24};Pool *pool=NULL;assert(pool_initialize(&pool,&pb));
  TextureBuilder tb={.device=&d};texture_defaults(&tb);texture_size2d(&tb,19,17);texture_format(&tb,0x25);texture_flags(&tb,0x28);texture_storage(&tb,&pool,0);
  Texture image;assert(texture_initialize(&image,&tb));Texture *colors[]={&image};
  QueueBuilder qb={.device=&d};void *q=NULL;assert(queue_initialize(&q,&qb));GpuSync *sync=NULL;assert(sync_initialize(&sync,&d));
  HyCommands c;command_initialize(&c,&d);command_memory(&c,&pool,0x4000,0x4000);hy_commands_begin(&c);
  command_counter(&c,0,(uintptr_t)memory+0x9000);
  command_targets(&c,1,colors,NULL,NULL,NULL);float red[]={1,0,0,1};hy_commands_clear_color(&c,0,red,15);
  command_counter(&c,0,(uintptr_t)memory+0x9010);command_fence(&c,&sync,0,1);uint64_t handle=hy_commands_end(&c);
  queue_submit(&q,1,&handle);assert(queue_command_used(&q)==c.used && queue_control_used(&q)==8 && queue_compute_used(&q)==0);
  queue_reset_used(&q);assert(!queue_command_used(&q) && !queue_control_used(&q));assert(sync_wait(&sync,UINT64_MAX)<=1);
  HyCounterData *counter=(HyCounterData *)((uint8_t *)memory+0x9000);assert(counter[0].timestamp && counter[1].timestamp>counter[0].timestamp);
  uint8_t pixels[19*17*4];[(__bridge id<MTLTexture>)image.image getBytes:pixels bytesPerRow:19*4 fromRegion:MTLRegionMake2D(0,0,19,17) mipmapLevel:0];
  for(unsigned i=0;i<19*17;i++)assert(!memcmp(pixels+4*i,(uint8_t[]){255,0,0,255},4));
  sync_finalize(&sync);queue_finalize(&q);texture_finalize(&image);pool_finalize(&pool);free(memory);
  puts("PASS: recorded queue clear rendered all 323 pixels, real GPU timestamps and fence completed, resources released");
 }
}
