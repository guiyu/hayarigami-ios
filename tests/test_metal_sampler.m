#import <Metal/Metal.h>
#include "../src/gfx/sampler.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
_Noreturn void hy_fail(const char *format, ...) {
    va_list ap; va_start(ap,format); vfprintf(stderr,format,ap); va_end(ap); exit(42);
}
int main(void) {
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice(); assert(device);
        NSError *error=nil;
        NSString *source=@"#include <metal_stdlib>\nusing namespace metal; kernel void probe(texture2d<float> image [[texture(0)]], sampler s [[sampler(0)]], device float4 *out [[buffer(0)]], constant float2 &uv [[buffer(1)]]) { out[0]=image.sample(s,uv,level(0)); }";
        id<MTLLibrary> library=[device newLibraryWithSource:source options:nil error:&error];
        if (!library) { fprintf(stderr,"%s\n",error.description.UTF8String); return 1; }
        id<MTLComputePipelineState> pipeline=[device newComputePipelineStateWithFunction:[library newFunctionWithName:@"probe"] error:&error];
        assert(pipeline);
        MTLTextureDescriptor *desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:2 height:2 mipmapped:NO];
        desc.storageMode=MTLStorageModeShared; desc.usage=MTLTextureUsageShaderRead;
        id<MTLTexture> texture=[device newTextureWithDescriptor:desc]; assert(texture);
        const uint8_t pixels[]={255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
        [texture replaceRegion:MTLRegionMake2D(0,0,2,2) mipmapLevel:0 withBytes:pixels bytesPerRow:8];
        id<MTLBuffer> result=[device newBufferWithLength:16 options:MTLResourceStorageModeShared];
        id<MTLCommandQueue> queue=[device newCommandQueue]; assert(result && queue);
        const float coordinates[][2]={{0.25f,0.25f},{0.5f,0.5f},{1.25f,0.25f},{1.25f,0.25f},{-1,-1}};
        const float expected[][4]={{1,0,0,1},{0.5f,0.5f,0.5f,1},{1,0,0,1},{0,1,0,1},{0,0,0,0}};
        for (unsigned i=0;i<5;i++) {
            HySamplerConfig config={.min_filter=1,.mag_filter=1,.wrap={7,7,7},.lod_max=16,.anisotropy=1,.compare_func=1};
            if (i==2) config.wrap[0]=1;
            if (i==4) config.wrap[0]=config.wrap[1]=5;
            id<MTLSamplerState> sampler=hy_sampler_create(device,&config); assert(sampler);
            id<MTLCommandBuffer> command=[queue commandBuffer];
            id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
            [encoder setComputePipelineState:pipeline]; [encoder setTexture:texture atIndex:0];
            [encoder setSamplerState:sampler atIndex:0]; [encoder setBuffer:result offset:0 atIndex:0];
            [encoder setBytes:coordinates[i] length:sizeof coordinates[i] atIndex:1];
            [encoder dispatchThreads:MTLSizeMake(1,1,1) threadsPerThreadgroup:MTLSizeMake(1,1,1)];
            [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
            assert(command.status==MTLCommandBufferStatusCompleted);
            for (unsigned c=0;c<4;c++) assert(fabsf(((float *)result.contents)[c]-expected[i][c])<0.00001f);
        }
        puts("PASS: real Metal sampling, bilinear interpolation, repeat, edge clamp and transparent border");
    }
}
