// Synthetic vertex stage plus an original translated fragment stage.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <stdio.h>
int main(int argc,char **argv) {
    if(argc!=2)return 2;
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();assert(device);NSError *error=nil;
        NSString *source=@"#include <metal_stdlib>\nusing namespace metal;struct Out{float4 p [[position]];float4 color [[user(locn0)]];};vertex Out main0(uint id [[vertex_id]],constant float4 &color [[buffer(0)]]){float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return {float4(p[id],0,1),color};}";
        id<MTLLibrary> vertex=[device newLibraryWithSource:source options:nil error:&error];assert(vertex);
        id<MTLLibrary> fragment=[device newLibraryWithURL:[NSURL fileURLWithPath:@(argv[1])] error:&error];
        if(!fragment){fprintf(stderr,"%s\n",error.description.UTF8String);return 1;}
        MTLRenderPipelineDescriptor *pipeline=[MTLRenderPipelineDescriptor new];pipeline.vertexFunction=[vertex newFunctionWithName:@"main0"];
        pipeline.fragmentFunction=[fragment newFunctionWithName:@"main0"];pipeline.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
        id<MTLRenderPipelineState> state=[device newRenderPipelineStateWithDescriptor:pipeline error:&error];assert(state);
        MTLTextureDescriptor *descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:12 height:4 mipmapped:NO];
        descriptor.storageMode=MTLStorageModeShared;descriptor.usage=MTLTextureUsageRenderTarget;id<MTLTexture> output=[device newTextureWithDescriptor:descriptor];assert(output);
        id<MTLCommandQueue> queue=[device newCommandQueue];id<MTLCommandBuffer> commands=[queue commandBuffer];
        MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=output;
        pass.colorAttachments[0].loadAction=MTLLoadActionClear;pass.colorAttachments[0].storeAction=MTLStoreActionStore;pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,1,1);
        id<MTLRenderCommandEncoder> encoder=[commands renderCommandEncoderWithDescriptor:pass];[encoder setRenderPipelineState:state];
        for(unsigned tile=0;tile<3;tile++) {
            float color[]={1,0,0,tile*0.5f};[encoder setVertexBytes:color length:sizeof color atIndex:0];
            [encoder setViewport:(MTLViewport){tile*4,0,4,4,0,1}];[encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        }
        [encoder endEncoding];[commands commit];[commands waitUntilCompleted];assert(commands.status==MTLCommandBufferStatusCompleted);
        uint8_t pixels[12*4*4];[output getBytes:pixels bytesPerRow:48 fromRegion:MTLRegionMake2D(0,0,12,4) mipmapLevel:0];
        const uint8_t expected[3][4]={{0,0,255,255},{255,0,0,128},{255,0,0,255}};
        for(unsigned y=0;y<4;y++)for(unsigned x=0;x<12;x++)if(memcmp(pixels+4*(y*12+x),expected[x/4],4)) {
            fprintf(stderr,"Alpha test mismatch at %u,%u\n",x,y);return 1;
        }
        puts("PASS: original fragment alpha>0 variant discards all zero-alpha pixels and preserves 0.5/1.0 alpha");
    }
}
