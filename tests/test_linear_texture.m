#include "../src/gfx/platform.m"
#include <assert.h>
#include <stdarg.h>
HyModule hy_modules[4];size_t hy_module_count;
const char *const hy_nvn_names[]={"nvnUnknown"};void *const hy_nvn_gates[]={NULL};const size_t hy_nvn_name_count=1;
void *hy_symbol(const char *name){(void)name;abort();}
_Noreturn void hy_fail(const char *format,...){va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);exit(42);}
int main(void){@autoreleasepool{
 device=MTLCreateSystemDefaultDevice();assert(device);queue=[device newCommandQueue];
 void *memory;assert(!posix_memalign(&memory,16384,131072));memset(memory,0,131072);
 PoolBuilder pb={.device=(void *)1,.storage=memory,.size=131072,.flags=0x22};Pool *pool;assert(pool_initialize(&pool,&pb));
 TextureBuilder b={.device=(void *)1};texture_defaults(&b);texture_size2d(&b,960,4);texture_format(&b,0xd);texture_flags(&b,0x10);texture_stride(&b,1984);texture_storage(&b,&pool,0);
 assert(texture_size(&b)==1984*4);Texture texture;assert(texture_initialize(&texture,&b));id<MTLTexture> image=(__bridge id<MTLTexture>)texture.image;
 MTLTextureDescriptor *desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm width:960 height:4 mipmapped:NO];id<MTLTexture> copy=[device newTextureWithDescriptor:desc];
 uint8_t bytes[1920*4];
 for(unsigned pass=0;pass<2;pass++){
 for(unsigned row=0;row<4;row++)for(unsigned col=0;col<1920;col++)((uint8_t *)memory)[row*1984+col]=(uint8_t)(col+row+pass*71);
 pool_flush(&pool,0,1984*4);id<MTLCommandBuffer> command=[queue commandBuffer];id<MTLBlitCommandEncoder> encoder=[command blitCommandEncoder];[encoder copyFromTexture:image sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(960,4,1) toTexture:copy destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];[encoder endEncoding];[command commit];[command waitUntilCompleted];assert(command.status==MTLCommandBufferStatusCompleted);[copy getBytes:bytes bytesPerRow:1920 fromRegion:MTLRegionMake2D(0,0,960,4) mipmapLevel:0];
 for(unsigned row=0;row<4;row++)for(unsigned col=0;col<1920;col++)assert(bytes[row*1920+col]==(uint8_t)(col+row+pass*71));
 }
 texture_swizzle(&texture.builder,2,2,2,1);id<MTLTexture> swizzled=texture_view(&texture,NULL);assert(swizzled.swizzle.red==MTLTextureSwizzleRed && swizzled.swizzle.green==MTLTextureSwizzleRed && swizzled.swizzle.blue==MTLTextureSwizzleRed && swizzled.swizzle.alpha==MTLTextureSwizzleOne);
 // The title keeps its linear movie Texture alive while reusing CPU storage
 // for a new packaged BC7 image. Both Metal images are sampled only.
 TextureBuilder packaged={.device=(void *)1};texture_defaults(&packaged);texture_size2d(&packaged,19,17);texture_format(&packaged,0x4d);texture_flags(&packaged,0x20);texture_storage(&packaged,&pool,0);
 for(unsigned i=0;i<131072;i+=16){memset((uint8_t *)memory+i,0,16);((uint8_t *)memory)[i]=0x40;}
 Texture replacement;assert(texture_initialize(&replacement,&packaged));assert(replacement.image!=texture.image);
 uint8_t after[1920*4];[image getBytes:after bytesPerRow:1920 fromRegion:MTLRegionMake2D(0,0,960,4) mipmapLevel:0];assert(!memcmp(bytes,after,sizeof bytes));
 uint8_t bc7_pixels[19*17*4];[(__bridge id<MTLTexture>)replacement.image getBytes:bc7_pixels bytesPerRow:19*4 fromRegion:MTLRegionMake2D(0,0,19,17) mipmapLevel:0];for(unsigned i=0;i<sizeof bc7_pixels;i++)assert(bc7_pixels[i]==0);
 texture_finalize(&replacement);
 HyTargetImage *old=image_snapshot(&texture,NULL);texture_finalize(&texture);
 texture_defaults(&b);texture_size2d(&b,1920,4);texture_format(&b,1);texture_flags(&b,0x10);texture_stride(&b,1984);texture_storage(&b,&pool,0);assert(texture_initialize(&texture,&b));assert(texture.image!=(__bridge void *)old->image);old=nil;
 texture_finalize(&texture);pool_finalize(&pool);free(memory);puts("PASS: padded RG8 CPU uploads, live linear-to-BC7 storage reuse and finalized R8 reallocation");
}}
