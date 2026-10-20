#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "../src/gfx/bc7.h"
#include <assert.h>
#include <stdio.h>
int main(int argc,char **argv) {
    (void)argv;
    @autoreleasepool {
        id<MTLDevice> d=MTLCreateSystemDefaultDevice();assert(d);
        if(!d.supportsBCTextureCompression) {puts("SKIP: native BC7 reference unavailable on this GPU");return 0;}
        uint8_t encoded[64*16],expected[32*32*4];uint32_t random=0x6e766e31;
        for(unsigned block=0;block<64;block++) {
            for(unsigned byte=0;byte<16;byte++) {random=random*1664525u+1013904223u;encoded[block*16+byte]=(uint8_t)(random>>24);}
            unsigned mode=block%8;encoded[block*16]=(encoded[block*16]&~((1u<<(mode+1))-1))|(1u<<mode);
        }
        assert(!hy_bc7_decode(encoded,sizeof encoded,32,32,expected,128,sizeof expected));
        NSString *source=@"#include <metal_stdlib>\nusing namespace metal;kernel void decode(texture2d<float> image [[texture(0)]],device uchar4 *out [[buffer(0)]],uint2 p [[thread_position_in_grid]]) {constexpr sampler s(coord::normalized,address::clamp_to_edge,filter::nearest);out[p.y*32+p.x]=uchar4(round(image.sample(s,(float2(p)+0.5f)/32.0f)*255.0f));}";
        NSError *error=nil;id<MTLLibrary> library=[d newLibraryWithSource:source options:nil error:&error];
        if(!library) {fprintf(stderr,"%s\n",error.description.UTF8String);return 1;}
        id<MTLComputePipelineState> pipeline=[d newComputePipelineStateWithFunction:[library newFunctionWithName:@"decode"] error:&error];assert(pipeline);
        MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBC7_RGBAUnorm width:32 height:32 mipmapped:NO];
        td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageShaderRead;
        id<MTLTexture> image=[d newTextureWithDescriptor:td];assert(image);
        [image replaceRegion:MTLRegionMake2D(0,0,32,32) mipmapLevel:0 withBytes:encoded bytesPerRow:128];
        id<MTLBuffer> output=[d newBufferWithLength:sizeof expected options:MTLResourceStorageModeShared];assert(output);
        id<MTLCommandQueue> q=[d newCommandQueue];id<MTLCommandBuffer> c=[q commandBuffer];
        id<MTLComputeCommandEncoder> e=[c computeCommandEncoder];[e setComputePipelineState:pipeline];
        [e setTexture:image atIndex:0];[e setBuffer:output offset:0 atIndex:0];
        [e dispatchThreads:MTLSizeMake(32,32,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];[e endEncoding];[c commit];[c waitUntilCompleted];
        assert(c.status==MTLCommandBufferStatusCompleted);
        if(argc>1) expected[0]^=1;
        unsigned differences=0,maximum=0;
        for(size_t i=0;i<sizeof expected;i++) {
            int delta=(int)expected[i]-((uint8_t *)output.contents)[i];if(delta<0)delta=-delta;
            if(delta) differences++;if((unsigned)delta>maximum)maximum=(unsigned)delta;
        }
        if(differences) {fprintf(stderr,"BC7 reference mismatch: %u bytes, max delta %u\n",differences,maximum);return 1;}
        puts("PASS: 64 BC7 blocks spanning all eight modes match native GPU decode in 4096 bytes");
    }
}
