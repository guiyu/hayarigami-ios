// Retain SDK mixing, effects, codecs and voice lifetime. Select its built-in CPU
// manual renderer and route its circular PCM sink to the Apple output device.
#include "../runtime/runtime.h"
#include "output.h"
#include "mix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { uint64_t object, event; } Handle;
static struct {
    Handle handle;
    void *event, *config, *sink;
    uint64_t pool;
    void *pcm;
    int opened, running, output_started, channels;
} renderer;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void *symbol(const char *name) {
    void *p = hy_symbol(name);
    if (!p) hy_fail("required audio export missing: %s", name);
    return p;
}
#define FN(type, name) ((type)symbol(name))
static void cpu_parameter(uint32_t out[14], const uint32_t *in) {
    memcpy(out, in, 56);
    if (in[0] != 48000 || in[1] != 240 || in[5] != 1 || in[13] != 0x3b564552)
        hy_fail("unsupported AudioRenderer parameter");
    out[11] = 1; out[12] = 1;
}
static uint64_t work_size(const uint32_t *in) {
    uint32_t p[14]; cpu_parameter(p, in);
    return FN(uint64_t (*)(const void *), "_ZN2nn5audio30GetAudioRendererWorkBufferSizeERKNS0_22AudioRendererParameterE")(p);
}
static uint64_t config_size(const uint32_t *in) {
    uint32_t p[14]; cpu_parameter(p, in);
    return FN(uint64_t (*)(const void *), "_ZN2nn5audio36GetAudioRendererConfigWorkBufferSizeERKNS0_22AudioRendererParameterE")(p);
}
static unsigned open_renderer(Handle *out, void *event, const uint32_t *in, void *work, size_t size) {
    pthread_mutex_lock(&lock);
    if (renderer.opened || renderer.event) hy_fail("previous AudioRenderer or event is still open");
    uint32_t p[14]; cpu_parameter(p, in);
    unsigned result = FN(unsigned (*)(Handle *, const void *, void *, size_t), "_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleERKNS0_22AudioRendererParameterEPvm")(out, p, work, size);
    if (result) { pthread_mutex_unlock(&lock); return result; }
    renderer.handle = *out; renderer.event = event; renderer.opened = 1;
    if (hy_audio_output_open()) hy_fail("Apple audio output initialization failed");
    fputs("AudioRenderer: SDK CPU mixer with Apple stereo output\n", stderr);
    pthread_mutex_unlock(&lock); return 0;
}
static void config_init(void *config, const uint32_t *in, void *work, size_t size) {
    pthread_mutex_lock(&lock);
    uint32_t p[14]; cpu_parameter(p, in);
    FN(void (*)(void *, const void *, void *, size_t), "_ZN2nn5audio29InitializeAudioRendererConfigEPNS0_19AudioRendererConfigERKNS0_22AudioRendererParameterEPvm")(config, p, work, size);
    renderer.config = config;
    pthread_mutex_unlock(&lock);
}
static unsigned add_sink(void *config, void *sink, void *final, const int8_t *indices, int count, const char *name) {
    (void)name;
    pthread_mutex_lock(&lock);
    if (!renderer.opened || renderer.sink || (count != 2 && count != 6)) hy_fail("unsupported audio device sink: channels=%d", count);
    if (posix_memalign(&renderer.pcm, 4096, 16384)) hy_fail("audio sink allocation");
    if (!FN(int (*)(void *, void *, void *, size_t), "_ZN2nn5audio17AcquireMemoryPoolEPNS0_19AudioRendererConfigEPNS0_14MemoryPoolTypeEPvm")(config, &renderer.pool, renderer.pcm, 16384)) hy_fail("audio sink pool acquisition");
    if (!FN(int (*)(void *), "_ZN2nn5audio23RequestAttachMemoryPoolEPNS0_14MemoryPoolTypeE")(&renderer.pool)) hy_fail("audio sink pool attachment");
    // The CPU sink wraps after each whole channel block, never mid-block.
    // Passing the page allocation size (16384) overruns on the first wrap.
    size_t sink_bytes = (size_t)count * 240 * sizeof(int16_t) * 5;
    unsigned result = FN(unsigned (*)(void *, void *, void *, const int8_t *, int, void *, size_t, int), "_ZN2nn5audio21AddCircularBufferSinkEPNS0_19AudioRendererConfigEPNS0_22CircularBufferSinkTypeEPNS0_12FinalMixTypeEPKaiPvmNS0_12SampleFormatE")(config, sink, final, indices, count, renderer.pcm, sink_bytes, 2);
    // A failed sink leaves an acquired pool in the SDK config. Do not permit a
    // retry to overwrite its ownership; initialization cannot continue safely.
    if (result) hy_fail("audio circular sink creation failed: %#x", result);
    renderer.sink = sink; renderer.channels = count;
    pthread_mutex_unlock(&lock); return result;
}
static unsigned start_renderer(Handle handle) {
    pthread_mutex_lock(&lock);
    if (handle.object != renderer.handle.object || !renderer.sink) hy_fail("invalid audio renderer start");
    unsigned result = FN(unsigned (*)(Handle), "_ZN2nn5audio18StartAudioRendererENS0_19AudioRendererHandleE")(handle);
    if (!result) renderer.running = 1;
    pthread_mutex_unlock(&lock); return result;
}
static unsigned update_renderer(Handle handle, void *config) {
    pthread_mutex_lock(&lock);
    if (handle.object != renderer.handle.object || config != renderer.config) hy_fail("invalid renderer update");
    unsigned result = FN(unsigned (*)(Handle, void *), "_ZN2nn5audio26RequestUpdateAudioRendererENS0_19AudioRendererHandleEPKNS0_19AudioRendererConfigE")(handle, config);
    if (result || !renderer.running) { pthread_mutex_unlock(&lock); return result; }
    result = FN(unsigned (*)(Handle), "_ZN2nn5audio29ExecuteAudioRendererRenderingENS0_19AudioRendererHandleE")(handle);
    if (result) { pthread_mutex_unlock(&lock); return result; }
    int16_t planar[1440], stereo[480];
    size_t expected = (size_t)renderer.channels * 240 * 2;
    size_t bytes = FN(size_t (*)(void *, void *, size_t), "_ZN2nn5audio22ReadCircularBufferSinkEPNS0_22CircularBufferSinkTypeEPvm")(renderer.sink, planar, expected);
    if (bytes && bytes != expected) hy_fail("incomplete renderer PCM tick: %zu bytes", bytes);
    if (bytes) {
        if (hy_audio_stereo(planar, renderer.channels, 240, stereo)) hy_fail("unsupported PCM channel layout");
        if (hy_audio_output_push(stereo, 240)) hy_fail("audio output queue overflow");
        if (!renderer.output_started && hy_audio_output_pending() >= hy_audio_output_target()) {
            if (hy_audio_output_start()) hy_fail("Apple audio output start failed");
            renderer.output_started = 1;
        }
    }
    pthread_mutex_unlock(&lock); return 0;
}
static void wait_event(void *event) {
    if (hy_graphics_event(event, 0) >= 0) return;
    pthread_mutex_lock(&lock);
    int matches = event && event == renderer.event;
    pthread_mutex_unlock(&lock);
    if (!matches) {
        FN(void (*)(void *), "_ZN2nn2os15WaitSystemEventEPNS0_15SystemEventTypeE")(event); return;
    }
    // Pace production by frames actually consumed, avoiding timer/device drift.
    struct timespec pause = {0, 1000000};
    unsigned waits = 0;
    for (;;) {
        pthread_mutex_lock(&lock);
        int ready = !renderer.running || !renderer.opened ||
            hy_audio_output_pending() < hy_audio_output_target();
        pthread_mutex_unlock(&lock);
        if (ready) break;
        if (++waits > 5000) hy_fail("audio output stalled");
        nanosleep(&pause, NULL);
    }
}
static void destroy_event(void *event) {
    if (hy_graphics_event(event, 3) >= 0) return;
    pthread_mutex_lock(&lock);
    int matches = event && event == renderer.event;
    if (matches) {
        renderer.event = NULL; renderer.running = 0; renderer.output_started = 0;
        hy_audio_output_stop();
    }
    pthread_mutex_unlock(&lock);
    if (!matches) FN(void (*)(void *), "_ZN2nn2os18DestroySystemEventEPNS0_15SystemEventTypeE")(event);
}
static void stop_renderer(Handle handle) {
    pthread_mutex_lock(&lock);
    if (!renderer.opened || handle.object != renderer.handle.object) hy_fail("invalid renderer stop");
    // SDK 14.3.0 aborts internally on failure; this public wrapper returns void.
    FN(void (*)(Handle), "_ZN2nn5audio17StopAudioRendererENS0_19AudioRendererHandleE")(handle);
    renderer.running = 0; renderer.output_started = 0; hy_audio_output_stop();
    pthread_mutex_unlock(&lock);
}
static void close_renderer(Handle handle) {
    pthread_mutex_lock(&lock);
    if (!renderer.opened || handle.object != renderer.handle.object) hy_fail("invalid renderer close");
    renderer.running = 0;
    hy_audio_output_close();
    FN(void (*)(Handle), "_ZN2nn5audio18CloseAudioRendererENS0_19AudioRendererHandleE")(handle);
    free(renderer.pcm);
    void *event = renderer.event;
    memset(&renderer, 0, sizeof renderer);
    renderer.event = event; // The caller may destroy its event after closing.
    pthread_mutex_unlock(&lock);
}
void *hy_audio_service(const char *name) {
    static const struct { const char *name; void *fn; } functions[] = {
        {"_ZN2nn5audio30GetAudioRendererWorkBufferSizeERKNS0_22AudioRendererParameterE", (void *)work_size},
        {"_ZN2nn5audio36GetAudioRendererConfigWorkBufferSizeERKNS0_22AudioRendererParameterE", (void *)config_size},
        {"_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleEPNS_2os11SystemEventERKNS0_22AudioRendererParameterEPvm", (void *)open_renderer},
        {"_ZN2nn5audio29InitializeAudioRendererConfigEPNS0_19AudioRendererConfigERKNS0_22AudioRendererParameterEPvm", (void *)config_init},
        {"_ZN2nn5audio13AddDeviceSinkEPNS0_19AudioRendererConfigEPNS0_14DeviceSinkTypeEPNS0_12FinalMixTypeEPKaiPKc", (void *)add_sink},
        {"_ZN2nn5audio18StartAudioRendererENS0_19AudioRendererHandleE", (void *)start_renderer},
        {"_ZN2nn5audio26RequestUpdateAudioRendererENS0_19AudioRendererHandleEPKNS0_19AudioRendererConfigE", (void *)update_renderer},
        {"_ZN2nn2os15WaitSystemEventEPNS0_15SystemEventTypeE", (void *)wait_event},
        {"_ZN2nn2os18DestroySystemEventEPNS0_15SystemEventTypeE", (void *)destroy_event},
        {"_ZN2nn5audio17StopAudioRendererENS0_19AudioRendererHandleE", (void *)stop_renderer},
        {"_ZN2nn5audio18CloseAudioRendererENS0_19AudioRendererHandleE", (void *)close_renderer},
    };
    for (size_t i = 0; i < sizeof functions / sizeof functions[0]; i++)
        if (!strcmp(name, functions[i].name)) return functions[i].fn;
    return NULL;
}
