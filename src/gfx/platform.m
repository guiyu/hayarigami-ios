#include "../runtime/runtime.h"
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <libkern/OSCacheControl.h>
#include "vertex.h"
#include "commands.h"
#include "sampler.h"
#include "shader.h"
#include "bc7.h"
#include <CommonCrypto/CommonDigest.h>
#include <TargetConditionals.h>
#include "nvn_deswizzle.h"
extern const char *const hy_nvn_names[];
extern void *const hy_nvn_gates[];
extern const size_t hy_nvn_name_count;
static id<MTLDevice> device;
static id<MTLCommandQueue> queue;
static uint32_t device_flags;
static int depth_mode, origin_mode;
static CAMetalLayer *surface;
static uint64_t display_token;
static void *vsync_event, *resolution_event;
static uint64_t next_tick;
static pthread_mutex_t display_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t display_changed = PTHREAD_COND_INITIALIZER;
static int app_active=1;
static void graphics_wait_active(void) {
    pthread_mutex_lock(&display_lock);while(!app_active)pthread_cond_wait(&display_changed,&display_lock);pthread_mutex_unlock(&display_lock);
}
void hy_graphics_set_active(int active) {
    pthread_mutex_lock(&display_lock);app_active=!!active;pthread_cond_broadcast(&display_changed);pthread_mutex_unlock(&display_lock);
}
typedef struct { void *device, *storage; size_t size; uint32_t flags; } PoolBuilder;
typedef struct Pool {
    void *storage, *buffer; size_t size, offset; uint32_t flags;
    struct TextureBacking *textures; unsigned buffers; pthread_mutex_t lock;
    struct Pool *next;
} Pool;
static void replay_release_pool(Pool *pool);
static void replay_release_queue(void *queue_object);
static Pool *pools;
static pthread_mutex_t pools_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t descriptors_lock=PTHREAD_MUTEX_INITIALIZER;
// Descriptors are host references, never Maxwell descriptor words.
typedef struct { void *resource; void *view; } Descriptor;
typedef struct { Pool *pool; size_t offset; int count; } DescriptorPool;
enum { DESCRIPTOR_LIMIT = 65536 };
typedef struct {
    void *device; uint32_t flags;
    size_t command_size, compute_size, control_size, flush_threshold;
    void *memory; size_t memory_size;
} QueueBuilder;
_Static_assert(sizeof(QueueBuilder) <= 64, "NVN queue builder size");
typedef struct {
    void *device;
    int flags, target, width, height, depth, levels, format, samples;
    Pool **pool; ptrdiff_t offset, stride;
    uint16_t layout; int has_layout; uint8_t swizzle[4],has_swizzle;
} TextureBuilder;
_Static_assert(sizeof(TextureBuilder) <= 128, "NVN texture builder size");
typedef struct TextureBacking {
    struct TextureBacking *next; Pool *pool;
    size_t offset, size; int width, height, levels, format; unsigned tile_width, tile_height; uint32_t flags,stride;
    unsigned references,objects; void *image;
} TextureBacking;
typedef struct { TextureBuilder builder; void *image; TextureBacking *backing; } Texture;
_Static_assert(sizeof(Texture) <= 128, "NVN texture ABI size");
typedef struct { uint8_t level,levels;uint16_t layer,layers,format;uint8_t target,fields,depth_mode,swizzle[4]; } TextureView;
typedef struct { void *device, *native; int count, interval; Texture *textures[4]; } WindowBuilder;
typedef struct { WindowBuilder builder; int next, crop[4]; uint8_t acquired[4]; uint64_t frames; } Window;
_Static_assert(sizeof(TextureView) == 16 && sizeof(WindowBuilder) <= 64 && sizeof(Window) <= 384, "NVN view/window ABI sizes");
@interface HySampler : NSObject {
@public
    HySamplerConfig config;
@private
    id<MTLSamplerState> state;
}
- (id<MTLSamplerState>)stateForDevice:(id<MTLDevice>)target;
@end
@implementation HySampler
- (id<MTLSamplerState>)stateForDevice:(id<MTLDevice>)target {
    @synchronized (self) {
        if (!state) state=hy_sampler_create(target,&config);
        if (!state) hy_fail("Metal sampler allocation failed");
        return state;
    }
}
@end
static void initialize(void *driver_memory, size_t size) {
    (void)driver_memory; (void)size; // Switch driver IPC storage is not used by Metal.
    @autoreleasepool {
        device = MTLCreateSystemDefaultDevice();
        if (!device) hy_fail("Metal device unavailable");
        queue = [device newCommandQueue];
        if (!queue) hy_fail("Metal command queue unavailable");
        fputs("graphics platform: Metal device and command queue created\n", stderr);
    }
}
static void allocator(void *allocate, void *free_fn, void *reallocate, void *user) {
    (void)allocate; (void)free_fn; (void)reallocate; (void)user;
    // Metal owns its driver allocations; guest resource allocations remain guest-owned.
}
_Noreturn void hy_nvn_unimplemented(unsigned index) {
    if (index >= hy_nvn_name_count) hy_fail("invalid NVN gate");
    hy_fail("NVN operation not implemented: %s", hy_nvn_names[index]);
}
static void *lookup(const char *name);
static void *get_proc(const void *unused, const char *name) { (void)unused; return lookup(name); }
static void builder_defaults(void *builder) { *(uint32_t *)builder = 0; }
static void builder_flags(void *builder, uint32_t flags) { *(uint32_t *)builder = flags; }
static uint32_t initialize_device(void *object, const void *builder) {
    if (!device || !queue) hy_fail("NVN device initialized before Metal");
    *(uint64_t *)object = 0x48594e564e444556ull;
    device_flags = *(const uint32_t *)builder;
    fprintf(stderr, "NVN device created: flags=%#x\n", device_flags);
    return 1;
}
static uint64_t device_timestamp(const void *object,const HyCounterData *data) {
    if (!object || !data) hy_fail("invalid timestamp conversion");
    // The Metal adapter's counter writer must normalize GPU samples to ns.
    // This reads existing data; it never substitutes a CPU clock for a GPU sample.
    return data->timestamp;
}
static void set_depth(void *object, int mode) {
    (void)object;
    if (mode != 1) hy_fail("unsupported NVN depth mode: %d", mode);
    depth_mode = mode;
    fputs("NVN depth mode: zero to one\n", stderr);
}
static void set_origin(void *object, int mode) {
    (void)object;
    if (mode != 1) hy_fail("unsupported NVN window origin: %d", mode);
    origin_mode = mode;
    fputs("NVN window origin: upper left\n", stderr);
}
static void integer(const void *object, unsigned query, int *out) {
    (void)object;
    if (query == 0xf || query == 0x10) { *out = sizeof(Descriptor); return; }
    if (query == 0x11 || query == 0x12) { *out=256; return; } // Preserve the title SDK reserved descriptor range.
    if (query == 0x24 || query == 0x25) { *out = DESCRIPTOR_LIMIT; return; }
    // Metal queues allocate command/control/compute storage internally.
    if (query == 0x51 || query == 0x52 || query == 0x54 || query == 0x55 ||
        query == 0x56 || query == 0x5b || query == 0x5c) { *out = 0; return; }
    if (query == 0x50 || query == 0x53 || query == 0x5d) { *out = 8; return; }
    if (query == 0x13 || query == 0x14) { *out = 8; return; } // Host command words/pointers.
    if (query == 0x15) { *out = 32; return; }
    if (query == 0x16) { *out = 16; return; }
    // Guest spill storage is an allocator reservation; Metal manages shader spills.
    if (query == 0x19 || query == 0x1a) { *out=256; return; }
    if (query == 0x17 || query == 0x18) { *out=1; return; }
    // Only API version queries are delegated to the shipped SDK. Hardware limits
    // must be supplied by an implemented Metal path, not copied from another game.
    if (query > 1) hy_fail("NVN device capability not implemented: %#x", query);
    void *(*bootstrap)(const char *) = hy_symbol("nvnBootstrapLoader");
    if (!bootstrap) hy_fail("NVN bootstrap export missing");
    void (*original)(const void *, unsigned, int *) = bootstrap("nvnDeviceGetInteger");
    if (!original) hy_fail("NVN version query unavailable");
    original(NULL, query, out);
    fprintf(stderr, "NVN SDK version field %u: %d\n", query, *out);
}
static void pool_defaults(PoolBuilder *b) { memset(b, 0, sizeof *b); }
static void pool_device(PoolBuilder *b, void *d) { b->device = d; }
static void pool_storage(PoolBuilder *b, void *data, size_t size) { b->storage = data; b->size = size; }
static void pool_flags(PoolBuilder *b, uint32_t flags) { b->flags = flags; }
static uint32_t pool_initialize(Pool **object, const PoolBuilder *b) {
    if (!device || !b->device || !b->storage || !b->size || (b->flags & 0x300))
        hy_fail("unsupported NVN memory pool storage/flags: %#x", b->flags);
    uintptr_t base = (uintptr_t)b->storage, low = base & ~(uintptr_t)0x3fff;
    if (base > UINTPTR_MAX - 0x3fff || b->size > UINTPTR_MAX - base - 0x3fff)
        hy_fail("NVN pool address overflow");
    size_t length = ((base + b->size + 0x3fff) & ~(uintptr_t)0x3fff) - low;
    @autoreleasepool {
        id<MTLBuffer> buffer = [device newBufferWithBytesNoCopy:(void *)low length:length
            options:MTLResourceStorageModeShared deallocator:nil];
        if (!buffer) return 0;
        Pool *p = calloc(1, sizeof *p);
        if (!p) return 0;
        p->storage = b->storage; p->size = b->size; p->flags = b->flags; p->offset = base - low;
        p->buffer = (__bridge_retained void *)buffer;
        pthread_mutex_init(&p->lock, NULL);
        pthread_mutex_lock(&pools_lock); p->next=pools; pools=p; pthread_mutex_unlock(&pools_lock);
        *object = p;
        fprintf(stderr, "NVN Metal memory pool: %zu bytes flags=%#x\n", p->size, p->flags);
    }
    return 1;
}
// Resource leases belong to recorded bytes, not the reusable command-builder object.
// Release any old lease touched by a new record, including partial overwrites.
@protocol HyPoolReference
- (BOOL)usesPool:(Pool *)pool;
@end
@interface HyCommandLease : NSObject {
@public uintptr_t begin, end; uint32_t op; id resource;
}
@end
@implementation HyCommandLease
@end
static NSMutableArray<HyCommandLease *> *command_leases;
static pthread_mutex_t command_lock=PTHREAD_MUTEX_INITIALIZER;
static void command_overwrite(void *address,size_t size) {
    uintptr_t begin=(uintptr_t)address;
    if (size>UINTPTR_MAX-begin) hy_fail("command range overflow");
    uintptr_t end=begin+size;
    pthread_mutex_lock(&command_lock);
    for (NSUInteger i=command_leases.count;i>0;i--) {
        HyCommandLease *lease=command_leases[i-1];
        if (begin<lease->end && lease->begin<end) [command_leases removeObjectAtIndex:i-1];
    }
    pthread_mutex_unlock(&command_lock);
}
static void command_release_pool(Pool *pool) {
    uintptr_t begin=(uintptr_t)pool->storage,end=begin+pool->size;
    pthread_mutex_lock(&command_lock);
    for (NSUInteger i=command_leases.count;i>0;i--) {
        HyCommandLease *lease=command_leases[i-1];
        BOOL dependency=[lease->resource respondsToSelector:@selector(usesPool:)] &&
            [(id<HyPoolReference>)lease->resource usesPool:pool];
        if (dependency || (begin<lease->end && lease->begin<end)) [command_leases removeObjectAtIndex:i-1];
    }
    pthread_mutex_unlock(&command_lock);
}
static void command_keep(void *payload,size_t size,id resource) {
    HyCommandLease *lease=[HyCommandLease new];
    lease->begin=(uintptr_t)payload-8;lease->end=lease->begin+((size+15)&~(size_t)7);lease->resource=resource;
    memcpy(&lease->op,(const uint8_t *)payload-8,sizeof lease->op);
    pthread_mutex_lock(&command_lock);
    if (!command_leases) command_leases=[NSMutableArray new];
    [command_leases addObject:lease];pthread_mutex_unlock(&command_lock);
}
static id command_resource(uint64_t address,const void *token,size_t size,uint32_t op) {
    id resource=nil;pthread_mutex_lock(&command_lock);
    for (HyCommandLease *lease in command_leases) if (lease->begin==address &&
        size==lease->end-lease->begin && op==lease->op &&
        (__bridge void *)lease->resource==token) { resource=lease->resource;break; }
    pthread_mutex_unlock(&command_lock);
    if (!resource) hy_fail("recorded command resource was overwritten, altered or released");
    return resource;
}
static uint32_t command_initialize(HyCommands *c,void *d) {
    uint32_t result=hy_commands_init(c,d);c->overwrite=command_overwrite;return result;
}
static void pool_finalize(Pool **object) {
    Pool *p = *object;
    if (!p) hy_fail("invalid NVN memory pool finalize");
    command_release_pool(p);
    replay_release_pool(p);
    if (p->textures || p->buffers) hy_fail("memory pool finalized with live resources");
    pthread_mutex_lock(&pools_lock);
    Pool **slot=&pools; while (*slot && *slot!=p) slot=&(*slot)->next;
    if (!*slot) hy_fail("unregistered memory pool finalize");
    *slot=p->next;
    pthread_mutex_unlock(&pools_lock);
    @autoreleasepool {
        id<MTLCommandBuffer> fence = [queue commandBuffer]; [fence commit]; [fence waitUntilCompleted];
        id buffer = (__bridge_transfer id)p->buffer; (void)buffer;
    }
    pthread_mutex_destroy(&p->lock); free(p); *object = NULL;
}
static void *pool_map(Pool *const *object) { return ((*object)->flags & 1) ? NULL : (*object)->storage; }
static uint64_t pool_address(Pool *const *object) { return (uintptr_t)(*object)->storage; }
static size_t pool_size(Pool *const *object) { return (*object)->size; }
static uint32_t pool_get_flags(Pool *const *object) { return (*object)->flags; }
static void refresh_linear_pool(Pool *pool,size_t offset,size_t size);
static void pool_flush(Pool *const *object, ptrdiff_t offset, size_t size) {
    Pool *p = *object;
    if (offset < 0 || (size_t)offset > p->size || size > p->size - (size_t)offset) hy_fail("NVN pool flush out of bounds");
    sys_dcache_flush((uint8_t *)p->storage + offset, size);
    refresh_linear_pool(p,(size_t)offset,size);
}
static NSData *pool_copy(uint64_t address,size_t size) {
    NSData *result=nil;
    pthread_mutex_lock(&pools_lock);
    for (Pool *p=pools;p;p=p->next) {
        uint64_t base=(uintptr_t)p->storage;
        if (address>=base && address-base<=p->size && size<=p->size-(address-base)) {
            result=[NSData dataWithBytes:(const void *)(uintptr_t)address length:size]; break;
        }
    }
    pthread_mutex_unlock(&pools_lock);
    if (!result) hy_fail("GPU address outside live memory pools");
    return result;
}
typedef struct { uint64_t address; const void *control; } HyShaderData;
@interface HyShader : NSObject {
@public
    NSData *control, *code, *constants;
    uint32_t stage;
    char key[65];
}
@end
@implementation HyShader
@end
static NSString *shader_digest(NSData *data) {
    if(!data || data.length>UINT32_MAX) hy_fail("invalid shader hash input");
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];char hex[65];
    CC_SHA256(data.bytes,(CC_LONG)data.length,digest);
    for(unsigned i=0;i<sizeof digest;i++) snprintf(hex+2*i,3,"%02x",digest[i]);
    return @(hex);
}
// Returns a function from the exact library bytes whose hash was checked.
id<MTLFunction> hy_metal_shader(HyShader *shader,NSString *root,int alpha,float reference,NSDictionary **reflection) {
    if(!device || !root.length || !shader || (shader->stage!=1 && shader->stage!=5) ||
       (alpha!=8 && alpha!=5) || reference!=0 || (shader->stage==1 && alpha!=8))
        hy_fail("unsupported Metal shader specialization");
    HyShaderLayout layout;
    if(!hy_shader_layout(shader->control.bytes,shader->control.length,&layout) || layout.stage!=shader->stage ||
       shader->code.length!=layout.code_size || shader->constants.length!=layout.constant_size ||
       ![shader_digest(shader->control) isEqual:@(shader->key)]) hy_fail("shader snapshot identity mismatch");
    NSString *directory=alpha==5 ? [root stringByAppendingPathComponent:@"alpha5-ref0"] : root;
    NSString *base=[directory stringByAppendingPathComponent:@(shader->key)];
    NSData *json=[NSData dataWithContentsOfFile:[base stringByAppendingString:@".json"]];NSError *error=nil;
    id parsed=json ? [NSJSONSerialization JSONObjectWithData:json options:0 error:&error] : nil;
    if(![parsed isKindOfClass:NSDictionary.class]) hy_fail("Metal shader metadata unavailable or malformed: %s",base.fileSystemRepresentation);
    NSDictionary *metadata=parsed;
    if(![metadata[@"Profile"] isEqual:@"float-2d-upper-left-zero-one-v1"] ||
       ![metadata[@"Stage"] isEqual:@(shader->stage)] || ![metadata[@"AlphaTestFunction"] isEqual:@(alpha)] ||
       ![metadata[@"AlphaTestReference"] isEqual:@(reference)] ||
       ![metadata[@"CodeSha256"] isEqual:shader_digest(shader->code)] ||
       ![metadata[@"ConstantsSha256"] isEqual:shader_digest(shader->constants)])
        hy_fail("Metal shader source/profile mismatch");
#if TARGET_OS_IPHONE
    NSString *sdk=@"iphoneos";
#else
    NSString *sdk=@"macosx";
#endif
    NSString *path=[base stringByAppendingFormat:@".%@.metallib",sdk];
    NSData *binary=[NSData dataWithContentsOfFile:path];id hashes=metadata[@"MetalLibraries"];
    if(!binary.length || ![hashes isKindOfClass:NSDictionary.class] ||
       ![hashes[sdk] isEqual:shader_digest(binary)]) hy_fail("Metal library hash mismatch");
    dispatch_data_t bytes=dispatch_data_create(binary.bytes,binary.length,
        dispatch_get_global_queue(QOS_CLASS_DEFAULT,0),^{(void)binary;});
    id<MTLLibrary> library=[device newLibraryWithData:bytes error:&error];
    if(!library) hy_fail("Metal library load failed: %s",error.localizedDescription.UTF8String);
    id<MTLFunction> function=[library newFunctionWithName:@"main0"];
    if(!function || function.functionType!=(shader->stage==1 ? MTLFunctionTypeVertex : MTLFunctionTypeFragment))
        hy_fail("Metal library entrypoint/stage mismatch");
    if(reflection) *reflection=metadata;
    return function;
}
static uint32_t program_initialize(void **object,const void *d) {
    if (!device || !d) hy_fail("program device missing");
    *object=(__bridge_retained void *)[NSArray array]; return 1;
}
static void program_finalize(void **object) {
    id old=(__bridge_transfer id)*object; (void)old; *object=NULL;
}
static void program_label(void *const *object,const char *label) {
    if (!*object) hy_fail("label on finalized program");
    fprintf(stderr,"NVN program label: %.120s\n",label ? label : "");
}
static uint32_t program_shaders(void **object,int count,const HyShaderData *inputs) {
    if (!*object || count<1 || count>6 || !inputs) hy_fail("invalid program shader list");
    NSMutableArray *captured=[NSMutableArray arrayWithCapacity:count]; unsigned stages=0;
    for (int i=0;i<count;i++) {
        HyShader *shader=[HyShader new]; uintptr_t address=(uintptr_t)inputs[i].control;
        for (size_t j=0;j<hy_module_count;j++) {
            HyModule *m=&hy_modules[j]; uintptr_t base=(uintptr_t)m->base;
            if (address>=base && address-base<=m->size && 0x880<=m->size-(address-base)) {
                shader->control=[NSData dataWithBytes:inputs[i].control length:0x880]; break;
            }
        }
        if (!shader->control) shader->control=pool_copy(address,0x880);
        HyShaderLayout layout;
        if (!hy_shader_layout(shader->control.bytes,shader->control.length,&layout)) hy_fail("unsupported shader control layout");
        if (stages&(1u<<layout.stage)) hy_fail("duplicate program shader stage");
        stages|=1u<<layout.stage;
        NSData *blob=pool_copy(inputs[i].address,layout.extent);
        uint32_t sph; memcpy(&sph,(const uint8_t *)blob.bytes+layout.prefix,4);
        if (((sph>>10)&15)!=layout.stage) hy_fail("shader control and SPH stages disagree");
        shader->stage=layout.stage;
        shader->code=[blob subdataWithRange:NSMakeRange(layout.prefix,layout.code_size)];
        shader->constants=[blob subdataWithRange:NSMakeRange(layout.constant_offset,layout.constant_size)];
        uint8_t digest[CC_SHA256_DIGEST_LENGTH]; CC_SHA256(shader->control.bytes,(CC_LONG)shader->control.length,digest);
        for (unsigned k=0;k<sizeof digest;k++) snprintf(shader->key+2*k,3,"%02x",digest[k]);
        fprintf(stderr,"NVN shader: %s SPH-stage=%u\n",shader->key,shader->stage);
        [captured addObject:shader];
    }
    NSArray *snapshot=[captured copy];
    program_finalize(object); *object=(__bridge_retained void *)snapshot;
    fprintf(stderr,"NVN program captured: %d immutable shader stages\n",count);
    return 1;
}
typedef struct { void *device; Pool **pool; ptrdiff_t offset; size_t size; } BufferBuilder;
typedef struct { Pool **handle; Pool *pool; size_t offset, size; } Buffer;
_Static_assert(sizeof(BufferBuilder)<=64 && sizeof(Buffer)<=48,"NVN buffer ABI sizes");
static void buffer_defaults(BufferBuilder *b) { void *d=b->device; memset(b,0,sizeof *b); b->device=d; }
static void buffer_device(BufferBuilder *b, void *d) { b->device=d; }
static void buffer_storage(BufferBuilder *b, Pool **pool, ptrdiff_t offset, size_t size) {
    b->pool=pool; b->offset=offset; b->size=size;
}
static uint32_t buffer_initialize(Buffer *b, const BufferBuilder *builder) {
    if (!builder->device || !builder->pool || !*builder->pool) hy_fail("buffer device or pool missing");
    Pool *pool=*builder->pool;
    if (builder->offset<0 || !builder->size || (size_t)builder->offset>pool->size ||
        builder->size>pool->size-(size_t)builder->offset) hy_fail("buffer storage outside pool");
    *b=(Buffer){builder->pool,pool,(size_t)builder->offset,builder->size};
    pthread_mutex_lock(&pool->lock); pool->buffers++; pthread_mutex_unlock(&pool->lock);
    return 1;
}
static void buffer_finalize(Buffer *b) {
    if (!b->pool) hy_fail("invalid buffer finalize");
    pthread_mutex_lock(&b->pool->lock); b->pool->buffers--; pthread_mutex_unlock(&b->pool->lock);
    memset(b,0,sizeof *b);
}
static void *buffer_map(const Buffer *b) { return (b->pool->flags&1) ? NULL : (uint8_t *)b->pool->storage+b->offset; }
static uint64_t buffer_address(const Buffer *b) { return (uintptr_t)b->pool->storage+b->offset; }
static size_t buffer_size(const Buffer *b) { return b->size; }
static ptrdiff_t buffer_offset(const Buffer *b) { return b->offset; }
static Pool **buffer_pool(const Buffer *b) { return b->handle; }
static void buffer_flush(const Buffer *b, ptrdiff_t offset, size_t size) {
    if (offset<0 || (size_t)offset>b->size || size>b->size-(size_t)offset) hy_fail("buffer flush out of range");
    pool_flush(&b->pool,(ptrdiff_t)b->offset+offset,size);
}
static uint32_t descriptor_initialize(DescriptorPool *d, Pool *const *pool, ptrdiff_t offset, int count) {
    Pool *p = *pool;
    if (offset < 0 || (offset & 7) || count <= 0 || count > DESCRIPTOR_LIMIT || (size_t)offset > p->size ||
        (size_t)count > (p->size - (size_t)offset) / sizeof(Descriptor))
        hy_fail("NVN descriptor pool out of bounds");
    *d = (DescriptorPool){p, (size_t)offset, count};
    memset((uint8_t *)p->storage + offset, 0, (size_t)count * sizeof(Descriptor));
    return 1;
}
static void queue_defaults(QueueBuilder *b) {
    // NVN defaults preserve the separately assigned device.
    void *d = b->device; memset(b, 0, sizeof *b); b->device = d;
}
static void queue_device(QueueBuilder *b, void *d) { b->device = d; }
static void queue_flags(QueueBuilder *b, uint32_t flags) { b->flags = flags; }
static void queue_command_size(QueueBuilder *b, size_t size) { b->command_size = size; }
static void queue_compute_size(QueueBuilder *b, size_t size) { b->compute_size = size; }
static void queue_control_size(QueueBuilder *b, size_t size) { b->control_size = size; }
static void queue_threshold(QueueBuilder *b, size_t size) { b->flush_threshold = size; }
static size_t queue_memory_size(const QueueBuilder *b) { (void)b; return 0; }
static void queue_memory(QueueBuilder *b, void *memory, size_t size) { b->memory = memory; b->memory_size = size; }
static uint32_t queue_initialize(void **object, const QueueBuilder *b) {
    if (!device || !b->device) hy_fail("NVN queue device missing");
    id<MTLCommandQueue> q = [device newCommandQueue];
    if (!q) return 0;
    *object = (__bridge_retained void *)q;
    fputs("NVN Metal command queue initialized\n", stderr);
    return 1;
}
static void queue_finish(void *const *object) {
    graphics_wait_active();
    id<MTLCommandQueue> q = (__bridge id<MTLCommandQueue>)*object;
    id<MTLCommandBuffer> fence = [q commandBuffer];
    [fence commit]; [fence waitUntilCompleted];
    if (fence.status != MTLCommandBufferStatusCompleted) hy_fail("Metal queue finish failed");
}
static void queue_finalize(void **object) {
    queue_finish(object);
    replay_release_queue(*object);
    id q = (__bridge_transfer id)*object; (void)q; *object = NULL;
}
// The guest sync object stores one host pointer. Metal owns submitted command buffers.
typedef struct { pthread_mutex_t lock; void *command, *event; unsigned references; } GpuSync;
static uint64_t monotonic_ns(void);
static uint32_t sync_initialize(GpuSync **object, void *d) {
    if (!device || !d) hy_fail("sync device missing");
    GpuSync *sync=calloc(1,sizeof *sync); if (!sync) return 0;
    pthread_mutex_init(&sync->lock,NULL);sync->references=1; *object=sync; return 1;
}
static void sync_release(GpuSync *sync) {
    pthread_mutex_lock(&sync->lock);
    if (!sync->references) hy_fail("invalid sync reference count");
    if (--sync->references) { pthread_mutex_unlock(&sync->lock);return; }
    id command=(__bridge_transfer id)sync->command; (void)command;
    id event=(__bridge_transfer id)sync->event; (void)event;
    pthread_mutex_unlock(&sync->lock);pthread_mutex_destroy(&sync->lock);free(sync);
}
static void sync_finalize(GpuSync **object) {
    GpuSync *sync=*object;if (!sync) hy_fail("invalid sync finalize");
    *object=NULL;sync_release(sync);
}
@interface HyRecordedSync : NSObject {
@public GpuSync *sync; int condition,flags;
}
@end
@implementation HyRecordedSync
- (void)dealloc { if (sync) sync_release(sync); }
@end
static void command_fence(HyCommands *c,GpuSync *const *object,int condition,int flags) {
    if (!object || !*object || condition<0 || condition>1 || (flags&~1)) hy_fail("invalid recorded fence");
    HyRecordedSync *fence=[HyRecordedSync new];fence->sync=*object;fence->condition=condition;fence->flags=flags;
    pthread_mutex_lock(&fence->sync->lock);fence->sync->references++;pthread_mutex_unlock(&fence->sync->lock);
    void *value=(__bridge void *)fence;void *record=hy_commands_record(c,HY_FENCE_SYNC,sizeof value);
    memcpy(record,&value,sizeof value);command_keep(record,sizeof value,fence);
}
static void queue_fence(void *const *object, GpuSync **sync_object, int condition, int flags) {
    graphics_wait_active();
    if (condition<0 || condition>1 || (flags&~1)) hy_fail("unsupported sync condition/flags");
    GpuSync *sync=*sync_object; if (!sync) hy_fail("invalid queue fence sync");
    id<MTLCommandBuffer> command=[(__bridge id<MTLCommandQueue>)*object commandBuffer];
    if (!command) hy_fail("Metal fence allocation failed");
    id<MTLSharedEvent> event=[device newSharedEvent];
    if (!event) hy_fail("Metal sync event allocation failed");
    // Each fence has a distinct event, so a later fence cannot retarget queued waits.
    // Waiting for all commands is stronger than the world-space-only condition.
    [command encodeSignalEvent:event value:1]; [command commit];
    pthread_mutex_lock(&sync->lock);
    id previous=(__bridge_transfer id)sync->command; (void)previous;
    id previous_event=(__bridge_transfer id)sync->event; (void)previous_event;
    sync->command=(__bridge_retained void *)command;
    sync->event=(__bridge_retained void *)event;
    pthread_mutex_unlock(&sync->lock);
}
static int sync_wait(GpuSync *const *object, uint64_t timeout) {
    GpuSync *sync=*object; if (!sync) return 3;
    pthread_mutex_lock(&sync->lock);
    id<MTLCommandBuffer> command=(__bridge id<MTLCommandBuffer>)sync->command;
    pthread_mutex_unlock(&sync->lock);
    if (!command || command.status==MTLCommandBufferStatusError) return 3;
    if (command.status==MTLCommandBufferStatusCompleted) return 0;
    uint64_t start=monotonic_ns();
    while (command.status!=MTLCommandBufferStatusCompleted) {
        if (command.status==MTLCommandBufferStatusError) return 3;
        uint64_t elapsed=monotonic_ns()-start;
        if (elapsed>=timeout) return 2;
        uint64_t delay=timeout-elapsed; if (delay>1000000) delay=1000000;
        struct timespec pause={0,(long)delay}; nanosleep(&pause,NULL);
    }
    return 1;
}
static uint32_t queue_wait(void *const *object, GpuSync *const *sync) {
    GpuSync *state=*sync; if (!state) return 0;
    pthread_mutex_lock(&state->lock);
    id<MTLSharedEvent> event=(__bridge id<MTLSharedEvent>)state->event;
    pthread_mutex_unlock(&state->lock);
    if (!event) return 0;
    id<MTLCommandBuffer> wait=[(__bridge id<MTLCommandQueue>)*object commandBuffer];
    if (!wait) return 0;
    [wait encodeWaitForEvent:event value:1]; [wait commit];
    return 1;
}
static void command_memory(HyCommands *c, Pool *const *object, ptrdiff_t offset, size_t size) {
    Pool *p = *object;
    if (offset < 0 || (size_t)offset > p->size || size > p->size - (size_t)offset)
        hy_fail("command memory outside pool");
    hy_commands_memory(c, (uint8_t *)p->storage + offset, size);
}
@interface HyBoundBuffer : NSObject <HyPoolReference> {
@public Pool *pool; id<MTLBuffer> buffer; size_t offset,length; int stage,index;
}
@end
@implementation HyBoundBuffer
- (BOOL)usesPool:(Pool *)other { return pool==other; }
- (void)dealloc {
    if (pool) { pthread_mutex_lock(&pool->lock);pool->buffers--;pthread_mutex_unlock(&pool->lock); }
}
@end
static HyBoundBuffer *bound_buffer(int stage,int index,uint64_t address,size_t length) {
    HyBoundBuffer *bound=[HyBoundBuffer new];bound->stage=stage;bound->index=index;bound->length=length;
    if (length) {
        pthread_mutex_lock(&pools_lock);
        for (Pool *p=pools;p;p=p->next) {
            uint64_t base=(uintptr_t)p->storage;
            if (address>=base && address-base<=p->size && length<=p->size-(address-base)) {
                bound->pool=p;bound->buffer=(__bridge id<MTLBuffer>)p->buffer;
                bound->offset=p->offset+(size_t)(address-base);
                pthread_mutex_lock(&p->lock);p->buffers++;pthread_mutex_unlock(&p->lock);break;
            }
        }
        pthread_mutex_unlock(&pools_lock);
        if (!bound->pool) hy_fail("%s buffer outside live memory pools",stage==-1 ? "vertex" : stage==-2 ? "index" : "uniform");
    }
    return bound;
}
static void command_bind_buffer(HyCommands *c,uint32_t opcode,int stage,int index,uint64_t address,size_t length) {
    HyBoundBuffer *bound=bound_buffer(stage,index,address,length);
    void *value=(__bridge void *)bound;void *record=hy_commands_record(c,opcode,sizeof value);
    memcpy(record,&value,sizeof value);command_keep(record,sizeof value,bound);
}
static void command_uniform(HyCommands *c,int stage,int index,uint64_t address,size_t length) {
    if (stage<0 || stage>5 || index<0 || index>=32) hy_fail("unsupported uniform stage/index");
    command_bind_buffer(c,HY_UNIFORM_BUFFER,stage,index,address,length);
}
typedef struct { uint64_t address,size; } HyGpuBufferRange;
static void command_vertex_buffers(HyCommands *c,int first,int count,const HyGpuBufferRange *ranges) {
    if (first<0 || first>16 || count<0 || count>16-first || (count && !ranges)) hy_fail("unsupported vertex buffer range");
    for (int i=0;i<count;i++) command_bind_buffer(c,HY_VERTEX_BUFFER,-1,first+i,ranges[i].address,ranges[i].size);
}
@interface HyDrawIndexed : NSObject <HyPoolReference> {
@public HyBoundBuffer *indices; int primitive,index_type,count,base_vertex;
}
@end
@implementation HyDrawIndexed
- (BOOL)usesPool:(Pool *)pool { return [indices usesPool:pool]; }
@end
static void command_draw_elements_base(HyCommands *c,int primitive,int type,int count,uint64_t address,int base) {
    if (primitive<0 || primitive>14 || type<0 || type>2 || count<0 || (count && (address&((1u<<type)-1))))
        hy_fail("unsupported indexed draw parameters");
    HyDrawIndexed *draw=[HyDrawIndexed new];draw->primitive=primitive;draw->index_type=type;draw->count=count;draw->base_vertex=base;
    draw->indices=bound_buffer(-2,0,address,(size_t)count<<type);
    void *value=(__bridge void *)draw;void *record=hy_commands_record(c,HY_DRAW_ELEMENTS,sizeof value);
    memcpy(record,&value,sizeof value);command_keep(record,sizeof value,draw);
}
static void command_draw_elements(HyCommands *c,int primitive,int type,int count,uint64_t address) {
    command_draw_elements_base(c,primitive,type,count,address,0);
}
static void command_program(HyCommands *c,void *const *object,uint32_t stages) {
    if ((stages&~63u) || (object && !*object)) hy_fail("invalid bound program/stages");
    NSArray *shaders=object ? (__bridge NSArray *)*object : @[];
    HyProgramRecord *record=hy_commands_record(c,HY_BIND_PROGRAM,sizeof *record);
    *record=(HyProgramRecord){(__bridge void *)shaders,stages};
    command_keep(record,sizeof *record,shaders);
}
static void command_counter(HyCommands *c, int type, uint64_t address) {
    if (type!=0 && type!=16) hy_fail("counter type not implemented: %d",type);
    if (address&7) hy_fail("unaligned counter destination");
    HyCounterRecord *record=hy_commands_record(c,HY_REPORT_COUNTER,sizeof *record);
    *record=(HyCounterRecord){address,type};
}
static void command_texture_pool(HyCommands *c, const DescriptorPool *pool) {
    if (!pool->pool || pool->count<1) hy_fail("invalid recorded texture pool");
    memcpy(hy_commands_record(c,HY_TEXTURE_POOL,sizeof *pool),pool,sizeof *pool);
}
static void command_sampler_pool(HyCommands *c, const DescriptorPool *pool) {
    if (!pool->pool || pool->count<1) hy_fail("invalid recorded sampler pool");
    memcpy(hy_commands_record(c,HY_SAMPLER_POOL,sizeof *pool),pool,sizeof *pool);
}
#define STATE_COMMAND(name,opcode,bytes) \
    static void name(HyCommands *c,const void *state) { memcpy(hy_commands_record(c,opcode,bytes),state,bytes); }
STATE_COMMAND(command_multisample,HY_MULTISAMPLE_STATE,24)
STATE_COMMAND(command_color,HY_COLOR_STATE,4)
STATE_COMMAND(command_blend,HY_BLEND_STATE,8)
STATE_COMMAND(command_channels,HY_CHANNEL_MASK_STATE,4)
STATE_COMMAND(command_polygon,HY_POLYGON_STATE,4)
STATE_COMMAND(command_depth_stencil,HY_DEPTH_STENCIL_STATE,8)
#undef STATE_COMMAND
static void texture_device(TextureBuilder *b, void *d) { b->device = d; }
static void texture_defaults(TextureBuilder *b) {
    void *d = b->device; memset(b, 0, sizeof *b); b->device = d;
    b->target = 1; b->width = b->height = b->depth = b->levels = 1;
}
static void texture_swizzle(TextureBuilder *builder,int r,int g,int b,int a) {
    int channels[]={r,g,b,a};for(unsigned i=0;i<4;i++){if(channels[i]<0 || channels[i]>5)hy_fail("invalid texture builder swizzle");builder->swizzle[i]=(uint8_t)channels[i];}builder->has_swizzle=1;
}
static void texture_flags(TextureBuilder *b, int v) { b->flags = v; }
static void texture_target(TextureBuilder *b, int v) { b->target = v; }
static void texture_width(TextureBuilder *b, int v) { b->width = v; }
static void texture_height(TextureBuilder *b, int v) { b->height = v; }
static void texture_depth(TextureBuilder *b, int v) { b->depth = v; }
static void texture_levels(TextureBuilder *b, int v) { b->levels = v; }
static void texture_format(TextureBuilder *b, int v) { b->format = v; }
static void texture_samples(TextureBuilder *b, int v) { b->samples = v; }
static void texture_depth_mode(TextureBuilder *b, int mode) {
    (void)b; // Metal depth textures expose depth by default; stencil sampling needs a separate view.
    if (mode!=0) hy_fail("texture-builder stencil sampling is not implemented");
}
static void texture_size2d(TextureBuilder *b, int w, int h) { b->width = w; b->height = h; b->depth = 1; }
static void texture_size3d(TextureBuilder *b,int w,int h,int depth) { b->width=w; b->height=h; b->depth=depth; }
static void texture_storage(TextureBuilder *b, Pool **p, ptrdiff_t offset) { b->pool = p; b->offset = offset; }
static void texture_stride(TextureBuilder *b, ptrdiff_t stride) { b->stride = stride; }
static void texture_layout(TextureBuilder *b, const void *layout) {
    b->has_layout = layout != NULL;
    b->layout=0;
    if (layout) { memcpy(&b->layout,layout,2); b->layout&=0xfff; }
}
static uint32_t texture_get_layout(const TextureBuilder *b,uint32_t *out) { *out=b->has_layout ? b->layout : 0;return b->has_layout; }
static int texture_get_flags(const TextureBuilder *b) { return b->flags; }
static int texture_get_target(const TextureBuilder *b) { return b->target; }
static int texture_get_width(const TextureBuilder *b) { return b->width; }
static int texture_get_height(const TextureBuilder *b) { return b->height; }
static int texture_get_depth(const TextureBuilder *b) { return b->depth; }
static int texture_get_levels(const TextureBuilder *b) { return b->levels; }
static int texture_get_format(const TextureBuilder *b) { return b->format; }
static int texture_get_samples(const TextureBuilder *b) { return b->samples; }
static nvn_texdesc texture_description(const TextureBuilder *b) {
    int bytes_per_block=nvn_fmt_bpb(b->format,NULL,NULL);
    if (b->target != 1 || b->width < 1 || b->width > 16384 || b->height < 1 || b->height > 16384 ||
        b->depth != 1 || b->levels < 1 || b->levels > 15 || b->samples > 1 || b->samples < 0 ||
        (b->flags & ~0x39) || ((b->flags&0x20) && (b->levels!=1 ||
        (bytes_per_block!=1 && bytes_per_block!=2 && bytes_per_block!=4 && bytes_per_block!=8 && bytes_per_block!=16))) ||
        (b->has_layout && ((b->layout&15)>5 || (b->layout&0xf0) || (b->layout>>8)>5 ||
        ((b->layout>>8) && b->levels>1))) || !bytes_per_block)
        hy_fail("unsupported texture: %dx%dx%d target=%d format=%#x flags=%#x levels=%d samples=%d packaged=%d layout=%#x",
            b->width,b->height,b->depth,b->target,b->format,b->flags,b->levels,b->samples,b->has_layout,
            b->layout);
    if((b->flags&0x10) && ((b->format!=1 && b->format!=0xd) || b->levels!=1 || b->has_layout || (b->flags&0x20) || b->stride<0 || (uint64_t)b->stride>UINT32_MAX || (b->stride && (b->stride<(ptrdiff_t)b->width*bytes_per_block || (b->stride&31)))))hy_fail("unsupported linear texture layout: %dx%d format=%#x flags=%#x stride=%td levels=%d packaged=%d",b->width,b->height,b->format,b->flags,b->stride,b->levels,b->has_layout);
    return (nvn_texdesc){.stride=(uint32_t)b->stride,.target=b->target,.format=b->format,.flags=b->flags,.samples=b->samples,
        .width=b->width,.height=b->height,.depth=b->depth,.levels=b->levels,
        .pkg_block_h=b->has_layout ? (b->layout&15)+1 : 0,
        .pkg_block_w=b->has_layout ? (b->layout>>8)+1 : 0};
}
static size_t texture_size(const TextureBuilder *b) {
    nvn_texdesc d = texture_description(b); nvn_texlayout layout; nvn_tex_layout(&d, &layout); return layout.size;
}
static size_t texture_alignment(const TextureBuilder *b) {
    nvn_texdesc d=texture_description(b); nvn_texlayout layout; nvn_tex_layout(&d,&layout); return layout.align;
}
static void texture_sparse_tiles(const TextureBuilder *b,uint32_t *out) {
    memset(out,0,16);
    nvn_texdesc d=texture_description(b);
    if (!(b->flags&0x20)) return;
    d.pkg_block_h=d.pkg_block_w=0; // The SDK's sparse query ignores packaged overrides.
    nvn_texlayout layout;nvn_tex_layout(&d,&layout);
    if (layout.tileW) {
        int block_width,block_height;nvn_fmt_bpb(b->format,&block_width,&block_height);
        out[0]=1;out[1]=(64u<<layout.tileW)/layout.bpb*block_width;
        out[2]=(8u<<layout.tileH)*block_height;out[3]=1;
    }
}
static MTLPixelFormat texture_metal_format(int format) {
    switch (format) {
    case 0x1: return MTLPixelFormatR8Unorm;
    case 0xd: return MTLPixelFormatRG8Unorm;
    case 0x25: return MTLPixelFormatRGBA8Unorm;
    case 0x3d: return MTLPixelFormatRGB10A2Unorm;
    case 0x3f: return MTLPixelFormatRG11B10Float;
    case 0x4d: return MTLPixelFormatRGBA8Unorm; // BC7 is decoded for portable iPhone support.
    case 0x4e: return MTLPixelFormatRGBA8Unorm_sRGB;
    case 0x38: return MTLPixelFormatRGBA8Unorm_sRGB;
    case 0x34: return MTLPixelFormatDepth32Float;
    case 0x36: return MTLPixelFormatDepth32Float_Stencil8;
    default: hy_fail("unimplemented Metal texture format: %#x", format);
    }
}
static uint32_t texture_initialize(Texture *t, const TextureBuilder *b) {
    nvn_texdesc d = texture_description(b);
    nvn_texlayout layout; nvn_tex_layout(&d,&layout);
    size_t bytes = layout.size;
    if (!b->device || !b->pool || !*b->pool || b->offset < 0 ||
        (size_t)b->offset > (*b->pool)->size || bytes > (*b->pool)->size - (size_t)b->offset)
        hy_fail("texture storage outside memory pool");
    MTLPixelFormat format = texture_metal_format(b->format);
    Pool *pool = *b->pool;
    pthread_mutex_lock(&pool->lock);
    for (TextureBacking *backing=pool->textures;backing;backing=backing->next) {
        if ((size_t)b->offset >= backing->offset+backing->size || backing->offset >= (size_t)b->offset+bytes) continue;
        if ((size_t)b->offset!=backing->offset || bytes!=backing->size || b->width!=backing->width ||
            b->height!=backing->height || b->levels!=backing->levels || b->format!=backing->format || layout.tileH!=backing->tile_height || layout.tileW!=backing->tile_width || (b->flags&0x10)!=(backing->flags&0x10) || d.stride!=backing->stride)
            {
                // A live movie descriptor may outlast its CPU allocation. The title
                // reuses those bytes for a packaged, sampled-only BC7 image.
                // Keep each interpretation in a separate Metal image; linear reads
                // refresh from CPU storage when sampled. GPU-written aliases remain unsupported.
                if(!backing->objects || ((backing->flags&0x10) && (b->format==0x4d || b->format==0x4e)))continue;
                hy_fail("unsupported overlapping texture layouts: new=%dx%d/%#x old=%dx%d/%#x objects=%u refs=%u",b->width,b->height,b->format,backing->width,backing->height,backing->format,backing->objects,backing->references);}
        backing->references++;backing->objects++;
        *t=(Texture){.builder=*b,.image=backing->image,.backing=backing};
        pthread_mutex_unlock(&pool->lock);
        fputs("NVN texture alias shares existing Metal image\n",stderr);
        return 1;
    }
    MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
        width:b->width height:b->height mipmapped:b->levels > 1];
    desc.mipmapLevelCount = b->levels; desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget | MTLTextureUsagePixelFormatView;
    int bc7=b->format==0x4d || b->format==0x4e;
    if (b->flags&0x10)desc.usage=MTLTextureUsageShaderRead|MTLTextureUsagePixelFormatView;
    if (bc7) desc.usage=MTLTextureUsageShaderRead|MTLTextureUsagePixelFormatView;
    int depth = b->format == 0x34 || b->format == 0x36;
    desc.storageMode = depth ? MTLStorageModePrivate : MTLStorageModeShared;
    id<MTLTexture> image=[device newTextureWithDescriptor:desc];
    if (!image) { pthread_mutex_unlock(&pool->lock); return 0; }
    if (!depth && !(b->flags & (1|0x10))) {
        void *storage = (uint8_t *)(*b->pool)->storage + b->offset;
        for (int level = 0; level < b->levels; level++) {
            size_t w = b->width >> level, h = b->height >> level; if (!w) w=1; if (!h) h=1;
            int bw,bh;size_t block_bytes=nvn_fmt_bpb(b->format,&bw,&bh);
            size_t pitch=(w+bw-1)/bw*block_bytes,linear_size=pitch*((h+bh-1)/bh);
            void *linear = malloc(linear_size); if (!linear) hy_fail("texture staging allocation");
            if (nvn_deswizzle(&d, level, 0, storage, linear, pitch)) hy_fail("texture deswizzle");
            if (bc7) {
                void *pixels=malloc(w*h*4);if (!pixels) hy_fail("BC7 staging allocation");
                if (hy_bc7_decode(linear,linear_size,(uint32_t)w,(uint32_t)h,pixels,w*4,w*h*4)) hy_fail("BC7 decode bounds");
                [image replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:level withBytes:pixels bytesPerRow:w*4];
                free(pixels);
            } else {
                [image replaceRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:level withBytes:linear bytesPerRow:pitch];
            }
            free(linear);
        }
    }
    TextureBacking *backing=calloc(1,sizeof *backing);
    if (!backing) hy_fail("texture backing allocation");
    *backing=(TextureBacking){.next=pool->textures,.pool=pool,.offset=b->offset,.size=bytes,.tile_height=layout.tileH,.tile_width=layout.tileW,.flags=b->flags,.stride=d.stride,
        .width=b->width,.height=b->height,.levels=b->levels,.format=b->format,.references=1,.objects=1,
        .image=(__bridge_retained void *)image};
    pool->textures=backing;
    *t = (Texture){.builder=*b,.image=backing->image,.backing=backing};
    pthread_mutex_unlock(&pool->lock);
    fprintf(stderr, "NVN Metal texture: %dx%d format=%#x levels=%d bytes=%zu\n", b->width,b->height,b->format,b->levels,bytes);
    return 1;
}
static void refresh_linear_locked(TextureBacking *backing) {
    if(!(backing->flags&0x10))return;
    NSUInteger bpp=(NSUInteger)nvn_fmt_bpb(backing->format,NULL,NULL);
    NSUInteger pitch=backing->stride ? backing->stride : ((NSUInteger)backing->width*bpp+31)&~(NSUInteger)31;
    id<MTLTexture> image=(__bridge id<MTLTexture>)backing->image;
    [image replaceRegion:MTLRegionMake2D(0,0,(NSUInteger)backing->width,(NSUInteger)backing->height) mipmapLevel:0
        withBytes:(uint8_t *)backing->pool->storage+backing->offset bytesPerRow:pitch];
}
static void refresh_linear_pool(Pool *pool,size_t offset,size_t size) {
    pthread_mutex_lock(&pool->lock);
    for(TextureBacking *b=pool->textures;b;b=b->next)if(offset<b->offset+b->size && b->offset<offset+size)refresh_linear_locked(b);
    pthread_mutex_unlock(&pool->lock);
}
static void backing_release(TextureBacking *backing) {
    if (!backing) return;
    Pool *pool=backing->pool;
    pthread_mutex_lock(&pool->lock);
    if (!backing->references) hy_fail("invalid texture backing release");
    if (!--backing->references) {
        TextureBacking **link=&pool->textures;
        while (*link && *link!=backing) link=&(*link)->next;
        if (!*link) hy_fail("texture backing missing");
        *link=backing->next;
        id image=(__bridge_transfer id)backing->image; (void)image; free(backing);
    }
    pthread_mutex_unlock(&pool->lock);
}
static void texture_finalize(Texture *t) {
    if (!t->backing) hy_fail("invalid texture finalize");
    pthread_mutex_lock(&t->backing->pool->lock);
    if(!t->backing->objects)hy_fail("invalid texture object ownership");
    t->backing->objects--;pthread_mutex_unlock(&t->backing->pool->lock);
    backing_release(t->backing); memset(t, 0, sizeof *t);
}
static int texture_width_value(const Texture *t) { return t->builder.width; }
static int texture_height_value(const Texture *t) { return t->builder.height; }
static void texture_label(Texture *t, const char *label) {
    id<MTLTexture> image=(__bridge id<MTLTexture>)t->image;
    image.label=label ? [NSString stringWithUTF8String:label] : nil;
}
static void view_defaults(TextureView *v) { memset(v, 0, sizeof *v); }
static void view_levels(TextureView *v, int first, int count) { if(first<0 || first>255 || count<0 || count>255)hy_fail("invalid view mip range");v->fields |= 1; v->level=first; v->levels=count; }
static void view_layers(TextureView *v, int first, int count) { if(first<0 || first>65535 || count<0 || count>65535)hy_fail("invalid view layer range");v->fields |= 2; v->layer=first; v->layers=count; }
static void view_format(TextureView *v, int format) { if(format<0 || format>65535)hy_fail("invalid view format");v->fields |= 4; v->format=format; }
static void view_target(TextureView *v, int target) { if(target<0 || target>255)hy_fail("invalid view target");v->fields |= 8; v->target=target; }
static void view_depth_mode(TextureView *v, int mode) { if (mode<0 || mode>1) hy_fail("invalid texture depth/stencil mode"); v->fields |= 16; v->depth_mode=(uint8_t)mode; }
static void view_swizzle(TextureView *v, int r, int g, int b, int a) {
    int channels[]={r,g,b,a};
    for (int i=0;i<4;i++) {
        if (channels[i]<0 || channels[i]>5) hy_fail("invalid texture swizzle");
        v->swizzle[i]=(uint8_t)channels[i];
    }
    v->fields |= 32;
}
static Descriptor *descriptor_slot(const DescriptorPool *pool, int index) {
    if (!pool->pool || index<0 || index>=pool->count) hy_fail("NVN descriptor index out of range");
    return (Descriptor *)((uint8_t *)pool->pool->storage+pool->offset)+(size_t)index;
}
static id<MTLTexture> texture_view(const Texture *t,const TextureView *view) {
    if (!t || !t->image) hy_fail("registering invalid texture");
    id<MTLTexture> image=(__bridge id<MTLTexture>)t->image;
    int first=0,count=t->builder.levels;MTLPixelFormat format=image.pixelFormat;
    const uint8_t identity[]={2,3,4,5};const uint8_t *swizzle=t->builder.has_swizzle ? t->builder.swizzle : identity;
    if (view) {
        if (((view->fields&2) && (view->layer || view->layers>1)) ||
            ((view->fields&8) && view->target!=1) || ((view->fields&16) && view->depth_mode!=0))hy_fail("unsupported texture view layers/depth");
        first=(view->fields&1) ? view->level : 0;
        count=(view->fields&1) && view->levels ? view->levels : t->builder.levels-first;
        if(first<0 || count<1 || first>=t->builder.levels || count>t->builder.levels-first)hy_fail("texture view mip range");
        format=(view->fields&4) ? texture_metal_format(view->format) : image.pixelFormat;
        if(view->fields&32)swizzle=view->swizzle;
    }
    if(first || count!=t->builder.levels || format!=image.pixelFormat || memcmp(swizzle,identity,4)) {
        const MTLTextureSwizzle channels[]={MTLTextureSwizzleZero,MTLTextureSwizzleOne,MTLTextureSwizzleRed,MTLTextureSwizzleGreen,MTLTextureSwizzleBlue,MTLTextureSwizzleAlpha};
        image=[image newTextureViewWithPixelFormat:format textureType:MTLTextureType2D levels:NSMakeRange(first,count) slices:NSMakeRange(0,1)
            swizzle:MTLTextureSwizzleChannelsMake(channels[swizzle[0]],channels[swizzle[1]],channels[swizzle[2]],channels[swizzle[3]])];
        if(!image)hy_fail("Metal texture view creation failed");
    }
    return image;
}
static void register_texture(const DescriptorPool *pool, int index, const Texture *t, const TextureView *view) {
    id<MTLTexture> image=texture_view(t,view);
    pthread_mutex_lock(&descriptors_lock);Descriptor *slot=descriptor_slot(pool,index);
    pthread_mutex_lock(&t->backing->pool->lock);
    t->backing->references++;
    pthread_mutex_unlock(&t->backing->pool->lock);
    id previous=(__bridge_transfer id)slot->resource; (void)previous;
    backing_release(slot->view);
    slot->resource=(__bridge_retained void *)image; slot->view=t->backing;
    pthread_mutex_unlock(&descriptors_lock);
}
@interface HyTargetImage : NSObject {
@public id<MTLTexture> image; TextureBacking *backing; int format;
}
@end
@implementation HyTargetImage
- (void)dealloc { backing_release(backing); }
@end
@interface HyRenderTargets : NSObject <HyPoolReference> {
@public NSArray *colors; HyTargetImage *depth;
}
@end
@implementation HyRenderTargets
- (BOOL)usesPool:(Pool *)pool {
    if (depth && depth->backing->pool==pool) return YES;
    for (id color in colors) if (color!=[NSNull null] && ((HyTargetImage *)color)->backing->pool==pool) return YES;
    return NO;
}
@end
static HyTargetImage *image_snapshot(const Texture *texture,const TextureView *view) {
    HyTargetImage *snapshot=[HyTargetImage new];snapshot->image=texture_view(texture,view);snapshot->backing=texture->backing;
    snapshot->format=view && (view->fields&4) ? view->format : texture->builder.format;
    pthread_mutex_lock(&snapshot->backing->pool->lock);snapshot->backing->references++;
    pthread_mutex_unlock(&snapshot->backing->pool->lock);return snapshot;
}
static HyTargetImage *target_image(const Texture *texture,const TextureView *view,int depth) {
    HyTargetImage *snapshot=image_snapshot(texture,view);id<MTLTexture> image=snapshot->image;
    if (!(image.usage&MTLTextureUsageRenderTarget) || image.mipmapLevelCount!=1 ||
        ((snapshot->format==0x34 || snapshot->format==0x36)!=!!depth))
        hy_fail("unsupported render target format/view");
    return snapshot;
}
@interface HyTextureCopy : NSObject <HyPoolReference> {
@public HyTargetImage *source,*destination; HyCopyRegion from,to; uint32_t flags;
}
@end
@implementation HyTextureCopy
- (BOOL)usesPool:(Pool *)pool { return source->backing->pool==pool || destination->backing->pool==pool; }
@end
static void check_copy_region(id<MTLTexture> image,const HyCopyRegion *region) {
    if (!region || region->x<0 || region->y<0 || region->z || region->width<1 || region->height<1 || region->depth!=1 ||
        (NSUInteger)region->x>image.width || (NSUInteger)region->y>image.height ||
        (NSUInteger)region->width>image.width-(NSUInteger)region->x || (NSUInteger)region->height>image.height-(NSUInteger)region->y)
        hy_fail("texture copy region outside image bounds");
}
static void command_copy_texture(HyCommands *c,const Texture *source,const TextureView *source_view,const HyCopyRegion *from,
                                 const Texture *destination,const TextureView *destination_view,const HyCopyRegion *to,uint32_t flags) {
    HyTextureCopy *copy=[HyTextureCopy new];copy->source=image_snapshot(source,source_view);
    copy->destination=image_snapshot(destination,destination_view);
    if(copy->destination->backing->flags&0x10)hy_fail("GPU copy into CPU linear texture is unsupported");
    check_copy_region(copy->source->image,from);check_copy_region(copy->destination->image,to);
    copy->from=*from;copy->to=*to;copy->flags=flags;
    void *value=(__bridge void *)copy;void *record=hy_commands_record(c,HY_COPY_TEXTURE,sizeof value);
    memcpy(record,&value,sizeof value);command_keep(record,sizeof value,copy);
}
static void command_targets(HyCommands *c,int count,Texture *const *textures,const TextureView *const *views,
                            const Texture *depth,const TextureView *depth_view) {
    if (count<0 || count>8 || (count && !textures)) hy_fail("invalid render target count");
    HyRenderTargets *targets=[HyRenderTargets new];NSMutableArray *colors=[NSMutableArray new];
    for (int i=0;i<count;i++) [colors addObject:textures[i] ? target_image(textures[i],views ? views[i] : NULL,0) : (id)[NSNull null]];
    targets->colors=[colors copy];if (depth) targets->depth=target_image(depth,depth_view,1);
    void *value=(__bridge void *)targets;void *record=hy_commands_record(c,HY_RENDER_TARGETS,sizeof value);
    memcpy(record,&value,sizeof value);command_keep(record,sizeof value,targets);
}
static void descriptor_finalize(DescriptorPool *pool) {
    pthread_mutex_lock(&descriptors_lock);
    for (int i=0;i<pool->count;i++) {
        Descriptor *slot=descriptor_slot(pool,i);
        id resource=(__bridge_transfer id)slot->resource; (void)resource;
        backing_release(slot->view); *slot=(Descriptor){0};
    }
    memset(pool,0,sizeof *pool);pthread_mutex_unlock(&descriptors_lock);
}
static uint32_t sampler_initialize(void **object, const void *builder) {
    void *(*get)(const char *)=hy_symbol("nvnBootstrapLoader");
    if (!get || !device) hy_fail("sampler initialized without SDK or Metal device");
    HySamplerConfig config={0};
    ((void (*)(const void *,int *,int *))get("nvnSamplerBuilderGetMinMagFilter"))(builder,&config.min_filter,&config.mag_filter);
    ((void (*)(const void *,int *,int *,int *))get("nvnSamplerBuilderGetWrapMode"))(builder,&config.wrap[0],&config.wrap[1],&config.wrap[2]);
    ((void (*)(const void *,float *,float *))get("nvnSamplerBuilderGetLodClamp"))(builder,&config.lod_min,&config.lod_max);
    ((void (*)(const void *,int *,int *))get("nvnSamplerBuilderGetCompare"))(builder,&config.compare_mode,&config.compare_func);
    ((void (*)(const void *,float *))get("nvnSamplerBuilderGetBorderColor"))(builder,config.border);
    config.bias=((float (*)(const void *))get("nvnSamplerBuilderGetLodBias"))(builder);
    config.anisotropy=((float (*)(const void *))get("nvnSamplerBuilderGetMaxAnisotropy"))(builder);
    config.reduction=((int (*)(const void *))get("nvnSamplerBuilderGetReductionFilter"))(builder);
    config.snap=((float (*)(const void *))get("nvnSamplerBuilderGetLodSnap"))(builder);
    // Capture all SDK settings. A shader may need to implement legacy wrap modes;
    // never replace them with an inexact Metal sampler merely to pass initialization.
    HySampler *sampler=[HySampler new]; sampler->config=config;
    *object=(__bridge_retained void *)sampler; return 1;
}
static void sampler_finalize(void **object) { id sampler=(__bridge_transfer id)*object; (void)sampler; *object=NULL; }
static void register_sampler(const DescriptorPool *pool, int index, void *const *object) {
    if (!*object) hy_fail("registering invalid sampler");
    pthread_mutex_lock(&descriptors_lock);Descriptor *slot=descriptor_slot(pool,index);
    id sampler=(__bridge id)*object, previous=(__bridge_transfer id)slot->resource; (void)previous;
    backing_release(slot->view); slot->view=NULL; slot->resource=(__bridge_retained void *)sampler;pthread_mutex_unlock(&descriptors_lock);
}
static uint64_t texture_handle(const void *object,int texture,int sampler) {
    (void)object;if(texture<0 || texture>=1048576 || sampler<0 || sampler>=4096)hy_fail("invalid texture/sampler ID");
    uint32_t bits=(uint32_t)texture|((uint32_t)sampler<<20);return (uint64_t)(int64_t)(int32_t)bits|0x100000000ull;
}
static void command_textures(HyCommands *c,int stage,int first,int count,const uint64_t *handles) {
    if(first<0 || count<0 || first>32 || count>32-first || (count && !handles))hy_fail("invalid texture binding range");
    for(int i=0;i<count;i++)hy_commands_texture(c,stage,first+i,handles[i]);
}
static NSArray *sampled_resources(const DescriptorPool *textures,const DescriptorPool *samplers,uint64_t handle) {
    if(!(handle&0x100000000ull))hy_fail("invalid combined texture handle");
    pthread_mutex_lock(&descriptors_lock);pthread_mutex_lock(&pools_lock);
    const DescriptorPool *sets[]={textures,samplers};int indices[]={(int)(handle&0xfffff),(int)((handle>>20)&4095)};Descriptor *slots[2];
    for(unsigned i=0;i<2;i++) {
        const DescriptorPool *set=sets[i];Pool *p;
        for(p=pools;p && p!=set->pool;p=p->next){}
        if(!p || set->count<1 || set->offset>p->size || (size_t)set->count>(p->size-set->offset)/sizeof(Descriptor))hy_fail("descriptor pool is not live");
        slots[i]=descriptor_slot(set,indices[i]);
    }
    id image=(__bridge id)slots[0]->resource,sampler=(__bridge id)slots[1]->resource;
    if(!slots[0]->view || !image || ![image conformsToProtocol:@protocol(MTLTexture)] || slots[1]->view || ![sampler isKindOfClass:HySampler.class])hy_fail("descriptor resource type mismatch");
    TextureBacking *backing=slots[0]->view;pthread_mutex_lock(&backing->pool->lock);refresh_linear_locked(backing);pthread_mutex_unlock(&backing->pool->lock);
    NSArray *result=@[image,sampler];pthread_mutex_unlock(&pools_lock);pthread_mutex_unlock(&descriptors_lock);return result;
}
static void window_defaults(WindowBuilder *b) { void *d=b->device; memset(b,0,sizeof *b); b->device=d; b->interval=1; }
static void window_device(WindowBuilder *b, void *d) { b->device=d; }
static void window_native(WindowBuilder *b, void *native) { b->native=native; }
static void window_interval(WindowBuilder *b, int interval) {
    if (interval < 0 || interval > 1) hy_fail("unsupported present interval"); b->interval=interval;
}
static void window_textures(WindowBuilder *b, int count, Texture *const *textures) {
    if (count < 2 || count > 4) hy_fail("unsupported window texture count: %d",count);
    b->count=count; memcpy(b->textures,textures,(size_t)count*sizeof *textures);
}
static uint32_t window_initialize(Window *w, const WindowBuilder *b) {
    if (!b->device || b->native != (__bridge void *)surface || b->count < 2 || b->count > 4)
        hy_fail("invalid NVN window");
    Texture *first=b->textures[0];
    for (int i=0;i<b->count;i++) {
        Texture *t=b->textures[i];
        if (!t->image || t->builder.width!=first->builder.width || t->builder.height!=first->builder.height ||
            t->builder.format!=first->builder.format) hy_fail("incompatible window texture");
    }
    surface.drawableSize=CGSizeMake(first->builder.width,first->builder.height);
    // CAMetalLayer remains BGRA8; presentation must sample the RGBA game target.
    *w=(Window){.builder=*b,.crop={0,0,first->builder.width,first->builder.height}};
    fprintf(stderr,"NVN window: %d textures, %dx%d\n",b->count,first->builder.width,first->builder.height);
    return 1;
}
static int window_acquire(Window *w,GpuSync **sync,int *index) {
    if (!sync || !*sync || !index) hy_fail("invalid window acquisition");
    for(int attempt=0;attempt<w->builder.count;attempt++) {
        int candidate=(w->next+attempt)%w->builder.count;
        if(w->acquired[candidate])continue;
        void *host_queue=(__bridge void *)queue;queue_fence(&host_queue,sync,0,0);
        w->acquired[candidate]=1;w->next=(candidate+1)%w->builder.count;*index=candidate;return 0;
    }
    hy_fail("all window textures are still acquired");
}
static void window_crop(Window *w, int x, int y, int width, int height) {
    Texture *texture=w->builder.textures[0];
    if (x<0 || y<0 || width<1 || height<1 || x>texture->builder.width || y>texture->builder.height ||
        width>texture->builder.width-x || height>texture->builder.height-y) hy_fail("window crop out of range");
    w->crop[0]=x; w->crop[1]=y; w->crop[2]=width; w->crop[3]=height;
}
static void window_present_interval(Window *w, int interval) { window_interval(&w->builder,interval); }
static int window_get_interval(const Window *w) { return w->builder.interval; }
static void window_get_crop(const Window *w, int *rectangle) { memcpy(rectangle,w->crop,sizeof w->crop); }
static void display_initialize(void) {
    if (!device) hy_fail("display initialized before Metal");
    if(!surface)surface=[CAMetalLayer layer];surface.device = device;
    surface.pixelFormat = MTLPixelFormatBGRA8Unorm;
    surface.drawableSize = CGSizeMake(1280, 720);
}
void hy_graphics_attach_layer(void *layer) {
    if(surface || !layer)hy_fail("graphics layer already assigned or missing");
    surface=(__bridge CAMetalLayer *)layer;
}
static unsigned display_open(void **out) { *out = &display_token; return 0; }
static unsigned display_layer(void **out, void *display) {
    if (display != &display_token || !surface) hy_fail("invalid display");
    *out = (__bridge void *)surface; return 0;
}
static unsigned display_native(void **out, void *layer) {
    if (layer != (__bridge void *)surface) hy_fail("invalid native layer");
    *out = layer; return 0;
}
static uint64_t monotonic_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static unsigned display_vsync(void *event, void *display) {
    if (display != &display_token || !event) hy_fail("invalid display vsync event");
    pthread_mutex_lock(&display_lock);
    if (vsync_event) hy_fail("duplicate display vsync event");
    vsync_event = event; next_tick = monotonic_ns() + 16666667;
    pthread_mutex_unlock(&display_lock);
    fputs("Headless display timing: 60 Hz clock (physical presentation not yet connected)\n", stderr);
    return 0;
}
static void display_resolution_event(void *event) {
    pthread_mutex_lock(&display_lock);
    if (!event || resolution_event) hy_fail("invalid resolution event");
    resolution_event = event;
    pthread_mutex_unlock(&display_lock);
}
int hy_graphics_event(void *event, int operation) {
    pthread_mutex_lock(&display_lock);
    if (!event || (event != vsync_event && event != resolution_event)) {
        pthread_mutex_unlock(&display_lock); return -1;
    }
    int vsync = event == vsync_event;
    if (operation == 3) {
        if (vsync) vsync_event = NULL; else resolution_event = NULL;
        pthread_cond_broadcast(&display_changed);
        pthread_mutex_unlock(&display_lock); return 1;
    }
    if (!vsync) {
        // Fixed headless resolution never signals a change. Keep a real wait
        // pending until event destruction rather than reporting a false change.
        if (!operation) while (resolution_event==event)
            pthread_cond_wait(&display_changed,&display_lock);
        pthread_mutex_unlock(&display_lock); return 0;
    }
    if (operation == 2) { next_tick = monotonic_ns() + 16666667; pthread_mutex_unlock(&display_lock); return 1; }
    for (;;) {
        uint64_t now = monotonic_ns();
        if (now >= next_tick) {
            next_tick += ((now - next_tick) / 16666667 + 1) * 16666667;
            pthread_mutex_unlock(&display_lock); return 1;
        }
        if (operation == 1) { pthread_mutex_unlock(&display_lock); return 0; }
        uint64_t delay = next_tick - now;
        pthread_mutex_unlock(&display_lock);
        struct timespec pause = {delay / 1000000000, delay % 1000000000}; nanosleep(&pause, NULL);
        pthread_mutex_lock(&display_lock);
        if (vsync_event != event) { pthread_mutex_unlock(&display_lock); return 0; }
    }
}
static uint32_t display_poll(void *event) {
    int result = hy_graphics_event(event, 1);
    if (result >= 0) return result;
    uint32_t (*original)(void *) = hy_symbol("_ZN2nn2os18TryWaitSystemEventEPNS0_15SystemEventTypeE");
    return original(event);
}
static void display_clear(void *event) {
    if (hy_graphics_event(event, 2) >= 0) return;
    void (*original)(void *) = hy_symbol("_ZN2nn2os16ClearSystemEventEPNS0_15SystemEventTypeE");
    original(event);
}
static void command_validate(uint32_t op,const void *payload,size_t size) {
    size_t expected;
    switch(op) {
    case HY_BEGIN:case HY_END:expected=0;break;
    case HY_TEXTURE_POOL:case HY_SAMPLER_POOL:expected=sizeof(DescriptorPool);break;
    case HY_MULTISAMPLE_STATE:expected=24;break;
    case HY_REPORT_COUNTER:expected=sizeof(HyCounterRecord);break;
    case HY_BIND_PROGRAM:expected=sizeof(HyProgramRecord);break;
    case HY_BIND_TEXTURE:expected=sizeof(HyTextureRecord);break;
    case HY_CLEAR_COLOR:expected=sizeof(HyClearColorRecord);break;
    case HY_CLEAR_DEPTH:expected=sizeof(HyClearDepthRecord);break;
    case HY_VIEWPORT:case HY_SCISSOR:expected=16;break;
    case HY_VERTEX_ATTRIBUTES:case HY_VERTEX_STREAMS: {
        uint32_t count;
        if(size<8) hy_fail("invalid command payload length");
        memcpy(&count,payload,4);
        if(count>16) hy_fail("invalid command vertex state count");
        expected=8+(size_t)count*(op==HY_VERTEX_ATTRIBUTES ? 4 : 8);break;
    }
    case HY_JUMP:case HY_COLOR_STATE:case HY_BLEND_STATE:case HY_CHANNEL_MASK_STATE:
    case HY_POLYGON_STATE:case HY_DEPTH_STENCIL_STATE:case HY_DEPTH_RANGE:case HY_DEPTH_CLAMP:
    case HY_RENDER_TARGETS:case HY_UNIFORM_BUFFER:case HY_STENCIL_MASK:case HY_STENCIL_VALUE_MASK:
    case HY_ALPHA_REF:case HY_STENCIL_REF:case HY_VERTEX_BUFFER:case HY_DRAW_ELEMENTS:
    case HY_COPY_TEXTURE:case HY_TILED_CACHE:case HY_BARRIER:case HY_FENCE_SYNC:expected=8;break;
    default:hy_fail("unknown recorded command opcode: %u",op);
    }
    if(size!=((expected+7)&~(size_t)7)) hy_fail("invalid command payload length");
}
#include "replay.inc"
static void *lookup(const char *name) {
    static const struct { const char *name; void *fn; } functions[] = {
        {"nvnDeviceGetProcAddress", get_proc}, {"nvnDeviceGetInteger", integer},
        {"nvnDeviceGetTimestampInNanoseconds", device_timestamp},
        {"nvnDeviceGetTextureHandle", texture_handle},
        {"nvnCommandBufferBindTexture", hy_commands_texture},
        {"nvnCommandBufferBindTextures", command_textures},
        {"nvnProgramInitialize", program_initialize}, {"nvnProgramFinalize", program_finalize},
        {"nvnProgramSetShaders", program_shaders},
        {"nvnProgramSetDebugLabel", program_label},
        {"nvnDeviceBuilderSetDefaults", builder_defaults}, {"nvnDeviceBuilderSetFlags", builder_flags},
        {"nvnDeviceInitialize", initialize_device},
        {"nvnDeviceSetDepthMode", set_depth}, {"nvnDeviceSetWindowOriginMode", set_origin},
        {"nvnMemoryPoolBuilderSetDefaults", pool_defaults}, {"nvnMemoryPoolBuilderSetDevice", pool_device},
        {"nvnMemoryPoolBuilderSetStorage", pool_storage}, {"nvnMemoryPoolBuilderSetFlags", pool_flags},
        {"nvnMemoryPoolInitialize", pool_initialize}, {"nvnMemoryPoolFinalize", pool_finalize},
        {"nvnMemoryPoolMap", pool_map}, {"nvnMemoryPoolGetBufferAddress", pool_address},
        {"nvnMemoryPoolGetSize", pool_size}, {"nvnMemoryPoolGetFlags", pool_get_flags},
        {"nvnMemoryPoolFlushMappedRange", pool_flush}, {"nvnMemoryPoolInvalidateMappedRange", pool_flush},
        {"nvnVertexAttribStateSetDefaults", hy_vertex_defaults}, {"nvnVertexAttribStateSetFormat", hy_vertex_format},
        {"nvnVertexAttribStateSetStreamIndex", hy_vertex_stream}, {"nvnVertexAttribStateGetFormat", hy_vertex_get_format},
        {"nvnVertexAttribStateGetStreamIndex", hy_vertex_get_stream},
        {"nvnVertexStreamStateSetDefaults", hy_stream_defaults}, {"nvnVertexStreamStateSetStride", hy_stream_stride},
        {"nvnVertexStreamStateSetDivisor", hy_stream_divisor}, {"nvnVertexStreamStateGetStride", hy_stream_get_stride},
        {"nvnVertexStreamStateGetDivisor", hy_stream_get_divisor},
        {"nvnBufferBuilderSetDefaults", buffer_defaults}, {"nvnBufferBuilderSetDevice", buffer_device},
        {"nvnBufferBuilderSetStorage", buffer_storage}, {"nvnBufferInitialize", buffer_initialize},
        {"nvnBufferFinalize", buffer_finalize}, {"nvnBufferMap", buffer_map},
        {"nvnBufferGetAddress", buffer_address}, {"nvnBufferGetSize", buffer_size},
        {"nvnBufferGetMemoryOffset", buffer_offset}, {"nvnBufferGetMemoryPool", buffer_pool},
        {"nvnBufferFlushMappedRange", buffer_flush}, {"nvnBufferInvalidateMappedRange", buffer_flush},
        {"nvnTexturePoolInitialize", descriptor_initialize}, {"nvnSamplerPoolInitialize", descriptor_initialize},
        {"nvnTexturePoolRegisterTexture", register_texture}, {"nvnSamplerPoolRegisterSampler", register_sampler},
        {"nvnSamplerInitialize", sampler_initialize}, {"nvnSamplerFinalize", sampler_finalize},
        {"nvnTexturePoolFinalize", descriptor_finalize}, {"nvnSamplerPoolFinalize", descriptor_finalize},
        {"nvnQueueBuilderSetDefaults", queue_defaults}, {"nvnQueueBuilderSetDevice", queue_device},
        {"nvnQueueBuilderSetFlags", queue_flags}, {"nvnQueueBuilderSetCommandMemorySize", queue_command_size},
        {"nvnQueueBuilderSetComputeMemorySize", queue_compute_size}, {"nvnQueueBuilderSetControlMemorySize", queue_control_size},
        {"nvnQueueBuilderSetCommandFlushThreshold", queue_threshold}, {"nvnQueueBuilderSetQueueMemory", queue_memory},
        {"nvnQueueBuilderGetQueueMemorySize", queue_memory_size}, {"nvnQueueInitialize", queue_initialize},
        {"nvnSyncInitialize", sync_initialize}, {"nvnSyncFinalize", sync_finalize},
        {"nvnSyncWait", sync_wait}, {"nvnQueueFenceSync", queue_fence}, {"nvnQueueWaitSync", queue_wait},
        {"nvnQueueFinish", queue_finish}, {"nvnQueueFinalize", queue_finalize},
        {"nvnQueueSubmitCommands", queue_submit},
        {"nvnQueuePresentTexture", queue_present},
        {"nvnQueueResetMemoryUsageCounts", queue_reset_used},
        {"nvnQueueGetTotalCommandMemoryUsed", queue_command_used},
        {"nvnQueueGetTotalControlMemoryUsed", queue_control_used},
        {"nvnQueueGetTotalComputeMemoryUsed", queue_compute_used},
        {"nvnQueueFlush", queue_finish}, // Synchronous flush until asynchronous submission is implemented.
        {"nvnCommandBufferReportCounter", command_counter},
        {"nvnCommandBufferSetDepthRange", hy_commands_depth_range},
        {"nvnCommandBufferSetDepthClamp", hy_commands_depth_clamp},
        {"nvnCommandBufferSetViewport", hy_commands_viewport},
        {"nvnCommandBufferSetScissor", hy_commands_scissor},
        {"nvnCommandBufferClearColor", hy_commands_clear_color},
        {"nvnCommandBufferClearDepthStencil", hy_commands_clear_depth_stencil},
        {"nvnCommandBufferSetTexturePool", command_texture_pool}, {"nvnCommandBufferSetSamplerPool", command_sampler_pool},
        {"nvnCommandBufferBindMultisampleState", command_multisample}, {"nvnCommandBufferBindColorState", command_color},
        {"nvnCommandBufferBindBlendState", command_blend}, {"nvnCommandBufferBindChannelMaskState", command_channels},
        {"nvnCommandBufferBindPolygonState", command_polygon}, {"nvnCommandBufferBindDepthStencilState", command_depth_stencil},
        {"nvnCommandBufferInitialize", command_initialize},
        {"nvnCommandBufferSetRenderTargets", command_targets},
        {"nvnCommandBufferBindProgram", command_program},
        {"nvnCommandBufferBindUniformBuffer", command_uniform},
        {"nvnCommandBufferSetStencilMask", hy_commands_stencil_mask},
        {"nvnCommandBufferSetStencilValueMask", hy_commands_stencil_value_mask},
        {"nvnCommandBufferSetAlphaRef", hy_commands_alpha_ref},
        {"nvnCommandBufferSetStencilRef", hy_commands_stencil_ref},
        {"nvnCommandBufferBindVertexAttribState", hy_commands_attributes},
        {"nvnCommandBufferBindVertexStreamState", hy_commands_streams},
        {"nvnCommandBufferBindVertexBuffers", command_vertex_buffers},
        {"nvnCommandBufferDrawElements", command_draw_elements},
        {"nvnCommandBufferDrawElementsBaseVertex", command_draw_elements_base},
        {"nvnCommandBufferCopyTextureToTexture", command_copy_texture},
        {"nvnCommandBufferSetTiledCacheAction", hy_commands_tiled_cache},
        {"nvnCommandBufferBarrier", hy_commands_barrier},
        {"nvnCommandBufferFenceSync", command_fence},
        {"nvnCommandBufferSetMemoryCallback", hy_commands_callback}, {"nvnCommandBufferSetMemoryCallbackData", hy_commands_context},
        {"nvnCommandBufferAddCommandMemory", command_memory}, {"nvnCommandBufferAddControlMemory", hy_commands_control},
        {"nvnCommandBufferBeginRecording", hy_commands_begin}, {"nvnCommandBufferEndRecording", hy_commands_end},
        {"nvnTextureGetWidth", texture_width_value}, {"nvnTextureGetHeight", texture_height_value},
        {"nvnTextureBuilderSetDevice", texture_device}, {"nvnTextureBuilderSetDefaults", texture_defaults},
        {"nvnTextureBuilderSetFlags", texture_flags}, {"nvnTextureBuilderSetTarget", texture_target},
        {"nvnTextureBuilderSetWidth", texture_width}, {"nvnTextureBuilderSetHeight", texture_height},
        {"nvnTextureBuilderSetDepth", texture_depth}, {"nvnTextureBuilderSetSize2D", texture_size2d},
        {"nvnTextureBuilderSetSize3D", texture_size3d},
        {"nvnTextureBuilderSetLevels", texture_levels}, {"nvnTextureBuilderSetFormat", texture_format},
        {"nvnTextureBuilderSetDepthStencilMode", texture_depth_mode}, {"nvnTextureBuilderSetSwizzle", texture_swizzle},
        {"nvnTextureBuilderSetSamples", texture_samples}, {"nvnTextureBuilderSetStorage", texture_storage},
        {"nvnTextureBuilderSetStride", texture_stride}, {"nvnTextureBuilderSetPackagedTextureLayout", texture_layout},
        {"nvnTextureBuilderGetPackagedTextureLayout", texture_get_layout},
        {"nvnTextureBuilderGetStorageSize", texture_size},
        {"nvnTextureBuilderGetStorageAlignment", texture_alignment},
        {"nvnTextureBuilderGetSparseTileLayout", texture_sparse_tiles},
        {"nvnTextureBuilderGetFlags", texture_get_flags}, {"nvnTextureBuilderGetTarget", texture_get_target},
        {"nvnTextureBuilderGetWidth", texture_get_width}, {"nvnTextureBuilderGetHeight", texture_get_height},
        {"nvnTextureBuilderGetDepth", texture_get_depth}, {"nvnTextureBuilderGetLevels", texture_get_levels},
        {"nvnTextureBuilderGetFormat", texture_get_format}, {"nvnTextureBuilderGetSamples", texture_get_samples},
        {"nvnTextureInitialize", texture_initialize}, {"nvnTextureFinalize", texture_finalize},
        {"nvnTextureSetDebugLabel", texture_label},
        {"nvnTextureViewSetDefaults", view_defaults}, {"nvnTextureViewSetLevels", view_levels},
        {"nvnTextureViewSetLayers", view_layers}, {"nvnTextureViewSetFormat", view_format}, {"nvnTextureViewSetTarget", view_target},
        {"nvnTextureViewSetDepthStencilMode", view_depth_mode}, {"nvnTextureViewSetSwizzle", view_swizzle},
        {"nvnWindowAcquireTexture", window_acquire},
        {"nvnWindowSetPresentInterval", window_present_interval}, {"nvnWindowGetPresentInterval", window_get_interval},
        {"nvnWindowSetCrop", window_crop}, {"nvnWindowGetCrop", window_get_crop},
        {"nvnWindowBuilderSetDefaults", window_defaults}, {"nvnWindowBuilderSetDevice", window_device},
        {"nvnWindowBuilderSetNativeWindow", window_native}, {"nvnWindowBuilderSetTextures", window_textures},
        {"nvnWindowBuilderSetPresentInterval", window_interval}, {"nvnWindowInitialize", window_initialize},
    };
    for (size_t i = 0; i < sizeof functions / sizeof functions[0]; i++)
        if (!strcmp(name, functions[i].name)) return functions[i].fn;
    // These SDK objects only pack CPU render-state bits. Preserve the shipped ABI;
    // GPU binding and drawing remain host operations and never fall through here.
    static const char *const cpu_states[]={"nvnMultisampleState","nvnColorState","nvnBlendState",
        "nvnChannelMaskState","nvnPolygonState","nvnDepthStencilState","nvnSamplerBuilder"};
    for (size_t i=0;i<sizeof cpu_states/sizeof cpu_states[0];i++) {
        size_t length=strlen(cpu_states[i]);
        if (!strncmp(name,cpu_states[i],length) &&
            (!strncmp(name+length,"Set",3) || !strncmp(name+length,"Get",3))) {
            void *(*original)(const char *)=hy_symbol("nvnBootstrapLoader");
            void *function=original ? original(name) : NULL;
            if (!function) hy_fail("missing SDK CPU render state: %s",name);
            return function;
        }
    }
    for (size_t i = 0; i < hy_nvn_name_count; i++)
        if (!strcmp(name, hy_nvn_names[i])) return hy_nvn_gates[i];
    hy_fail("unknown NVN API name: %s", name);
}
void *hy_graphics_service(const char *name) {
    if (!strcmp(name, "_ZN2nn2vi10InitializeEv")) return display_initialize;
    if (!strcmp(name, "_ZN2nn2vi18OpenDefaultDisplayEPPNS0_7DisplayE")) return display_open;
    if (!strcmp(name, "_ZN2nn2vi11CreateLayerEPPNS0_5LayerEPNS0_7DisplayE")) return display_layer;
    if (!strcmp(name, "_ZN2nn2vi15GetNativeWindowEPPvPNS0_5LayerE")) return display_native;
    if (!strcmp(name, "_ZN2nn2vi20GetDisplayVsyncEventEPNS_2os15SystemEventTypeEPNS0_7DisplayE")) return display_vsync;
    if (!strcmp(name, "_ZN2nn2oe38GetDefaultDisplayResolutionChangeEventEPNS_2os11SystemEventE")) return display_resolution_event;
    if (!strcmp(name, "_ZN2nn2os18TryWaitSystemEventEPNS0_15SystemEventTypeE")) return display_poll;
    if (!strcmp(name, "_ZN2nn2os16ClearSystemEventEPNS0_15SystemEventTypeE")) return display_clear;
    if (!strcmp(name, "_ZN2nv18InitializeGraphicsEPvm")) return initialize;
    if (!strcmp(name, "_ZN2nv20SetGraphicsAllocatorEPFPvmmS0_EPFvS0_S0_EPFS0_S0_mS0_ES0_")) return allocator;
    if (!strcmp(name, "nvnBootstrapLoader")) return lookup;
    return NULL;
}
