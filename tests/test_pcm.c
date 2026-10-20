#include "../src/audio/pcm.c"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
_Noreturn void hy_fail(const char *format,...) {(void)format;abort();}
int main(void) {
    uint32_t object[3]={48000,2,2};pcm.object=object;pcm.channels=2;pcm.rate=48000;atomic_store(&pcm.running,1);
    int16_t a[]={1,-1,2,-2,3,-3},b[]={4,-4,5,-5},out[8];
    PcmBuffer first={NULL,a,sizeof a,sizeof a},second={NULL,b,sizeof b,sizeof b};
    assert(!pcm_append(object,&first));assert(!pcm_append(object,&second));assert(!pcm_release(object));
    AudioBufferList list={.mNumberBuffers=1,.mBuffers={{2,sizeof out,out}}};
    assert(!pcm_consume(NULL,NULL,NULL,0,2,&list));assert(!memcmp(out,a,8));assert(!pcm_release(object));
    assert(!pcm_consume(NULL,NULL,NULL,0,4,&list));
    int16_t expected[]={3,-3,4,-4,5,-5,0,0};assert(!memcmp(out,expected,sizeof out));
    assert(pcm_release(object)==&first);assert(pcm_release(object)==&second);assert(!pcm_release(object));
    for(int i=0;i<200;i++) {
        assert(!pcm_append(object,&first));assert(!pcm_consume(NULL,NULL,NULL,0,3,&list));assert(pcm_release(object)==&first);
    }
    assert(hy_pcm_service("_ZN2nn5audio19OpenDefaultAudioOutEPNS0_8AudioOutERKNS0_17AudioOutParameterE"));
    puts("PASS: PCM consumption, partial buffers, release order, underrun and ring wrap");
}
