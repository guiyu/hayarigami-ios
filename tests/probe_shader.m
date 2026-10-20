#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <assert.h>
#include <stdio.h>
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    @autoreleasepool {
        id<MTLDevice> d=MTLCreateSystemDefaultDevice(); assert(d); NSError *error=nil;
        NSString *v=[NSString stringWithContentsOfFile:@(argv[1]) encoding:NSUTF8StringEncoding error:&error]; assert(v);
        NSString *f=[NSString stringWithContentsOfFile:@(argv[2]) encoding:NSUTF8StringEncoding error:&error]; assert(f);
        MTLCompileOptions *options=[MTLCompileOptions new]; options.mathMode=MTLMathModeSafe;
        id<MTLLibrary> vl=[d newLibraryWithSource:v options:options error:&error];
        if(!vl) {fprintf(stderr,"%s\n",error.description.UTF8String);return 1;}
        id<MTLLibrary> fl=[d newLibraryWithSource:f options:options error:&error];
        if(!fl) {fprintf(stderr,"%s\n",error.description.UTF8String);return 1;}
        MTLRenderPipelineDescriptor *p=[MTLRenderPipelineDescriptor new];
        p.vertexFunction=[vl newFunctionWithName:@"main0"]; p.fragmentFunction=[fl newFunctionWithName:@"main0"];
        p.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
        p.vertexDescriptor=[MTLVertexDescriptor vertexDescriptor];
        p.vertexDescriptor.attributes[0].format=MTLVertexFormatFloat2; p.vertexDescriptor.attributes[0].bufferIndex=0;
        p.vertexDescriptor.layouts[0].stride=8;
        id<MTLRenderPipelineState> ps=[d newRenderPipelineStateWithDescriptor:p error:&error];
        if(!ps) {fprintf(stderr,"%s\n",error.description.UTF8String);return 1;}
        MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:2 height:2 mipmapped:NO];
        td.storageMode=MTLStorageModeShared; id<MTLTexture> input=[d newTextureWithDescriptor:td];
        uint8_t pixels[]={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
        [input replaceRegion:MTLRegionMake2D(0,0,2,2) mipmapLevel:0 withBytes:pixels bytesPerRow:8];
        td.width=32;td.height=32;td.usage=MTLTextureUsageRenderTarget; id<MTLTexture> output=[d newTextureWithDescriptor:td];
        MTLSamplerDescriptor *sd=[MTLSamplerDescriptor new];sd.minFilter=MTLSamplerMinMagFilterNearest;sd.magFilter=MTLSamplerMinMagFilterNearest;
        sd.sAddressMode=MTLSamplerAddressModeClampToEdge;sd.tAddressMode=MTLSamplerAddressModeClampToEdge;
        id<MTLSamplerState> sampler=[d newSamplerStateWithDescriptor:sd];
        float vertices[]={-1,-1,3,-1,-1,3}; id<MTLBuffer> vb=[d newBufferWithBytes:vertices length:sizeof vertices options:MTLResourceStorageModeShared];
        id<MTLCommandQueue> q=[d newCommandQueue];id<MTLCommandBuffer> c=[q commandBuffer];
        MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture=output;pass.colorAttachments[0].loadAction=MTLLoadActionClear;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        id<MTLRenderCommandEncoder> e=[c renderCommandEncoderWithDescriptor:pass];
        [e setRenderPipelineState:ps];[e setVertexBuffer:vb offset:0 atIndex:0];[e setFragmentTexture:input atIndex:1];[e setFragmentSamplerState:sampler atIndex:1];
        [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[e endEncoding];[c commit];[c waitUntilCompleted];assert(c.status==MTLCommandBufferStatusCompleted);
        uint8_t readback[32*32*4];[output getBytes:readback bytesPerRow:128 fromRegion:MTLRegionMake2D(0,0,32,32) mipmapLevel:0];
        for(unsigned y=0;y<32;y++) for(unsigned x=0;x<32;x++)
            assert(!memcmp(readback+4*(y*32+x),pixels+4*((y/16)*2+x/16),4));
        puts("PASS: translated original vertex/fragment pair rendered all 1024 pixels with correct UV orientation");
    }
}
