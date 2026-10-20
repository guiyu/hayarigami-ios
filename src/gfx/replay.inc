// Initially synchronous: every encoder completes before the next operation.
// This preserves queue ordering and CPU visibility while unsupported paths fail explicitly.
#include "gpu_time.h"
@interface HyReplayState : NSObject {
@public
    NSMutableDictionary<NSNumber *,NSData *> *states;
    NSMutableDictionary<NSNumber *,HyBoundBuffer *> *uniforms,*vertices;
    NSMutableDictionary<NSNumber *,NSNumber *> *texture_handles;
    HyShader *shaders[6];HyRenderTargets *targets;
    uint32_t stencil_read[2],stencil_write[2],stencil_ref[2];
    size_t command_bytes,control_bytes;uint64_t draw_count;
}
@end
@implementation HyReplayState
- (instancetype)init {
    if((self=[super init])) {
        states=[NSMutableDictionary new];uniforms=[NSMutableDictionary new];vertices=[NSMutableDictionary new];texture_handles=[NSMutableDictionary new];
        stencil_read[0]=stencil_read[1]=stencil_write[0]=stencil_write[1]=255;
    }
    return self;
}
@end
static NSMutableDictionary<NSValue *,HyReplayState *> *replay_queues;
static pthread_mutex_t replay_lock=PTHREAD_MUTEX_INITIALIZER;
static HyReplayState *replay_state(void *q) {
    pthread_mutex_lock(&replay_lock);
    if(!replay_queues) replay_queues=[NSMutableDictionary new];
    NSValue *key=[NSValue valueWithPointer:q];HyReplayState *state=replay_queues[key];
    if(!state) {state=[HyReplayState new];replay_queues[key]=state;}
    pthread_mutex_unlock(&replay_lock);return state;
}
static void replay_release_queue(void *q) {
    pthread_mutex_lock(&replay_lock);[replay_queues removeObjectForKey:[NSValue valueWithPointer:q]];pthread_mutex_unlock(&replay_lock);
}
static void replay_release_pool(Pool *pool) {
    pthread_mutex_lock(&replay_lock);NSArray *all=[replay_queues.allValues copy];pthread_mutex_unlock(&replay_lock);
    for(HyReplayState *state in all) @synchronized(state) {
        if([state->targets usesPool:pool]) state->targets=nil;
        for(NSMutableDictionary *bindings in @[state->uniforms,state->vertices])
            for(NSNumber *key in [bindings.allKeys copy]) if([(HyBoundBuffer *)bindings[key] usesPool:pool]) [bindings removeObjectForKey:key];
        for(NSNumber *key in @[@(HY_TEXTURE_POOL),@(HY_SAMPLER_POOL)]) {
            NSData *bytes=state->states[key];DescriptorPool d;
            if(bytes.length==sizeof d) {memcpy(&d,bytes.bytes,sizeof d);if(d.pool==pool)[state->states removeObjectForKey:key];}
        }
    }
}
static void replay_complete(id<MTLCommandBuffer> command) {
    if(!command) hy_fail("Metal command allocation failed");
    graphics_wait_active();[command commit];[command waitUntilCompleted];
    if(command.status!=MTLCommandBufferStatusCompleted) hy_fail("Metal execution failed: %s",command.error.localizedDescription.UTF8String);
}
static const void *replay_bytes(HyReplayState *state,uint32_t op,size_t size) {
    NSData *bytes=state->states[@(op)];
    if(bytes.length<size) hy_fail("draw state not initialized: %u",op);
    return bytes.bytes;
}
static int replay_get(const void *bytes,const char *name) {return ((int (*)(const void *))lookup(name))(bytes);}
static MTLRenderPassDescriptor *replay_pass(HyReplayState *state) {
    if(!state->targets) hy_fail("render targets not initialized");
    MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
    for(NSUInteger i=0;i<state->targets->colors.count;i++) {
        id color=state->targets->colors[i];if(color==NSNull.null)continue;
        pass.colorAttachments[i].texture=((HyTargetImage *)color)->image;
        pass.colorAttachments[i].loadAction=MTLLoadActionLoad;pass.colorAttachments[i].storeAction=MTLStoreActionStore;
    }
    if(state->targets->depth) {
        id<MTLTexture> depth=state->targets->depth->image;
        pass.depthAttachment.texture=depth;pass.depthAttachment.loadAction=MTLLoadActionLoad;pass.depthAttachment.storeAction=MTLStoreActionStore;
        if(depth.pixelFormat==MTLPixelFormatDepth32Float_Stencil8) {
            pass.stencilAttachment.texture=depth;pass.stencilAttachment.loadAction=MTLLoadActionLoad;pass.stencilAttachment.storeAction=MTLStoreActionStore;
        }
    }
    return pass;
}
static void replay_clear_scissor(HyReplayState *state,id<MTLTexture> image) {
    NSData *bytes=state->states[@(HY_SCISSOR)];
    if(bytes) {int32_t rect[4];memcpy(rect,bytes.bytes,16);
        if(rect[0] || rect[1] || rect[2]!=(int)image.width || rect[3]!=(int)image.height) hy_fail("partial scissor clear not implemented");}
}
static void replay_clear(HyReplayState *state,id<MTLCommandQueue> q,uint32_t op,const void *bytes) {
    MTLRenderPassDescriptor *pass=replay_pass(state);
    if(op==HY_CLEAR_COLOR) {
        HyClearColorRecord clear;memcpy(&clear,bytes,sizeof clear);
        if(clear.index<0 || clear.index>=8 || (clear.mask&~15u))hy_fail("invalid replay color clear");
        if(!clear.mask)return;
        if(clear.mask!=15 || !pass.colorAttachments[clear.index].texture) hy_fail("masked/absent color clear not implemented");
        replay_clear_scissor(state,pass.colorAttachments[clear.index].texture);
        pass.colorAttachments[clear.index].loadAction=MTLLoadActionClear;
        pass.colorAttachments[clear.index].clearColor=MTLClearColorMake(clear.color[0],clear.color[1],clear.color[2],clear.color[3]);
    } else {
        HyClearDepthRecord clear;memcpy(&clear,bytes,sizeof clear);
        if(!clear.depth_mask && !clear.stencil_mask)return;
        if(!pass.depthAttachment.texture || (clear.stencil_mask && (clear.stencil_mask!=255 || !pass.stencilAttachment.texture)))
            hy_fail("masked/absent depth stencil clear not implemented");
        replay_clear_scissor(state,pass.depthAttachment.texture);
        if(clear.depth_mask) {pass.depthAttachment.loadAction=MTLLoadActionClear;pass.depthAttachment.clearDepth=clear.depth;}
        if(clear.stencil_mask) {pass.stencilAttachment.loadAction=MTLLoadActionClear;pass.stencilAttachment.clearStencil=(uint32_t)clear.stencil&255;}
    }
    id<MTLCommandBuffer> command=[q commandBuffer];id<MTLRenderCommandEncoder> encoder=[command renderCommandEncoderWithDescriptor:pass];
    if(!encoder)hy_fail("Metal clear encoder unavailable");[encoder endEncoding];replay_complete(command);
}
static void replay_counter(id<MTLCommandQueue> q,const HyCounterRecord *record) {
    if((record->type!=0 && record->type!=16) || (record->address&7))hy_fail("unsupported replay counter");
    HyBoundBuffer *destination=bound_buffer(-3,0,record->address,sizeof(HyCounterData));
    MTLCounterSampleBufferDescriptor *desc=[MTLCounterSampleBufferDescriptor new];
    for(id<MTLCounterSet> set in device.counterSets)if([set.name isEqual:MTLCommonCounterSetTimestamp])desc.counterSet=set;
    if(!desc.counterSet)hy_fail("GPU timestamp counter set unavailable");
    desc.sampleCount=2;desc.storageMode=MTLStorageModeShared;NSError *error=nil;
    id<MTLCounterSampleBuffer> samples=[device newCounterSampleBufferWithDescriptor:desc error:&error];
    id<MTLBuffer> scratch=[device newBufferWithLength:4 options:MTLResourceStorageModeShared];
    if(!samples || !scratch)hy_fail("GPU timestamp allocation failed");
    id<MTLCommandBuffer> command=[q commandBuffer];id<MTLBlitCommandEncoder> encoder;
    if([device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]) {
        MTLBlitPassDescriptor *pass=[MTLBlitPassDescriptor blitPassDescriptor];
        pass.sampleBufferAttachments[0].sampleBuffer=samples;pass.sampleBufferAttachments[0].startOfEncoderSampleIndex=0;pass.sampleBufferAttachments[0].endOfEncoderSampleIndex=1;
        encoder=[command blitCommandEncoderWithDescriptor:pass];
    } else {
        if(![device supportsCounterSampling:MTLCounterSamplingPointAtBlitBoundary])hy_fail("GPU timestamp sampling unavailable");
        encoder=[command blitCommandEncoder];[encoder sampleCountersInBuffer:samples atSampleIndex:0 withBarrier:YES];
    }
    if(!encoder)hy_fail("GPU timestamp encoder unavailable");
    [encoder fillBuffer:scratch range:NSMakeRange(0,4) value:0];
    if(![device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]) [encoder sampleCountersInBuffer:samples atSampleIndex:1 withBarrier:YES];
    [encoder endEncoding];MTLTimestamp cpu0,gpu0,cpu1,gpu1;[device sampleTimestamps:&cpu0 gpuTimestamp:&gpu0];
    replay_complete(command);[device sampleTimestamps:&cpu1 gpuTimestamp:&gpu1];
    NSData *resolved=[samples resolveCounterRange:NSMakeRange(0,2)];
    if(resolved.length!=2*sizeof(MTLCounterResultTimestamp))hy_fail("GPU timestamp resolve failed");
    const MTLCounterResultTimestamp *values=resolved.bytes;uint64_t raw=values[record->type==16 ? 0 : 1].timestamp;
    HyCounterData result={0,0};
    if(!raw || raw==MTLCounterErrorValue || !hy_gpu_time_ns(cpu0,gpu0,cpu1,gpu1,raw,&result.timestamp))hy_fail("GPU timestamp outside calibration");
    memcpy((uint8_t *)destination->buffer.contents+destination->offset,&result,sizeof result);
}
static void replay_color_copy(id<MTLCommandQueue> q,id<MTLTexture> source,id<MTLTexture> destination,
                              const HyCopyRegion *from,const HyCopyRegion *to) {
    NSString *text=@"#include <metal_stdlib>\nusing namespace metal;vertex float4 copy_vertex(uint id [[vertex_id]]){float2 p[3]={float2(-1,-1),float2(3,-1),float2(-1,3)};return float4(p[id],0,1);}fragment float4 copy_fragment(float4 p [[position]],texture2d<float,access::read> image [[texture(0)]],constant int4 &offset [[buffer(0)]]){return image.read(uint2(int2(p.xy)+offset.xy-offset.zw));}";
    NSError *error=nil;id<MTLLibrary> library=[device newLibraryWithSource:text options:nil error:&error];
    if(!library)hy_fail("Metal color copy library failed: %s",error.localizedDescription.UTF8String);
    MTLRenderPipelineDescriptor *pipeline=[MTLRenderPipelineDescriptor new];pipeline.vertexFunction=[library newFunctionWithName:@"copy_vertex"];
    pipeline.fragmentFunction=[library newFunctionWithName:@"copy_fragment"];pipeline.colorAttachments[0].pixelFormat=destination.pixelFormat;
    id<MTLRenderPipelineState> ps=[device newRenderPipelineStateWithDescriptor:pipeline error:&error];
    if(!ps)hy_fail("Metal color copy pipeline failed: %s",error.localizedDescription.UTF8String);
    MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];pass.colorAttachments[0].texture=destination;
    pass.colorAttachments[0].loadAction=MTLLoadActionLoad;pass.colorAttachments[0].storeAction=MTLStoreActionStore;
    id<MTLCommandBuffer> command=[q commandBuffer];id<MTLRenderCommandEncoder> encoder=[command renderCommandEncoderWithDescriptor:pass];
    if(!encoder)hy_fail("Metal color copy encoder unavailable");int32_t offset[]={from->x,from->y,to->x,to->y};
    [encoder setRenderPipelineState:ps];[encoder setFragmentTexture:source atIndex:0];[encoder setFragmentBytes:offset length:sizeof offset atIndex:0];
    [encoder setScissorRect:(MTLScissorRect){(NSUInteger)to->x,(NSUInteger)to->y,(NSUInteger)to->width,(NSUInteger)to->height}];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];[encoder endEncoding];replay_complete(command);
}
static void replay_copy(id<MTLCommandQueue> q,HyTextureCopy *copy) {
    check_copy_region(copy->source->image,&copy->from);check_copy_region(copy->destination->image,&copy->to);
    if(copy->flags || copy->from.width!=copy->to.width || copy->from.height!=copy->to.height)hy_fail("scaled/flagged texture copy not implemented");
    if(copy->source->backing==copy->destination->backing && copy->from.x<copy->to.x+copy->to.width &&
       copy->to.x<copy->from.x+copy->from.width && copy->from.y<copy->to.y+copy->to.height && copy->to.y<copy->from.y+copy->from.height)
        hy_fail("overlapping texture copy not implemented");
    if(copy->source->format==copy->destination->format) {
        id<MTLCommandBuffer> command=[q commandBuffer];id<MTLBlitCommandEncoder> encoder=[command blitCommandEncoder];
        if(!encoder)hy_fail("Metal texture copy encoder unavailable");
        [encoder copyFromTexture:copy->source->image sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(copy->from.x,copy->from.y,0)
            sourceSize:MTLSizeMake(copy->from.width,copy->from.height,1) toTexture:copy->destination->image destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(copy->to.x,copy->to.y,0)];
        [encoder endEncoding];replay_complete(command);
    } else if(copy->source->format==0x3d && copy->destination->format==0x25) {
        replay_color_copy(q,copy->source->image,copy->destination->image,&copy->from,&copy->to);
    } else hy_fail("texture copy format conversion not implemented: %#x -> %#x",copy->source->format,copy->destination->format);
}
static MTLBlendFactor replay_blend_factor(int value) {
    static const MTLBlendFactor table[]={MTLBlendFactorZero,MTLBlendFactorOne,MTLBlendFactorSourceColor,MTLBlendFactorOneMinusSourceColor,
        MTLBlendFactorSourceAlpha,MTLBlendFactorOneMinusSourceAlpha,MTLBlendFactorDestinationAlpha,MTLBlendFactorOneMinusDestinationAlpha,
        MTLBlendFactorDestinationColor,MTLBlendFactorOneMinusDestinationColor,MTLBlendFactorSourceAlphaSaturated};
    if(value<1 || value>11)hy_fail("unverified blend factor: %d",value);return table[value-1];
}
static MTLBlendOperation replay_blend_op(int value) {
    static const MTLBlendOperation ops[]={MTLBlendOperationAdd,MTLBlendOperationSubtract,MTLBlendOperationReverseSubtract,MTLBlendOperationMin,MTLBlendOperationMax};
    if(value<1 || value>5)hy_fail("unverified blend equation");return ops[value-1];
}
static MTLCompareFunction replay_compare(int value) {if(value<1 || value>8)hy_fail("invalid compare function");return (MTLCompareFunction)(value-1);}
static MTLStencilOperation replay_stencil_op(int value) {
    static const MTLStencilOperation ops[]={MTLStencilOperationKeep,MTLStencilOperationZero,MTLStencilOperationReplace,MTLStencilOperationIncrementClamp,
        MTLStencilOperationDecrementClamp,MTLStencilOperationInvert,MTLStencilOperationIncrementWrap,MTLStencilOperationDecrementWrap};
    if(value<1 || value>8)hy_fail("invalid stencil operation");return ops[value-1];
}
static void replay_draw(HyReplayState *state,id<MTLCommandQueue> q,HyDrawIndexed *draw) {
    if(!draw->count)return;
    if(!state->shaders[0] || !state->shaders[1])hy_fail("vertex/fragment shader missing");
    for(unsigned i=2;i<6;i++)if(state->shaders[i])hy_fail("unimplemented shader stage in draw");
    const void *color=replay_bytes(state,HY_COLOR_STATE,4),*blend=replay_bytes(state,HY_BLEND_STATE,8),*multi=replay_bytes(state,HY_MULTISAMPLE_STATE,24);
    const void *polygon=replay_bytes(state,HY_POLYGON_STATE,4),*depth=replay_bytes(state,HY_DEPTH_STENCIL_STATE,8),*channels=replay_bytes(state,HY_CHANNEL_MASK_STATE,4);
    int logic=replay_get(color,"nvnColorStateGetLogicOp"),samples=replay_get(multi,"nvnMultisampleStateGetSamples"),enabled=replay_get(multi,"nvnMultisampleStateGetMultisampleEnable");
    int coverage=replay_get(multi,"nvnMultisampleStateGetAlphaToCoverageEnable"),mode=replay_get(polygon,"nvnPolygonStateGetPolygonMode");
    if(logic!=3 || samples>1 || enabled || coverage || mode!=2)
        hy_fail("unverified rasterization profile: logic=%d samples=%d enabled=%d coverage=%d polygon=%d",logic,samples,enabled,coverage,mode);
    int alpha=replay_get(color,"nvnColorStateGetAlphaTest");float alpha_ref;memcpy(&alpha_ref,replay_bytes(state,HY_ALPHA_REF,4),4);
    NSString *root=getenv("HY_SHADER_DIR") ? @(getenv("HY_SHADER_DIR")) : [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"Shaders"];
    NSDictionary *vinfo=nil,*finfo=nil;MTLRenderPipelineDescriptor *pipeline=[MTLRenderPipelineDescriptor new];
    pipeline.vertexFunction=hy_metal_shader(state->shaders[0],root,8,0,&vinfo);pipeline.fragmentFunction=hy_metal_shader(state->shaders[1],root,alpha,alpha_ref,&finfo);
    for(NSDictionary *info in @[vinfo,finfo]) {
        for(NSString *key in @[@"SBuffers",@"Images"])if(![info[key] isKindOfClass:NSArray.class] || [info[key] count])hy_fail("shader resource bindings not implemented: %s",key.UTF8String);
        for(NSString *key in @[@"UsesInstanceId",@"UsesDrawParameters",@"UsesRtLayer",@"ClipDistancesWritten"])if([info[key] boolValue])hy_fail("unverified shader draw feature");
    }
    MTLRenderPassDescriptor *pass=replay_pass(state);
    int blend_target=replay_get(blend,"nvnBlendStateGetBlendTarget");
    for(unsigned i=0;i<8;i++)if(pass.colorAttachments[i].texture) {
        MTLRenderPipelineColorAttachmentDescriptor *attachment=pipeline.colorAttachments[i];attachment.pixelFormat=pass.colorAttachments[i].texture.pixelFormat;
        uint8_t mask[4]={0};((void (*)(const void *,int,uint8_t *,uint8_t *,uint8_t *,uint8_t *))lookup("nvnChannelMaskStateGetChannelMask"))(channels,(int)i,&mask[0],&mask[1],&mask[2],&mask[3]);
        attachment.writeMask=(mask[0]?MTLColorWriteMaskRed:0)|(mask[1]?MTLColorWriteMaskGreen:0)|(mask[2]?MTLColorWriteMaskBlue:0)|(mask[3]?MTLColorWriteMaskAlpha:0);
        attachment.blendingEnabled=((int (*)(const void *,int))lookup("nvnColorStateGetBlendEnable"))(color,(int)i);
        if(attachment.blendingEnabled) {
            if(blend_target!=(int)i)hy_fail("blend target state missing");int factors[4],ops[2];
            ((void (*)(const void *,int *,int *,int *,int *))lookup("nvnBlendStateGetBlendFunc"))(blend,&factors[0],&factors[1],&factors[2],&factors[3]);
            ((void (*)(const void *,int *,int *))lookup("nvnBlendStateGetBlendEquation"))(blend,&ops[0],&ops[1]);
            attachment.sourceRGBBlendFactor=replay_blend_factor(factors[0]);attachment.destinationRGBBlendFactor=replay_blend_factor(factors[1]);
            attachment.sourceAlphaBlendFactor=replay_blend_factor(factors[2]);attachment.destinationAlphaBlendFactor=replay_blend_factor(factors[3]);
            attachment.rgbBlendOperation=replay_blend_op(ops[0]);attachment.alphaBlendOperation=replay_blend_op(ops[1]);
        }
    }
    if(pass.depthAttachment.texture)pipeline.depthAttachmentPixelFormat=pass.depthAttachment.texture.pixelFormat;
    if(pass.stencilAttachment.texture)pipeline.stencilAttachmentPixelFormat=pass.stencilAttachment.texture.pixelFormat;
    const uint32_t *attributes=replay_bytes(state,HY_VERTEX_ATTRIBUTES,8),*streams=replay_bytes(state,HY_VERTEX_STREAMS,8);
    pipeline.vertexDescriptor=[MTLVertexDescriptor vertexDescriptor];uint32_t maximum=0;
    if(draw->index_type!=1 && draw->index_type!=2)hy_fail("8-bit GPU indices not implemented");
    const uint8_t *indices=(const uint8_t *)draw->indices->buffer.contents+draw->indices->offset;
    for(int i=0;i<draw->count;i++) {uint32_t index=0;memcpy(&index,indices+((size_t)i<<draw->index_type),1u<<draw->index_type);int64_t effective=(int64_t)index+draw->base_vertex;if(effective<0 || effective>UINT32_MAX)hy_fail("indexed base vertex outside range");if((uint32_t)effective>maximum)maximum=(uint32_t)effective;}
    for(uint32_t i=0;i<attributes[0];i++) {
        uint32_t attribute=attributes[2+i];int format=attribute&255;if(!format)continue;
        unsigned stream=attribute>>27;size_t offset=(attribute>>8)&0x7ffff;unsigned width;MTLVertexFormat mapped;
        switch(format) {case 0x16:mapped=MTLVertexFormatFloat2;width=8;break;case 0x22:mapped=MTLVertexFormatFloat3;width=12;break;
            case 0x2e:mapped=MTLVertexFormatFloat4;width=16;break;case 0x25:mapped=MTLVertexFormatUChar4Normalized;width=4;break;default:hy_fail("unimplemented vertex format %#x",format);}
        if(stream>=streams[0] || stream>14)hy_fail("unsupported vertex stream");
        const HyVertexStream *layout=(const HyVertexStream *)(streams+2)+stream;HyBoundBuffer *buffer=state->vertices[@(stream)];
        if(!layout->stride || layout->divisor || !buffer || !buffer->buffer || offset>layout->stride || width>layout->stride-offset ||
           offset>buffer->length || width>buffer->length-offset || maximum>(buffer->length-offset-width)/layout->stride)hy_fail("vertex fetch outside binding");
        pipeline.vertexDescriptor.attributes[i].format=mapped;pipeline.vertexDescriptor.attributes[i].offset=offset;pipeline.vertexDescriptor.attributes[i].bufferIndex=16+stream;
        pipeline.vertexDescriptor.layouts[16+stream].stride=layout->stride;
    }
    NSError *error=nil;id<MTLRenderPipelineState> ps=[device newRenderPipelineStateWithDescriptor:pipeline error:&error];
    if(!ps)hy_fail("Metal render pipeline failed: %s",error.localizedDescription.UTF8String);
    MTLDepthStencilDescriptor *ds=[MTLDepthStencilDescriptor new];int depth_test=replay_get(depth,"nvnDepthStencilStateGetDepthTestEnable");
    ds.depthCompareFunction=depth_test ? replay_compare(replay_get(depth,"nvnDepthStencilStateGetDepthFunc")) : MTLCompareFunctionAlways;
    ds.depthWriteEnabled=depth_test && replay_get(depth,"nvnDepthStencilStateGetDepthWriteEnable");
    if((depth_test || ds.depthWriteEnabled) && !pass.depthAttachment.texture)hy_fail("draw depth target absent");
    if(replay_get(depth,"nvnDepthStencilStateGetStencilTestEnable")) {
        if(!pass.stencilAttachment.texture)hy_fail("draw stencil target absent");
        for(unsigned face=0;face<2;face++) {
            MTLStencilDescriptor *stencil=[MTLStencilDescriptor new];int ops[3];
            stencil.stencilCompareFunction=replay_compare(((int (*)(const void *,int))lookup("nvnDepthStencilStateGetStencilFunc"))(depth,(int)face+1));
            ((void (*)(const void *,int,int *,int *,int *))lookup("nvnDepthStencilStateGetStencilOp"))(depth,(int)face+1,&ops[0],&ops[1],&ops[2]);
            stencil.stencilFailureOperation=replay_stencil_op(ops[0]);stencil.depthFailureOperation=replay_stencil_op(ops[1]);stencil.depthStencilPassOperation=replay_stencil_op(ops[2]);
            stencil.readMask=state->stencil_read[face];stencil.writeMask=state->stencil_write[face];
            if(!face)ds.frontFaceStencil=stencil;else ds.backFaceStencil=stencil;
        }
    }
    id<MTLDepthStencilState> dss=[device newDepthStencilStateWithDescriptor:ds];if(!dss)hy_fail("Metal depth stencil allocation failed");
    id<MTLCommandBuffer> command=[q commandBuffer];id<MTLRenderCommandEncoder> encoder=[command renderCommandEncoderWithDescriptor:pass];if(!encoder)hy_fail("Metal draw encoder unavailable");
    [encoder setRenderPipelineState:ps];[encoder setDepthStencilState:dss];
    [encoder setStencilFrontReferenceValue:state->stencil_ref[0] backReferenceValue:state->stencil_ref[1]];
    int cull=replay_get(polygon,"nvnPolygonStateGetCullFace"),front=replay_get(polygon,"nvnPolygonStateGetFrontFace");
    if(cull<0 || cull>2 || front<0 || front>1)hy_fail("unimplemented cull/front mode");
    [encoder setCullMode:cull==0 ? MTLCullModeNone : cull==1 ? MTLCullModeFront : MTLCullModeBack];[encoder setFrontFacingWinding:front ? MTLWindingCounterClockwise : MTLWindingClockwise];
    uint32_t clamp;memcpy(&clamp,replay_bytes(state,HY_DEPTH_CLAMP,4),4);[encoder setDepthClipMode:clamp ? MTLDepthClipModeClamp : MTLDepthClipModeClip];
    int32_t viewport[4],scissor[4];float range[2];memcpy(viewport,replay_bytes(state,HY_VIEWPORT,16),16);memcpy(scissor,replay_bytes(state,HY_SCISSOR,16),16);memcpy(range,replay_bytes(state,HY_DEPTH_RANGE,8),8);
    if(viewport[2]<=0 || viewport[3]<=0 || scissor[0]<0 || scissor[1]<0 || scissor[2]<=0 || scissor[3]<=0)hy_fail("unsupported viewport/scissor");
    [encoder setViewport:(MTLViewport){viewport[0],viewport[1],viewport[2],viewport[3],range[0],range[1]}];[encoder setScissorRect:(MTLScissorRect){(NSUInteger)scissor[0],(NSUInteger)scissor[1],(NSUInteger)scissor[2],(NSUInteger)scissor[3]}];
    for(NSNumber *key in state->vertices) {unsigned slot=key.unsignedIntValue;if(slot<=14) {HyBoundBuffer *b=state->vertices[key];[encoder setVertexBuffer:b->buffer offset:b->offset atIndex:16+slot];}}
    unsigned stage=0;for(NSDictionary *info in @[vinfo,finfo]) {
        for(NSDictionary *texture in info[@"Textures"]) {
            int index=[texture[@"HandleIndex"] intValue],binding=[texture[@"Binding"] intValue];
            if([texture[@"Set"] intValue] || [texture[@"Type"] intValue]!=3 || [texture[@"Format"] intValue] || [texture[@"CbufSlot"] intValue]!=-1 ||
               [texture[@"ArrayLength"] intValue]!=1 || [texture[@"Separate"] boolValue] || [texture[@"Flags"] intValue] ||
               index<8 || (index&1) || index>=72 || binding<0 || binding>=31)hy_fail("unverified texture shader mapping");
            NSNumber *handle=state->texture_handles[@(stage*32+(unsigned)(index-8)/2)];if(!handle)hy_fail("shader texture binding missing");
            DescriptorPool textures,samplers;memcpy(&textures,replay_bytes(state,HY_TEXTURE_POOL,sizeof textures),sizeof textures);memcpy(&samplers,replay_bytes(state,HY_SAMPLER_POOL,sizeof samplers),sizeof samplers);
            NSArray *resources=sampled_resources(&textures,&samplers,handle.unsignedLongLongValue);id<MTLTexture> image=resources[0];HySampler *sampler=resources[1];
            if(image.textureType!=MTLTextureType2D || image.sampleCount!=1 || sampler->config.compare_mode ||
               image.pixelFormat==MTLPixelFormatDepth32Float || image.pixelFormat==MTLPixelFormatDepth32Float_Stencil8)hy_fail("unverified sampled texture type");
            id<MTLSamplerState> sampling=[sampler stateForDevice:device];
            if(!stage) {[encoder setVertexTexture:image atIndex:(NSUInteger)binding];[encoder setVertexSamplerState:sampling atIndex:(NSUInteger)binding];}
            else {[encoder setFragmentTexture:image atIndex:(NSUInteger)binding];[encoder setFragmentSamplerState:sampling atIndex:(NSUInteger)binding];}
        }
        for(NSDictionary *cb in info[@"CBuffers"]) {
            int slot=[cb[@"Slot"] intValue],binding=[cb[@"Binding"] intValue];
            if((slot!=1 && (slot<3 || slot>=35)) || binding<0 || binding>=16 || [cb[@"Set"] intValue] || [cb[@"Flags"] intValue])hy_fail("unverified constant buffer mapping");
            const void *bytes;size_t length;
            if(slot==1) {
                // The immutable program payload is CB1, also used by the
                // translator's ConstantBuffer1Read accessor for folded reads.
                NSData *constants=state->shaders[stage]->constants;bytes=constants.bytes;length=constants.length;
            } else {
                HyBoundBuffer *b=state->uniforms[@(stage*32+(unsigned)slot-3)];
                if(!b || !b->buffer)hy_fail("missing constant buffer");bytes=(const uint8_t *)b->buffer.contents+b->offset;length=b->length;
            }
            if(!length || length>65536)hy_fail("missing/oversized constant buffer");
            id<MTLBuffer> padded=[device newBufferWithLength:65536 options:MTLResourceStorageModeShared];if(!padded)hy_fail("constant buffer allocation failed");
            memset(padded.contents,0,padded.length);memcpy(padded.contents,bytes,length);
            if(!stage)[encoder setVertexBuffer:padded offset:0 atIndex:(NSUInteger)binding];else[encoder setFragmentBuffer:padded offset:0 atIndex:(NSUInteger)binding];
        }
        stage++;
    }
    MTLPrimitiveType primitive;
    switch(draw->primitive) {case 0:primitive=MTLPrimitiveTypePoint;break;case 1:primitive=MTLPrimitiveTypeLine;break;case 3:primitive=MTLPrimitiveTypeLineStrip;break;case 4:primitive=MTLPrimitiveTypeTriangle;break;case 5:primitive=MTLPrimitiveTypeTriangleStrip;break;default:hy_fail("unimplemented draw primitive");}
    [encoder drawIndexedPrimitives:primitive indexCount:(NSUInteger)draw->count indexType:draw->index_type==1 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32 indexBuffer:draw->indices->buffer indexBufferOffset:draw->indices->offset instanceCount:1 baseVertex:draw->base_vertex baseInstance:0];
    [encoder endEncoding];replay_complete(command);
    if(state->draw_count++==0)fputs("Metal first draw completed\n",stderr);
}
static void queue_submit(void *const *object,int count,const uint64_t *handles) {
    if(!object || !*object || count<0 || count>64 || (count && !handles))hy_fail("invalid command submission");
    HyReplayState *state=replay_state(*object);id<MTLCommandQueue> q=(__bridge id<MTLCommandQueue>)*object;
    @synchronized(state) {
        if((size_t)count>(SIZE_MAX-state->control_bytes)/sizeof(uint64_t))hy_fail("queue control accounting overflow");
        state->control_bytes+=(size_t)count*sizeof(uint64_t);
        for(int list=0;list<count;list++) {
            uint64_t address=handles[list];
            for(unsigned step=0;;step++) @autoreleasepool {
                if(step==4096 || (address&7))hy_fail("command traversal limit/alignment");
                NSData *head=pool_copy(address,8);HyRecord header;memcpy(&header,head.bytes,8);
                if(header.size<8 || (header.size&7) || header.size>1048576)hy_fail("invalid recorded command size");
                NSData *data=pool_copy(address,header.size);const uint8_t *bytes=(const uint8_t *)data.bytes+8;size_t size=header.size-8;command_validate(header.op,bytes,size);
                if(!step && header.op!=HY_BEGIN)hy_fail("command handle does not start with BEGIN");
                if(header.size>SIZE_MAX-state->command_bytes)hy_fail("queue command accounting overflow");state->command_bytes+=header.size;
                id resource=nil;
                switch(header.op) {
                case HY_RENDER_TARGETS:case HY_UNIFORM_BUFFER:case HY_VERTEX_BUFFER:case HY_DRAW_ELEMENTS:case HY_COPY_TEXTURE:case HY_FENCE_SYNC:case HY_BIND_PROGRAM: {
                    void *token;memcpy(&token,bytes,8);resource=command_resource(address,token,header.size,header.op);break;
                }
                default:break;
                }
                switch(header.op) {
                case HY_BEGIN:case HY_END:break;
                case HY_JUMP:memcpy(&address,bytes,8);continue;
                case HY_RENDER_TARGETS:state->targets=resource;break;
                case HY_UNIFORM_BUFFER: {HyBoundBuffer *b=resource;state->uniforms[@(b->stage*32+b->index)]=b;break;}
                case HY_VERTEX_BUFFER: {HyBoundBuffer *b=resource;state->vertices[@(b->index)]=b;break;}
                case HY_BIND_PROGRAM: {
                    HyProgramRecord record;memcpy(&record,bytes,sizeof record);if(record.stages&~63u)hy_fail("invalid program stage mask");
                    for(unsigned stage=0;stage<6;stage++)if(record.stages&(1u<<stage))state->shaders[stage]=nil;
                    for(HyShader *shader in (NSArray *)resource) {unsigned stage;
                        switch(shader->stage) {case 1:stage=0;break;case 5:stage=1;break;case 4:stage=2;break;case 2:stage=3;break;case 3:stage=4;break;case 6:stage=5;break;default:hy_fail("invalid captured shader stage");}
                        if(record.stages&(1u<<stage))state->shaders[stage]=shader;
                    }break;
                }
                case HY_BIND_TEXTURE: {HyTextureRecord binding;memcpy(&binding,bytes,sizeof binding);if(binding.stage>5 || binding.index>=32)hy_fail("invalid replay texture binding");state->texture_handles[@(binding.stage*32+binding.index)]=@(binding.handle);break;}
                case HY_CLEAR_COLOR:case HY_CLEAR_DEPTH:replay_clear(state,q,header.op,bytes);break;
                case HY_DRAW_ELEMENTS:replay_draw(state,q,resource);break;
                case HY_REPORT_COUNTER: {HyCounterRecord counter;memcpy(&counter,bytes,sizeof counter);replay_counter(q,&counter);break;}
                case HY_FENCE_SYNC: {HyRecordedSync *fence=resource;queue_fence(object,&fence->sync,fence->condition,fence->flags);break;}
                case HY_STENCIL_MASK:case HY_STENCIL_VALUE_MASK:case HY_STENCIL_REF: {
                    uint32_t values[2];memcpy(values,bytes,8);if(values[0]&~3u)hy_fail("invalid stencil faces");
                    for(unsigned face=0;face<2;face++)if(values[0]&(1u<<face)) {
                        uint32_t *target=header.op==HY_STENCIL_MASK ? state->stencil_write : header.op==HY_STENCIL_VALUE_MASK ? state->stencil_read : state->stencil_ref;target[face]=values[1]&255;
                    }break;
                }
                case HY_COPY_TEXTURE:replay_copy(q,resource);break;
                case HY_TILED_CACHE:case HY_BARRIER: {
                    uint32_t value;memcpy(&value,bytes,4);
                    if(header.op==HY_TILED_CACHE ? (value<1 || value>6) : (value&~255u)!=0)hy_fail("invalid cache/barrier action");
                    break; // All earlier GPU work has completed synchronously.
                }
                default:state->states[@(header.op)]=[NSData dataWithBytes:bytes length:size];break;
                }
                if(header.op==HY_END)break;
                if(address>UINT64_MAX-header.size)hy_fail("command address overflow");address+=header.size;
            }
        }
    }
}

static void queue_present(void *const *object,Window *window,int index) {
    if(!object || !*object || !window || index<0 || index>=window->builder.count || !window->acquired[index])hy_fail("invalid present texture acquisition");
    Texture *texture=window->builder.textures[index];
    if(!texture || !texture->image || texture->builder.format!=0x25)hy_fail("unsupported present image");
    if(window->crop[0] || window->crop[1] || window->crop[2]!=texture->builder.width || window->crop[3]!=texture->builder.height)hy_fail("cropped presentation not implemented");
    queue_finish(object);id<MTLTexture> image=(__bridge id<MTLTexture>)texture->image;
    if(surface.superlayer) {
        id<CAMetalDrawable> drawable=[surface nextDrawable];if(!drawable)hy_fail("Metal drawable unavailable");
        if(drawable.texture.width!=image.width || drawable.texture.height!=image.height)hy_fail("present drawable size mismatch");
        HyCopyRegion region={0,0,0,(int32_t)image.width,(int32_t)image.height,1};
        id<MTLCommandQueue> q=(__bridge id<MTLCommandQueue>)*object;replay_color_copy(q,image,drawable.texture,&region,&region);
        id<MTLCommandBuffer> command=[q commandBuffer];[command presentDrawable:drawable];replay_complete(command);
    } else {
        const char *directory=getenv("HY_FRAME_DIR");if(!directory || !*directory)hy_fail("headless presentation requires HY_FRAME_DIR");
        NSString *folder=@(directory);BOOL is_directory=NO;
        if(![NSFileManager.defaultManager fileExistsAtPath:folder isDirectory:&is_directory] || !is_directory)hy_fail("frame capture directory missing");
        const char *limit_text=getenv("HY_FRAME_LIMIT");uint64_t limit=0;
        if(limit_text) {char *end=NULL;limit=strtoull(limit_text,&end,10);if(!*limit_text || *end || limit<1 || limit>10000)hy_fail("invalid diagnostic frame limit");}
        if(window->frames<3 || !(window->frames%30) || (limit && window->frames+1==limit)) {
            NSMutableData *pixels=[NSMutableData dataWithLength:image.width*image.height*4];
            [image getBytes:pixels.mutableBytes bytesPerRow:image.width*4 fromRegion:MTLRegionMake2D(0,0,image.width,image.height) mipmapLevel:0];
            NSString *header=[NSString stringWithFormat:@"P6\n%lu %lu\n255\n",(unsigned long)image.width,(unsigned long)image.height];
            NSMutableData *ppm=[[header dataUsingEncoding:NSASCIIStringEncoding] mutableCopy];const uint8_t *rgba=pixels.bytes;
            for(NSUInteger i=0;i<image.width*image.height;i++)[ppm appendBytes:rgba+4*i length:3];
            NSError *error=nil;NSString *path=[folder stringByAppendingPathComponent:@"latest.ppm"];
            if(![ppm writeToFile:path options:NSDataWritingAtomic error:&error])hy_fail("frame capture write failed: %s",error.localizedDescription.UTF8String);
            if(window->frames<3) {
                NSString *first=[folder stringByAppendingPathComponent:[NSString stringWithFormat:@"frame-%03llu.ppm",window->frames]];
                if(![ppm writeToFile:first options:NSDataWritingAtomic error:&error])hy_fail("initial frame write failed");
            }
        }
        if(limit && window->frames+1>=limit)hy_fail("diagnostic headless frame limit reached: %llu",limit);
    }
#if !TARGET_OS_IPHONE
    const char *input_file=getenv("HY_INPUT_FILE");
    if(input_file) {
        FILE *input=fopen(input_file,"r");if(!input)hy_fail("diagnostic input file unavailable");
        unsigned long long buttons=0;int count=fscanf(input,"%llx",&buttons);fclose(input);
        if(count!=1)hy_fail("invalid/unavailable diagnostic input state");hy_input_set(buttons,1);
    }
#endif
    window->frames++;window->acquired[index]=0;
    if(window->frames<=3)fprintf(stderr,"Frame %llu completed (%s)\n",window->frames,surface.superlayer ? "Metal drawable" : "headless GPU readback");
}

static size_t queue_command_used(void *const *object) {
    if(!object || !*object)hy_fail("invalid queue accounting");HyReplayState *state=replay_state(*object);@synchronized(state){return state->command_bytes;}
}
static size_t queue_control_used(void *const *object) {
    if(!object || !*object)hy_fail("invalid queue accounting");HyReplayState *state=replay_state(*object);@synchronized(state){return state->control_bytes;}
}
static size_t queue_compute_used(void *const *object) {
    if(!object || !*object)hy_fail("invalid queue accounting");return 0; // Compute submissions are rejected by this adapter.
}
static void queue_reset_used(void *const *object) {
    if(!object || !*object)hy_fail("invalid queue accounting");HyReplayState *state=replay_state(*object);@synchronized(state){state->command_bytes=state->control_bytes=0;}
}
