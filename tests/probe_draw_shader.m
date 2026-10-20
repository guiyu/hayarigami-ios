// Original first-draw stages, with synthetic indexed vertices and transform constants.
#include "../src/gfx/platform.m"
#include <assert.h>
#include <stdarg.h>
HyModule hy_modules[4];size_t hy_module_count;
const char *const hy_nvn_names[]={"nvnUnknown"};void *const hy_nvn_gates[]={NULL};const size_t hy_nvn_name_count=1;
void *hy_symbol(const char *name) {(void)name;abort();}
_Noreturn void hy_fail(const char *format,...) {va_list ap;va_start(ap,format);vfprintf(stderr,format,ap);va_end(ap);exit(42);}
static HyShader *read_shader(NSString *source,NSString *key,unsigned stage) {
    HyShader *shader=[HyShader new];shader->stage=stage;
    shader->control=[NSData dataWithContentsOfFile:[source stringByAppendingPathComponent:[key stringByAppendingString:@".control.bin"]]];
    shader->code=[NSData dataWithContentsOfFile:[source stringByAppendingPathComponent:[key stringByAppendingString:@".code.bin"]]];
    shader->constants=[NSData dataWithContentsOfFile:[source stringByAppendingPathComponent:[key stringByAppendingString:@".constants.bin"]]];
    assert(shader->control && shader->code && shader->constants);
    snprintf(shader->key,sizeof shader->key,"%s",key.UTF8String);return shader;
}
int main(int argc,char **argv) {
    if(argc<3 || argc>4) return 2;
    @autoreleasepool {
        initialize(NULL,0);NSString *root=@(argv[2]);
        HyShader *vertex=read_shader(@(argv[1]),@"8b48fccc06e7316ced2cffd9e0d2467a3526bec08b167bdf06cc98007b30878b",1);
        HyShader *fragment=read_shader(@(argv[1]),@"803c26109c02466dc4077dcf0644fc0ce9b020061279fe179ce683b4a33eddb0",5);
        if(argc==4 && !strcmp(argv[3],"--bad-code")) {
            NSMutableData *changed=[vertex->code mutableCopy];((uint8_t *)changed.mutableBytes)[0]^=1;vertex->code=changed;
        }
        if(argc==4 && !strcmp(argv[3],"--bad-reference")) {
            hy_metal_shader(fragment,root,5,0.5f,NULL);abort();
        }
        NSDictionary *vinfo=nil,*finfo=nil;
        id<MTLFunction> vs=hy_metal_shader(vertex,root,8,0,&vinfo);
        id<MTLFunction> fs=hy_metal_shader(fragment,root,5,0,&finfo);
        assert([vinfo[@"CBuffers"] count]==1 && ![finfo[@"CBuffers"] count]);
        MTLRenderPipelineDescriptor *p=[MTLRenderPipelineDescriptor new];p.vertexFunction=vs;p.fragmentFunction=fs;
        p.colorAttachments[0].pixelFormat=MTLPixelFormatRGBA8Unorm;
        p.vertexDescriptor=[MTLVertexDescriptor vertexDescriptor];
        p.vertexDescriptor.attributes[0].format=MTLVertexFormatFloat3;p.vertexDescriptor.attributes[0].bufferIndex=16;
        p.vertexDescriptor.attributes[1].format=MTLVertexFormatUChar4Normalized;p.vertexDescriptor.attributes[1].offset=12;p.vertexDescriptor.attributes[1].bufferIndex=16;
        p.vertexDescriptor.layouts[16].stride=16;
        NSError *error=nil;id<MTLRenderPipelineState> pipeline=[device newRenderPipelineStateWithDescriptor:p error:&error];
        if(!pipeline) {fprintf(stderr,"%s\n",error.description.UTF8String);return 1;}
        struct Vertex {float x,y,z;uint8_t color[4];};
        struct Vertex vertices[]={{-1,-1,0,{255,0,0,255}},{1,-1,0,{255,0,0,255}},{-1,1,0,{255,0,0,255}},{1,1,0,{255,0,0,255}}};
        uint16_t indices[]={0,1,2,3};
        id<MTLBuffer> vb=[device newBufferWithBytes:vertices length:sizeof vertices options:MTLResourceStorageModeShared];
        id<MTLBuffer> ib=[device newBufferWithBytes:indices length:sizeof indices options:MTLResourceStorageModeShared];
        id<MTLBuffer> ub=[device newBufferWithLength:65536 options:MTLResourceStorageModeShared];assert(vb && ib && ub);
        memset(ub.contents,0,ub.length);float *matrix=ub.contents;for(unsigned i=0;i<4;i++)matrix[i*5]=matrix[16+i*5]=1;
        MTLTextureDescriptor *td=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:16 height:16 mipmapped:NO];
        td.storageMode=MTLStorageModeShared;td.usage=MTLTextureUsageRenderTarget;id<MTLTexture> output=[device newTextureWithDescriptor:td];assert(output);
        MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=output;
        pass.colorAttachments[0].loadAction=MTLLoadActionClear;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,1,1);
        id<MTLCommandBuffer> commands=[queue commandBuffer];id<MTLRenderCommandEncoder> encoder=[commands renderCommandEncoderWithDescriptor:pass];
        [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:vb offset:0 atIndex:16];[encoder setVertexBuffer:ub offset:0 atIndex:1];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangleStrip indexCount:4 indexType:MTLIndexTypeUInt16 indexBuffer:ib indexBufferOffset:0];
        [encoder endEncoding];[commands commit];[commands waitUntilCompleted];assert(commands.status==MTLCommandBufferStatusCompleted);
        uint8_t pixels[16*16*4];[output getBytes:pixels bytesPerRow:64 fromRegion:MTLRegionMake2D(0,0,16,16) mipmapLevel:0];
        for(unsigned i=0;i<256;i++)assert(!memcmp(pixels+4*i,(uint8_t[]){255,0,0,255},4));
        puts("PASS: verified first-draw Metal libraries rendered all 256 indexed pixels with captured vertex and constant-buffer layouts");
    }
}
