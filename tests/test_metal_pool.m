#include "../src/gfx/platform.m"
#include <assert.h>
#include <stdarg.h>
const char *const hy_nvn_names[] = {"nvnUnknown"};
void *const hy_nvn_gates[] = {NULL}; const size_t hy_nvn_name_count = 1;
void *hy_symbol(const char *name) { (void)name; abort(); }
_Noreturn void hy_fail(const char *format, ...) {
    va_list ap; va_start(ap, format); vfprintf(stderr, format, ap); va_end(ap); exit(42);
}
int main(int argc, char **argv) {
    (void)argv;
    @autoreleasepool {
        _Alignas(8) uint8_t view_storage[40]; memset(view_storage,0xa5,sizeof view_storage);
        TextureView *bounded_view=(TextureView *)view_storage;
        view_defaults(bounded_view); view_levels(bounded_view,0,1); view_layers(bounded_view,0,1);
        view_format(bounded_view,0x25); view_target(bounded_view,1); view_depth_mode(bounded_view,0);
        view_swizzle(bounded_view,2,3,4,5);
        for (size_t i=32;i<sizeof view_storage;i++) assert(view_storage[i]==0xa5);
        initialize(NULL, 0);
        void *memory = NULL; assert(!posix_memalign(&memory, 0x4000, 0x8000));
        uint8_t *bytes = memory;
        for (size_t i = 0; i < 0x8000; i++) bytes[i] = (uint8_t)(i * 17 + 3);
        uint64_t device_object; uint32_t device_builder = 0;
        assert(initialize_device(&device_object, &device_builder));
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
        DescriptorPool descriptors;
        assert(descriptor_initialize(&descriptors, &pool, 0, 5));
        assert(descriptors.count == 5 && descriptors.pool == pool);
        for (size_t i = 0; i < 5 * sizeof(Descriptor); i++) assert(bytes[0x1000 + i] == 0);
        assert(bytes[0x1000 + 5 * sizeof(Descriptor)] == (uint8_t)((0x1000 + 5 * sizeof(Descriptor)) * 17 + 3));
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
        assert(hy_graphics_event(&resolution, 3) == 1 && hy_graphics_event(&vsync, 3) == 1);
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
        Texture alias; assert(texture_initialize(&alias,&tb)); assert(alias.image==texture.image);
        texture_finalize(&texture); assert(!texture.image && pool->textures->references==1);
        [(__bridge id<MTLTexture>)alias.image getBytes:copied bytesPerRow:19*4 fromRegion:MTLRegionMake2D(0,0,19,17) mipmapLevel:0];
        assert(copied[0]==255 && copied[1]==0 && copied[3]==255);
        // Keep descriptor memory separate from texture bytes.
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
        puts("PASS: Metal GPU copied 20480 bytes exactly; descriptor writes stayed in range; command queue completed and released");
    }
}
