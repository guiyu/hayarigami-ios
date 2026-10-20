#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "../src/gfx/gpu_time.h"
#include <assert.h>
#include <stdio.h>

static void marker(id<MTLDevice> device,id<MTLCommandBuffer> command,
                   id<MTLCounterSampleBuffer> samples,NSUInteger index,id<MTLBuffer> scratch) {
    if([device supportsCounterSampling:MTLCounterSamplingPointAtStageBoundary]) {
        MTLBlitPassDescriptor *pass=[MTLBlitPassDescriptor blitPassDescriptor];
        pass.sampleBufferAttachments[0].sampleBuffer=samples;
        pass.sampleBufferAttachments[0].startOfEncoderSampleIndex=index;
        pass.sampleBufferAttachments[0].endOfEncoderSampleIndex=index+1;
        id<MTLBlitCommandEncoder> encoder=[command blitCommandEncoderWithDescriptor:pass];
        assert(encoder);
        [encoder fillBuffer:scratch range:NSMakeRange(index*4,8) value:(uint8_t)(index+1)];
        [encoder endEncoding];
    } else {
        assert([device supportsCounterSampling:MTLCounterSamplingPointAtBlitBoundary]);
        id<MTLBlitCommandEncoder> encoder=[command blitCommandEncoder];assert(encoder);
        [encoder sampleCountersInBuffer:samples atSampleIndex:index withBarrier:YES];
        [encoder fillBuffer:scratch range:NSMakeRange(index*4,8) value:(uint8_t)(index+1)];
        [encoder sampleCountersInBuffer:samples atSampleIndex:index+1 withBarrier:YES];
        [encoder endEncoding];
    }
}
int main(void) {
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();assert(device);
        MTLCounterSampleBufferDescriptor *desc=[MTLCounterSampleBufferDescriptor new];
        for(id<MTLCounterSet> set in device.counterSets)
            if([set.name isEqualToString:MTLCommonCounterSetTimestamp]) desc.counterSet=set;
        assert(desc.counterSet);desc.storageMode=MTLStorageModeShared;desc.sampleCount=4;
        NSError *error=nil;
        id<MTLCounterSampleBuffer> samples=[device newCounterSampleBufferWithDescriptor:desc error:&error];
        if(!samples) {fprintf(stderr,"%s\n",error.localizedDescription.UTF8String);return 1;}
        id<MTLCommandQueue> queue=[device newCommandQueue];
        id<MTLCommandBuffer> command=[queue commandBuffer];
        id<MTLBuffer> buffer=[device newBufferWithLength:4096 options:MTLResourceStorageModeShared];
        assert(buffer);memset(buffer.contents,0,buffer.length);
        id<MTLBuffer> scratch=[device newBufferWithLength:16 options:MTLResourceStorageModeShared];
        assert(scratch);memset(scratch.contents,0,scratch.length);
        marker(device,command,samples,0,scratch);
        id<MTLBlitCommandEncoder> encoder=[command blitCommandEncoder];
        [encoder fillBuffer:buffer range:NSMakeRange(0,buffer.length) value:0x5a];[encoder endEncoding];
        marker(device,command,samples,2,scratch);
        MTLTimestamp cpu0,gpu0,cpu1,gpu1;
        [device sampleTimestamps:&cpu0 gpuTimestamp:&gpu0];
        [command commit];[command waitUntilCompleted];
        [device sampleTimestamps:&cpu1 gpuTimestamp:&gpu1];
        assert(command.status==MTLCommandBufferStatusCompleted);
        NSData *resolved=[samples resolveCounterRange:NSMakeRange(0,4)];
        assert(resolved.length==4*sizeof(MTLCounterResultTimestamp));
        const MTLCounterResultTimestamp *values=resolved.bytes;
        fprintf(stderr,"Calibration CPU %llu..%llu GPU %llu..%llu; samples %llu %llu %llu %llu\n",
                cpu0,cpu1,gpu0,gpu1,values[0].timestamp,values[1].timestamp,values[2].timestamp,values[3].timestamp);
        uint64_t previous=0,first=0;
        for(unsigned i=0;i<4;i++) {
            uint64_t ns=0;assert(values[i].timestamp && values[i].timestamp!=MTLCounterErrorValue);
            assert(hy_gpu_time_ns(cpu0,gpu0,cpu1,gpu1,values[i].timestamp,&ns));
            assert(ns>=cpu0 && ns<=cpu1 && ns>=previous);
            if(!i) first=ns;
            previous=ns;
        }
        assert(previous>first);
        for(NSUInteger i=0;i<buffer.length;i++) assert(((uint8_t *)buffer.contents)[i]==0x5a);
        for(NSUInteger i=0;i<scratch.length;i++) assert(((uint8_t *)scratch.contents)[i]==(i<8 ? 1 : 3));
        puts("PASS: four real GPU timestamp samples bracket a verified GPU buffer fill and calibrate to CPU nanoseconds");
    }
}
