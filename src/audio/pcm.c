// Title SDK AudioOut ABI: output fields at 0/4/8; buffer data/capacity/size
// at 8/16/24, confirmed against its pure getters and SetAudioOutBufferInfo.
#include "../runtime/runtime.h"
#include <AudioToolbox/AudioToolbox.h>
#include <TargetConditionals.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

enum { PCM_QUEUE=64 };
typedef struct {void *next,*data;uint64_t capacity,size;} PcmBuffer;
typedef struct {PcmBuffer *buffer;const int16_t *data;size_t frames;} PcmEntry;
static struct {
    void *object;AudioUnit unit;unsigned channels,rate;
    PcmEntry queue[PCM_QUEUE];_Atomic uint64_t written,consumed,released;
    _Atomic int running;size_t offset;
} pcm;
static pthread_mutex_t pcm_lock=PTHREAD_MUTEX_INITIALIZER;
static void pcm_object(const void *object) {if(!object || object!=pcm.object)hy_fail("invalid PCM AudioOut object");}
static OSStatus pcm_consume(void *context,AudioUnitRenderActionFlags *flags,const AudioTimeStamp *time,UInt32 bus,UInt32 frames,AudioBufferList *buffers) {
    (void)context;(void)flags;(void)time;(void)bus;
    if(buffers->mNumberBuffers!=1 || buffers->mBuffers[0].mDataByteSize<frames*pcm.channels*2u)return kAudio_ParamError;
    int16_t *out=buffers->mBuffers[0].mData;memset(out,0,frames*pcm.channels*2u);
    if(!atomic_load_explicit(&pcm.running,memory_order_acquire))return noErr;
    uint64_t head=atomic_load_explicit(&pcm.consumed,memory_order_relaxed);
    uint64_t tail=atomic_load_explicit(&pcm.written,memory_order_acquire);size_t done=0;
    while(done<frames && head<tail) {
        PcmEntry *entry=&pcm.queue[head%PCM_QUEUE];size_t count=entry->frames-pcm.offset;
        if(count>frames-done)count=frames-done;
        memcpy(out+done*pcm.channels,entry->data+pcm.offset*pcm.channels,count*pcm.channels*2);
        done+=count;pcm.offset+=count;
        if(pcm.offset==entry->frames){pcm.offset=0;head++;atomic_store_explicit(&pcm.consumed,head,memory_order_release);}
    }
    return noErr;
}
static unsigned pcm_open(void *object,const uint64_t *parameter) {
    pthread_mutex_lock(&pcm_lock);
    if(pcm.object)hy_fail("multiple PCM AudioOut instances unsupported");
    unsigned rate=(uint32_t)*parameter,channels=(uint16_t)(*parameter>>32);
    if(!rate)rate=48000;if(!channels)channels=2;
    if((rate!=32000 && rate!=48000) || (channels!=1 && channels!=2))hy_fail("unsupported PCM AudioOut rate=%u channels=%u",rate,channels);
    memset(&pcm,0,sizeof pcm);pcm.object=object;pcm.channels=channels;pcm.rate=rate;
    AudioComponentDescription description={kAudioUnitType_Output,
#if TARGET_OS_IPHONE
        kAudioUnitSubType_RemoteIO,
#else
        kAudioUnitSubType_DefaultOutput,
#endif
        kAudioUnitManufacturer_Apple,0,0};
    AudioComponent component=AudioComponentFindNext(NULL,&description);
    if(!component || AudioComponentInstanceNew(component,&pcm.unit))hy_fail("PCM AudioUnit creation failed");
    AudioStreamBasicDescription format={rate,kAudioFormatLinearPCM,kAudioFormatFlagIsSignedInteger|kAudioFormatFlagIsPacked,channels*2,1,channels*2,channels,16,0};
    AURenderCallbackStruct callback={pcm_consume,NULL};
    if(AudioUnitSetProperty(pcm.unit,kAudioUnitProperty_StreamFormat,kAudioUnitScope_Input,0,&format,sizeof format) ||
       AudioUnitSetProperty(pcm.unit,kAudioUnitProperty_SetRenderCallback,kAudioUnitScope_Input,0,&callback,sizeof callback) || AudioUnitInitialize(pcm.unit))hy_fail("PCM AudioUnit initialization failed");
    uint32_t fields[]={rate,channels,2};memcpy(object,fields,sizeof fields);
    fprintf(stderr,"AudioOut: Apple PCM output rate=%u channels=%u\n",rate,channels);
    pthread_mutex_unlock(&pcm_lock);return 0;
}
static unsigned pcm_append(void *object,PcmBuffer *buffer) {
    pthread_mutex_lock(&pcm_lock);pcm_object(object);
    if(!buffer || !buffer->data || !buffer->size || buffer->size>buffer->capacity || buffer->size%(pcm.channels*2))hy_fail("invalid PCM buffer");
    uint64_t tail=atomic_load_explicit(&pcm.written,memory_order_relaxed),released=atomic_load_explicit(&pcm.released,memory_order_acquire);
    if(tail-released==PCM_QUEUE)hy_fail("PCM AudioOut queue full");
    for(uint64_t i=released;i<tail;i++)if(pcm.queue[i%PCM_QUEUE].buffer==buffer)hy_fail("PCM buffer already queued");
    pcm.queue[tail%PCM_QUEUE]=(PcmEntry){buffer,buffer->data,buffer->size/(pcm.channels*2)};
    atomic_store_explicit(&pcm.written,tail+1,memory_order_release);pthread_mutex_unlock(&pcm_lock);return 0;
}
static PcmBuffer *pcm_release(void *object) {
    pthread_mutex_lock(&pcm_lock);pcm_object(object);
    uint64_t read=atomic_load_explicit(&pcm.released,memory_order_relaxed),head=atomic_load_explicit(&pcm.consumed,memory_order_acquire);
    PcmBuffer *buffer=NULL;if(read<head){buffer=pcm.queue[read%PCM_QUEUE].buffer;atomic_store_explicit(&pcm.released,read+1,memory_order_release);}
    pthread_mutex_unlock(&pcm_lock);return buffer;
}
static unsigned pcm_start(void *object) {
    pthread_mutex_lock(&pcm_lock);pcm_object(object);
    if(AudioOutputUnitStart(pcm.unit))hy_fail("PCM AudioUnit start failed");atomic_store_explicit(&pcm.running,1,memory_order_release);
    pthread_mutex_unlock(&pcm_lock);return 0;
}
static void pcm_stop(void *object) {
    pthread_mutex_lock(&pcm_lock);pcm_object(object);atomic_store_explicit(&pcm.running,0,memory_order_release);
    if(AudioOutputUnitStop(pcm.unit))hy_fail("PCM AudioUnit stop failed");pthread_mutex_unlock(&pcm_lock);
}
static void pcm_close(void *object) {
    pcm_stop(object);pthread_mutex_lock(&pcm_lock);
    AudioUnitUninitialize(pcm.unit);AudioComponentInstanceDispose(pcm.unit);memset(&pcm,0,sizeof pcm);pthread_mutex_unlock(&pcm_lock);
}
void *hy_pcm_service(const char *name) {
    static const struct {const char *name;void *fn;} functions[]={
        {"_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutERKNS0_17AudioOutParameterE",pcm_open},
        {"_ZN2nn5audio20AppendAudioOutBufferEPNS0_8AudioOutEPNS0_14AudioOutBufferE",pcm_append},
        {"_ZN2nn5audio25GetReleasedAudioOutBufferEPNS0_8AudioOutE",pcm_release},
        {"_ZN2nn5audio13StartAudioOutEPNS0_8AudioOutE",pcm_start},
        {"_ZN2nn5audio12StopAudioOutEPNS0_8AudioOutE",pcm_stop},
        {"_ZN2nn5audio13CloseAudioOutEPNS0_8AudioOutE",pcm_close},
    };
    for(size_t i=0;i<sizeof functions/sizeof functions[0];i++)if(!strcmp(name,functions[i].name))return functions[i].fn;
    return NULL;
}
