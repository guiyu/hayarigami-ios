#include "../src/gfx/platform.m"
#include <assert.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdatomic.h>
HyModule hy_modules[4];size_t hy_module_count;
const char *const hy_nvn_names[] = {"nvnUnknown"};
void *const hy_nvn_gates[] = {NULL}; const size_t hy_nvn_name_count = 1;
void *hy_symbol(const char *name) { (void)name; abort(); }
_Noreturn void hy_fail(const char *format, ...) {
    va_list ap; va_start(ap, format); vfprintf(stderr, format, ap); va_end(ap); exit(42);
}
typedef struct { void *event; atomic_int entered, completed; } ResolutionWait;
static void *wait_resolution(void *context) {
    ResolutionWait *wait=context; atomic_store(&wait->entered,1);
    assert(hy_graphics_event(wait->event,0)==0);
    atomic_store(&wait->completed,1); return NULL;
}
int main(int argc, char **argv) {
    @autoreleasepool {
        _Alignas(8) uint8_t view_storage[40]; memset(view_storage,0xa5,sizeof view_storage);
        TextureView *bounded_view=(TextureView *)view_storage;
        view_defaults(bounded_view); view_levels(bounded_view,0,1); view_layers(bounded_view,0,1);
        view_format(bounded_view,0x25); view_target(bounded_view,1); view_depth_mode(bounded_view,0);
        view_swizzle(bounded_view,2,3,4,5);
        for (size_t i=16;i<sizeof view_storage;i++) assert(view_storage[i]==0xa5);
        initialize(NULL, 0);
        void *memory = NULL; assert(!posix_memalign(&memory, 0x4000, 0x8000));
        uint8_t *bytes = memory;
        for (size_t i = 0; i < 0x8000; i++) bytes[i] = (uint8_t)(i * 17 + 3);
        uint64_t device_object; uint32_t device_builder = 0;
        assert(initialize_device(&device_object, &device_builder));
        uint64_t (*timestamp_ns)(const void *,const HyCounterData *)=lookup("nvnDeviceGetTimestampInNanoseconds");
        HyCounterData timestamp={UINT64_MAX,123456789};assert(timestamp_ns(&device_object,&timestamp)==123456789);
        timestamp.timestamp=UINT64_MAX;assert(timestamp_ns(&device_object,&timestamp)==UINT64_MAX);
        PoolBuilder builder; pool_defaults(&builder); pool_device(&builder, &device_object);
        pool_storage(&builder, bytes + 0x1000, 0x5000); pool_flags(&builder, 0x24);
        Pool *pool = NULL; assert(pool_initialize(&pool, &builder));
        assert(pool_map(&pool) == bytes + 0x1000 && pool_size(&pool) == 0x5000);
        assert(pool_address(&pool) == (uintptr_t)(bytes + 0x1000));
        pool_flush(&pool, 0, 0x5000);
        id<MTLBuffer> readback = [device newBufferWithLength:0x5000 options:MTLResourceStorageModeShared];
        assert(readback);
        id<MTLCommandBuffer> commands = [queue commandBuffer];
        id<MTLBlitCommandEncoder> encoder = [commands blitCommandEncoder];
        [encoder copyFromBuffer:(__bridge id<MTLBuffer>)pool->buffer sourceOffset:pool->offset
            toBuffer:readback destinationOffset:0 size:0x5000];
        [encoder endEncoding]; [commands commit]; [commands waitUntilCompleted];
        assert(commands.status == MTLCommandBufferStatusCompleted);
        assert(!memcmp(readback.contents, bytes + 0x1000, 0x5000));
        BufferBuilder bb={.device=&device_object}; buffer_defaults(&bb);
        buffer_storage(&bb,&pool,7,64); Buffer buffer; assert(buffer_initialize(&buffer,&bb));
        assert(buffer_map(&buffer)==bytes+0x1007 && buffer_address(&buffer)==(uintptr_t)(bytes+0x1007));
        assert(buffer_size(&buffer)==64 && buffer_offset(&buffer)==7 && buffer_pool(&buffer)==&pool);
        if (argc>1 && !strcmp(argv[1],"--reject-buffer-range")) { buffer_flush(&buffer,31,34); return 1; }
        if (argc>1 && !strcmp(argv[1],"--reject-live-buffer")) { pool_finalize(&pool); return 1; }
        buffer_flush(&buffer,0,64); buffer_flush(&buffer,64,0);
        pool->flags|=1; assert(buffer_map(&buffer)==NULL); pool->flags&=~1u;
        buffer_finalize(&buffer); assert(!buffer.pool && !pool->buffers);
        DescriptorPool descriptors;
        assert(descriptor_initialize(&descriptors, &pool, 0, 5));
        assert(descriptors.count == 5 && descriptors.pool == pool);
        for (size_t i = 0; i < 5 * sizeof(Descriptor); i++) assert(bytes[0x1000 + i] == 0);
        assert(bytes[0x1000 + 5 * sizeof(Descriptor)] == (uint8_t)((0x1000 + 5 * sizeof(Descriptor)) * 17 + 3));
        _Alignas(8) uint8_t command_bytes[256]; HyCommands command_state;
        hy_commands_init(&command_state,&device_object); hy_commands_memory(&command_state,command_bytes,sizeof command_bytes);
        hy_commands_begin(&command_state); command_texture_pool(&command_state,&descriptors);
        command_sampler_pool(&command_state,&descriptors);
        uint8_t sample_bits[24]; memset(sample_bits,0x69,sizeof sample_bits);
        command_multisample(&command_state,sample_bits);
        command_counter(&command_state,0,pool_address(&pool)+0x3000); hy_commands_end(&command_state);
        HyRecord *pool_record=(HyRecord *)(command_bytes+8);
        assert(pool_record->op==HY_TEXTURE_POOL && pool_record->size==32);
        DescriptorPool *saved_pool=(DescriptorPool *)(command_bytes+16);
        descriptors.count=3; assert(saved_pool->count==5 && saved_pool->pool==pool); descriptors.count=5;
        assert(((HyRecord *)(command_bytes+40))->op==HY_SAMPLER_POOL);
        assert(((HyRecord *)(command_bytes+72))->op==HY_MULTISAMPLE_STATE);
        HyCounterRecord *counter=(HyCounterRecord *)(command_bytes+112);
        assert(((HyRecord *)(command_bytes+104))->op==HY_REPORT_COUNTER && counter->type==0 && counter->address==pool_address(&pool)+0x3000);
        memset(sample_bits,0,sizeof sample_bits);
        for (unsigned i=0;i<24;i++) assert(command_bytes[80+i]==0x69);
        pool->flags |= 1; assert(pool_map(&pool) == NULL); pool->flags &= ~1u;
        QueueBuilder qb = {.device = &device_object}; queue_defaults(&qb);
        assert(qb.device == &device_object && queue_memory_size(&qb) == 0);
        void *q = NULL; assert(queue_initialize(&q, &qb)); queue_finish(&q);
        GpuSync *sync=NULL; assert(sync_initialize(&sync,&device_object));
        assert(sync_wait(&sync,0)==3); // A newly initialized sync has no fence.
        id<MTLSharedEvent> gate=[device newSharedEvent]; assert(gate);
        id<MTLCommandBuffer> blocked=[(__bridge id<MTLCommandQueue>)q commandBuffer];
        [blocked encodeWaitForEvent:gate value:1]; [blocked commit];
        queue_fence(&q,&sync,0,1);
        assert(sync_wait(&sync,0)==2 && sync_wait(&sync,1000000)==2);
        void *dependent_queue=NULL; assert(queue_initialize(&dependent_queue,&qb));
        GpuSync *dependent_sync=NULL; assert(sync_initialize(&dependent_sync,&device_object));
        assert(queue_wait(&dependent_queue,&sync)); // Must enqueue and return while source is blocked.
        queue_fence(&dependent_queue,&dependent_sync,0,0);
        assert(sync_wait(&dependent_sync,1000000)==2);
        gate.signaledValue=1;
        int waited=sync_wait(&sync,2000000000); assert(waited==0 || waited==1);
        assert(sync_wait(&sync,0)==0);
        waited=sync_wait(&dependent_sync,2000000000); assert(waited==0 || waited==1);
        sync_finalize(&dependent_sync); queue_finalize(&dependent_queue);
        sync_finalize(&sync); assert(!sync);
        queue_finalize(&q); assert(q == NULL);
        display_initialize();
        void *display, *layer, *native;
        assert(!display_open(&display) && !display_layer(&layer, display) && !display_native(&native, layer));
        assert(native == (__bridge void *)surface && surface.device == device);
        uint64_t vsync, resolution;
        assert(!display_vsync(&vsync, display)); display_resolution_event(&resolution);
        assert(!display_poll(&resolution));
        assert(hy_graphics_event(&vsync, 0) == 1);
        display_clear(&vsync); assert(!display_poll(&vsync));
        ResolutionWait wait={.event=&resolution}; pthread_t resolution_thread;
        assert(!pthread_create(&resolution_thread,NULL,wait_resolution,&wait));
        while (!atomic_load(&wait.entered)) usleep(1000);
        usleep(20000); assert(!atomic_load(&wait.completed));
        assert(hy_graphics_event(&resolution,3)==1);
        assert(!pthread_join(resolution_thread,NULL) && atomic_load(&wait.completed));
        assert(hy_graphics_event(&vsync,3)==1);
        assert(hy_graphics_event(&vsync, 1) == -1);
        TextureBuilder tb = {.device=&device_object}; texture_defaults(&tb);
        texture_size2d(&tb, 19, 17); texture_format(&tb, 0x25); texture_storage(&tb, &pool, 0);
        if (argc>1) {
            texture_size2d(&tb,19,16); uint8_t layout[8]={0}; texture_layout(&tb,layout);
            assert(texture_size(&tb)==2048); Texture first; assert(texture_initialize(&first,&tb));
            layout[0]=1; texture_layout(&tb,layout); assert(texture_size(&tb)==2048);
            Texture incompatible; texture_initialize(&incompatible,&tb);
            return 1; // Must fail before two incompatible layouts share a Metal image.
        }
        nvn_texdesc td = texture_description(&tb);
        uint8_t pixels[19*17*4], copied[sizeof pixels];
        for (size_t i=0;i<sizeof pixels;i++) pixels[i]=(uint8_t)(i*29+7);
        assert(!nvn_swizzle(&td,0,0,pixels,19*4,pool->storage));
        Texture texture; assert(texture_initialize(&texture,&tb));
        id<MTLTexture> gpu_image = (__bridge id<MTLTexture>)texture.image;
        [gpu_image getBytes:copied bytesPerRow:19*4 fromRegion:MTLRegionMake2D(0,0,19,17) mipmapLevel:0];
        assert(!memcmp(pixels,copied,sizeof pixels));
        MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture=gpu_image;
        pass.colorAttachments[0].loadAction=MTLLoadActionClear;
        pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        pass.colorAttachments[0].clearColor=MTLClearColorMake(1,0,0,1);
        id<MTLCommandBuffer> clear=[queue commandBuffer];
        id<MTLRenderCommandEncoder> render=[clear renderCommandEncoderWithDescriptor:pass];
        [render endEncoding]; [clear commit]; [clear waitUntilCompleted];
        assert(clear.status == MTLCommandBufferStatusCompleted);
        [gpu_image getBytes:copied bytesPerRow:19*4 fromRegion:MTLRegionMake2D(0,0,19,17) mipmapLevel:0];
        for (size_t i=0;i<sizeof copied;i+=4) assert(copied[i]==255 && copied[i+1]==0 && copied[i+2]==0 && copied[i+3]==255);
        int (*width)(const Texture *)=lookup("nvnTextureGetWidth");
        int (*height)(const Texture *)=lookup("nvnTextureGetHeight");
        assert(width(&texture)==19 && height(&texture)==17);
        Texture alias; assert(texture_initialize(&alias,&tb)); assert(alias.image==texture.image);
        WindowBuilder wb={.device=&device_object}; window_defaults(&wb);
        window_native(&wb,native); Texture *window_images[]={&texture,&alias}; window_textures(&wb,2,window_images);
        Window window; assert(window_initialize(&window,&wb)); window_crop(&window,1,2,18,15);
        void (*set_interval)(Window *,int)=lookup("nvnWindowSetPresentInterval");
        int (*get_interval)(const Window *)=lookup("nvnWindowGetPresentInterval");
        assert(set_interval && get_interval && get_interval(&window)==1);
        set_interval(&window,0); assert(get_interval(&window)==0);
        set_interval(&window,1); assert(get_interval(&window)==1);
        int (*acquire)(Window *,GpuSync **,int *)=lookup("nvnWindowAcquireTexture");
        GpuSync *available=NULL;assert(sync_initialize(&available,&device_object));
        int image_index=-1;assert(!acquire(&window,&available,&image_index) && image_index==0);
        int ready=sync_wait(&available,2000000000);assert(ready==0 || ready==1);
        assert(!acquire(&window,&available,&image_index) && image_index==1);
        ready=sync_wait(&available,2000000000);assert(ready==0 || ready==1);sync_finalize(&available);
        int crop[4]; window_get_crop(&window,crop); assert(!memcmp(crop,(int[]){1,2,18,15},sizeof crop));
        texture_finalize(&texture); assert(!texture.image && pool->textures->references==1);
        [(__bridge id<MTLTexture>)alias.image getBytes:copied bytesPerRow:19*4 fromRegion:MTLRegionMake2D(0,0,19,17) mipmapLevel:0];
        assert(copied[0]==255 && copied[1]==0 && copied[3]==255);
        // Keep descriptor memory separate from texture bytes.
        HySampler *captured=[HySampler new]; captured->config=(HySamplerConfig){.min_filter=1,.mag_filter=1,
            .wrap={2,2,2},.lod_max=16,.anisotropy=1};
        void *sampler_object=(__bridge_retained void *)captured;
        DescriptorPool sampler_descriptors; assert(descriptor_initialize(&sampler_descriptors,&pool,0x4800,2));
        register_sampler(&sampler_descriptors,0,&sampler_object); sampler_finalize(&sampler_object);
        HySampler *retained=(__bridge HySampler *)descriptor_slot(&sampler_descriptors,0)->resource;
        assert(retained->config.wrap[0]==2); descriptor_finalize(&sampler_descriptors);
        DescriptorPool texture_descriptors; assert(descriptor_initialize(&texture_descriptors,&pool,0x4000,2));
        register_texture(&texture_descriptors,0,&alias,NULL);
        texture_finalize(&alias); assert(pool->textures && pool->textures->references==1);
        Texture recreated; assert(texture_initialize(&recreated,&tb));
        assert(recreated.image==descriptor_slot(&texture_descriptors,0)->resource);
        [(__bridge id<MTLTexture>)recreated.image getBytes:copied bytesPerRow:19*4 fromRegion:MTLRegionMake2D(0,0,19,17) mipmapLevel:0];
        for (size_t i=0;i<sizeof copied;i+=4) assert(copied[i]==255 && copied[i+1]==0 && copied[i+2]==0 && copied[i+3]==255);
        register_texture(&texture_descriptors,0,&recreated,NULL);
        assert(pool->textures->references==2);
        texture_finalize(&recreated);
        assert(descriptor_slot(&texture_descriptors,0)->resource!=NULL);
        descriptor_finalize(&texture_descriptors); assert(!pool->textures);
        pool_finalize(&pool); assert(!pool); free(memory);
        void *sparse_memory=NULL; assert(!posix_memalign(&sparse_memory,65536,262144));
        pool_storage(&builder,sparse_memory,262144); assert(pool_initialize(&pool,&builder));
        texture_defaults(&tb); texture_size2d(&tb,129,129); texture_format(&tb,0x3d);
        texture_flags(&tb,0x28); texture_storage(&tb,&pool,0);
        assert(texture_size(&tb)==262144 && texture_alignment(&tb)==65536);
        uint32_t *packed=malloc(129*129*4), *packed_copy=malloc(129*129*4); assert(packed && packed_copy);
        for (size_t i=0;i<129*129;i++) packed[i]=0xc0000000u|(uint32_t)(i&1023);
        td=texture_description(&tb); assert(!nvn_swizzle(&td,0,0,packed,129*4,sparse_memory));
        assert(texture_initialize(&texture,&tb));
        gpu_image=(__bridge id<MTLTexture>)texture.image;
        assert(gpu_image.pixelFormat==MTLPixelFormatRGB10A2Unorm);
        [gpu_image getBytes:packed_copy bytesPerRow:129*4 fromRegion:MTLRegionMake2D(0,0,129,129) mipmapLevel:0];
        assert(!memcmp(packed,packed_copy,129*129*4));
        texture_finalize(&texture); pool_finalize(&pool); free(sparse_memory); free(packed); free(packed_copy);
        void *r11_memory=NULL;assert(!posix_memalign(&r11_memory,65536,262144));
        pool_storage(&builder,r11_memory,262144);assert(pool_initialize(&pool,&builder));
        texture_defaults(&tb);texture_size2d(&tb,129,129);texture_format(&tb,0x3f);texture_flags(&tb,0x28);texture_storage(&tb,&pool,0);
        memset(r11_memory,0,262144);assert(texture_initialize(&texture,&tb));
        gpu_image=(__bridge id<MTLTexture>)texture.image;assert(gpu_image.pixelFormat==MTLPixelFormatRG11B10Float);
        pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=gpu_image;
        pass.colorAttachments[0].loadAction=MTLLoadActionClear;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
        pass.colorAttachments[0].clearColor=MTLClearColorMake(1,2,0.5,1);
        clear=[queue commandBuffer];render=[clear renderCommandEncoderWithDescriptor:pass];[render endEncoding];
        [clear commit];[clear waitUntilCompleted];assert(clear.status==MTLCommandBufferStatusCompleted);
        uint32_t *r11=malloc(129*129*4);assert(r11);
        [gpu_image getBytes:r11 bytesPerRow:129*4 fromRegion:MTLRegionMake2D(0,0,129,129) mipmapLevel:0];
        for(unsigned i=0;i<129*129;i++) assert(r11[i]==0x702003c0u);
        texture_finalize(&texture);pool_finalize(&pool);free(r11);free(r11_memory);
        void *r8_memory=NULL;assert(!posix_memalign(&r8_memory,65536,262144));
        pool_storage(&builder,r8_memory,262144);assert(pool_initialize(&pool,&builder));
        texture_defaults(&tb);texture_size2d(&tb,257,257);texture_format(&tb,1);texture_flags(&tb,0x28);texture_storage(&tb,&pool,0);
        uint8_t *r8=malloc(257*257),*r8_copy=malloc(257*257);assert(r8 && r8_copy);
        for(size_t i=0;i<257*257;i++) r8[i]=(uint8_t)(i*37+11);
        assert(texture_size(&tb)==262144);td=texture_description(&tb);
        assert(!nvn_swizzle(&td,0,0,r8,257,r8_memory));assert(texture_initialize(&texture,&tb));
        gpu_image=(__bridge id<MTLTexture>)texture.image;assert(gpu_image.pixelFormat==MTLPixelFormatR8Unorm);
        [gpu_image getBytes:r8_copy bytesPerRow:257 fromRegion:MTLRegionMake2D(0,0,257,257) mipmapLevel:0];
        assert(!memcmp(r8,r8_copy,257*257));
        texture_finalize(&texture);pool_finalize(&pool);free(r8_memory);free(r8);free(r8_copy);
        void *bc_memory=NULL;assert(!posix_memalign(&bc_memory,65536,262144));
        pool_storage(&builder,bc_memory,262144);assert(pool_initialize(&pool,&builder));
        texture_defaults(&tb);texture_size3d(&tb,257,257,1);texture_format(&tb,0x4d);texture_flags(&tb,0x20);texture_storage(&tb,&pool,0);
        const uint8_t block_colors[4][16]={
            {0xc0,0xff,0x1f,0,0,0,0xfe,0x7f},
            {0x40,0,0xe0,0xff,7,0,0xfe,0x7f},
            {0x40,0,0,0,0xf8,0xff,0xff,0x7f},
            {0xc0,0xff,0xff,0xff,0xff,0xff,0xff,0x7f}};
        uint8_t *blocks=malloc(65*65*16),*bc_pixels=malloc(257*257*4);assert(blocks && bc_pixels);
        for(unsigned by=0;by<65;by++) for(unsigned bx=0;bx<65;bx++) memcpy(blocks+(by*65+bx)*16,block_colors[(bx*7+by*13)%4],16);
        size_t page=(size_t)sysconf(_SC_PAGESIZE);uint8_t *guarded=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);
        assert(guarded!=MAP_FAILED && !mprotect(guarded+page,page,PROT_NONE));
        guarded[page-2]=3;guarded[page-1]=4;texture_layout(&tb,guarded+page-2);assert(!munmap(guarded,page*2));
        td=texture_description(&tb);assert(texture_size(&tb)==262144);
        assert(!nvn_swizzle(&td,0,0,blocks,65*16,bc_memory));assert(texture_initialize(&texture,&tb));
        gpu_image=(__bridge id<MTLTexture>)texture.image;assert(gpu_image.pixelFormat==MTLPixelFormatRGBA8Unorm);
        [gpu_image getBytes:bc_pixels bytesPerRow:257*4 fromRegion:MTLRegionMake2D(0,0,257,257) mipmapLevel:0];
        const uint8_t rgba_colors[4][4]={{254,0,0,254},{0,254,0,254},{0,0,254,254},{254,254,254,254}};
        for(unsigned y=0;y<257;y++) for(unsigned x=0;x<257;x++) assert(!memcmp(bc_pixels+(y*257+x)*4,rgba_colors[((x/4)*7+(y/4)*13)%4],4));
        texture_finalize(&texture);pool_finalize(&pool);free(blocks);free(bc_pixels);free(bc_memory);
        void *depth_memory=NULL; assert(!posix_memalign(&depth_memory,65536,393216));
        pool_storage(&builder,depth_memory,393216); pool_flags(&builder,0xa1);
        assert(pool_initialize(&pool,&builder));
        texture_defaults(&tb); texture_size2d(&tb,129,129); texture_format(&tb,0x36);
        texture_flags(&tb,0x28); texture_depth_mode(&tb,0); texture_storage(&tb,&pool,0);
        assert(texture_size(&tb)==393216 && texture_initialize(&texture,&tb));
        gpu_image=(__bridge id<MTLTexture>)texture.image;
        pass=[MTLRenderPassDescriptor renderPassDescriptor];
        pass.depthAttachment.texture=gpu_image; pass.depthAttachment.loadAction=MTLLoadActionClear;
        pass.depthAttachment.storeAction=MTLStoreActionStore; pass.depthAttachment.clearDepth=0.25;
        pass.stencilAttachment.texture=gpu_image; pass.stencilAttachment.loadAction=MTLLoadActionClear;
        pass.stencilAttachment.storeAction=MTLStoreActionStore; pass.stencilAttachment.clearStencil=83;
        id<MTLBuffer> depth_readback=[device newBufferWithLength:(768+256)*129 options:MTLResourceStorageModeShared];
        assert(depth_readback); clear=[queue commandBuffer];
        render=[clear renderCommandEncoderWithDescriptor:pass]; [render endEncoding];
        encoder=[clear blitCommandEncoder];
        [encoder copyFromTexture:gpu_image sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(129,129,1) toBuffer:depth_readback destinationOffset:0
            destinationBytesPerRow:768 destinationBytesPerImage:768*129 options:MTLBlitOptionDepthFromDepthStencil];
        [encoder copyFromTexture:gpu_image sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
            sourceSize:MTLSizeMake(129,129,1) toBuffer:depth_readback destinationOffset:768*129
            destinationBytesPerRow:256 destinationBytesPerImage:256*129 options:MTLBlitOptionStencilFromDepthStencil];
        [encoder endEncoding]; [clear commit]; [clear waitUntilCompleted];
        assert(clear.status==MTLCommandBufferStatusCompleted);
        for (unsigned y=0;y<129;y++) for (unsigned x=0;x<129;x++) {
            assert(*(float *)((uint8_t *)depth_readback.contents+y*768+x*4)==0.25f);
            assert(((uint8_t *)depth_readback.contents)[768*129+y*256+x]==83);
        }
        texture_finalize(&texture); pool_finalize(&pool); free(depth_memory);
        puts("PASS: Metal GPU copied 20480 bytes exactly; descriptor writes stayed in range; command queue completed and released");
    }
}
