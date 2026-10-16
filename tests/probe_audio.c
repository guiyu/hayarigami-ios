// Executes the game's own CPU AudioRenderer in isolation from nnMain.
// This probe is not enabled in the game host and cannot count as game playback.
#include "../src/runtime/runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct { uint64_t object, event; } Handle;
typedef struct {
    void *data; uint64_t bytes; int32_t start, end;
    uint8_t loop, last, reserved[6]; void *context; uint64_t context_size;
    int32_t loop_start, loop_end, loop_count;
} Wave;
static void *symbol(const char *name) {
    void *p = hy_symbol(name);
    if (!p) hy_fail("audio export missing: %s", name);
    return p;
}
#define FN(type, name) ((type)symbol(name))
static void *allocate(size_t size) {
    void *p = NULL;
    if (posix_memalign(&p, 4096, (size + 4095) & ~(size_t)4095)) hy_fail("audio allocation");
    memset(p, 0, size); return p;
}
static void check(unsigned result, const char *step) {
    if (result) hy_fail("%s: result 0x%x", step, result);
}
static void empty(void) {}
static int channel_count = 2;
static void probe(void) {
    for (size_t i = 0; i < hy_module_count; i++) {
        HyModule *m = &hy_modules[i];
        if (m->dynamic[12]) ((void (*)(void))hy_range(m, m->dynamic[12], 4))();
    }
    // SDK 14.3.0 public parameter layout, observed from this game's call.
    // Rendering device 1 selects the SDK CPU renderer; execution mode 1 is manual.
    uint32_t parameter[14] = {48000, 240, 66, 10, 1024, 1, 16, 0, 0, 0, 0, 1, 1, 0x3b564552};
    uint64_t (*size_fn)(const void *) = hy_symbol("_ZN2nn5audio30GetAudioRendererWorkBufferSizeERKNS0_22AudioRendererParameterE");
    unsigned (*open_fn)(void *, const void *, void *, size_t) = hy_symbol("_ZN2nn5audio17OpenAudioRendererEPNS0_19AudioRendererHandleERKNS0_22AudioRendererParameterEPvm");
    if (!size_fn || !open_fn) hy_fail("audio probe exports missing");
    size_t size = size_fn(parameter);
    fprintf(stderr, "CPU AudioRenderer work size: %zu\n", size);
    if (!size || size > 64 * 1024 * 1024) hy_fail("unexpected renderer work size");
    void *work = NULL;
    if (posix_memalign(&work, 4096, size)) hy_fail("audio work allocation");
    Handle handle = {0};
    unsigned result = open_fn(&handle, parameter, work, size);
    fprintf(stderr, "CPU AudioRenderer open result: 0x%x\n", result);
    if (result) hy_fail("CPU renderer open failed");
    void *config = allocate(4096), *final = allocate(4096), *sink = allocate(4096);
    void *voice = allocate(4096), *pool = allocate(4096), *sink_pool = allocate(4096);
    size_t config_size = FN(size_t (*)(void *), "_ZN2nn5audio36GetAudioRendererConfigWorkBufferSizeERKNS0_22AudioRendererParameterE")(parameter);
    void *config_work = allocate(config_size);
    FN(void (*)(void *, void *, void *, size_t), "_ZN2nn5audio29InitializeAudioRendererConfigEPNS0_19AudioRendererConfigERKNS0_22AudioRendererParameterEPvm")(config, parameter, config_work, config_size);
    if (!FN(int (*)(void *, void *, int), "_ZN2nn5audio15AcquireFinalMixEPNS0_19AudioRendererConfigEPNS0_12FinalMixTypeEi")(config, final, channel_count)) hy_fail("final mix acquisition");
    int16_t *pcm = allocate(4096), *output = allocate(32768);
    size_t sink_bytes = (size_t)channel_count * 240 * 2 * 5;
    memset((uint8_t *)output + sink_bytes, 0xa5, 32768 - sink_bytes); // Detect uninstrumented guest writes past the sink.
    for (int i = 0; i < 2048; i++) pcm[i] = 1000;
    int (*acquire_pool)(void *, void *, void *, size_t) = symbol("_ZN2nn5audio17AcquireMemoryPoolEPNS0_19AudioRendererConfigEPNS0_14MemoryPoolTypeEPvm");
    int (*attach_pool)(void *) = symbol("_ZN2nn5audio23RequestAttachMemoryPoolEPNS0_14MemoryPoolTypeE");
    if (!acquire_pool(config, pool, pcm, 4096) || !acquire_pool(config, sink_pool, output, 16384)) hy_fail("memory pool acquisition");
    if (!attach_pool(pool) || !attach_pool(sink_pool)) hy_fail("memory pool attachment request");
    int8_t channels[] = {0, 1, 2, 3, 4, 5};
    check(FN(unsigned (*)(void *, void *, void *, void *, int, void *, size_t, int), "_ZN2nn5audio21AddCircularBufferSinkEPNS0_19AudioRendererConfigEPNS0_22CircularBufferSinkTypeEPNS0_12FinalMixTypeEPKaiPvmNS0_12SampleFormatE")(config, sink, final, channels, channel_count, output, sink_bytes, 2), "circular sink");
    if (!FN(int (*)(void *, void *, int, int, int, int, void *, size_t), "_ZN2nn5audio16AcquireVoiceSlotEPNS0_19AudioRendererConfigEPNS0_9VoiceTypeEiiNS0_12SampleFormatEiPKvm")(config, voice, 48000, 1, 2, 0, NULL, 0)) hy_fail("voice acquisition");
    FN(void (*)(void *, void *, void *), "_ZN2nn5audio19SetVoiceDestinationEPNS0_19AudioRendererConfigEPNS0_9VoiceTypeEPNS0_12FinalMixTypeE")(config, voice, final);
    void (*volume)(void *, void *, float, int, int) = symbol("_ZN2nn5audio17SetVoiceMixVolumeEPNS0_9VoiceTypeEPNS0_12FinalMixTypeEfii");
    volume(voice, final, 1.0f, 0, 0); volume(voice, final, 0.5f, 0, 1);
    Wave wave = {.data = pcm, .bytes = 4096, .end = 2048, .last = 1};
    if (!FN(int (*)(void *, void *), "_ZN2nn5audio16AppendWaveBufferEPNS0_9VoiceTypeEPKNS0_10WaveBufferE")(voice, &wave)) hy_fail("append PCM wave");
    FN(void (*)(void *, int), "_ZN2nn5audio17SetVoicePlayStateEPNS0_9VoiceTypeENS1_9PlayStateE")(voice, 0);
    unsigned (*update)(Handle, void *) = symbol("_ZN2nn5audio26RequestUpdateAudioRendererENS0_19AudioRendererHandleEPKNS0_19AudioRendererConfigE");
    unsigned (*render)(Handle) = symbol("_ZN2nn5audio29ExecuteAudioRendererRenderingENS0_19AudioRendererHandleE");
    check(update(handle, config), "initial update");
    check(FN(unsigned (*)(Handle), "_ZN2nn5audio18StartAudioRendererENS0_19AudioRendererHandleE")(handle), "renderer start");
    int16_t captured[5760] = {0}; size_t captured_bytes = 0;
    size_t tick_bytes = (size_t)channel_count * 240 * 2;
    size_t (*read_sink)(void *, void *, size_t) = symbol("_ZN2nn5audio22ReadCircularBufferSinkEPNS0_22CircularBufferSinkTypeEPvm");
    for (int i = 0; i < 4; i++) {
        check(render(handle), "render"); check(update(handle, config), "update");
        size_t count = read_sink(sink, (char *)captured + captured_bytes, tick_bytes);
        if (count != (i < 2 ? 0 : tick_bytes)) hy_fail("circular sink API returned unexpected tick: %zu", count);
        captured_bytes += count;
    }
    // Circular sinks write each tick as planar channels. Skip the first tick's
    // resampler startup and allow 1% unity-gain error from its finite FIR filter.
    // This checks actual PCM and routing, not bit-exact Switch resampling.
    for (int tick = 1; tick < 4; tick++) for (int i = 0; i < 240; i++) {
        int left = output[tick * channel_count * 240 + i], right = output[tick * channel_count * 240 + 240 + i];
        if (left < 990 || left > 1010 || abs(left - 2 * right) > 1)
            hy_fail("CPU renderer PCM routing/gain mismatch: %d %d", left, right);
    }
    // Sink status crosses an extra staging buffer and arrives two ticks late.
    if (captured_bytes != tick_bytes * 2 || memcmp(captured, output, captured_bytes)) hy_fail("circular sink read differs from rendered PCM");
    uint64_t played = FN(uint64_t (*)(void *), "_ZN2nn5audio25GetVoicePlayedSampleCountEPKNS0_9VoiceTypeE")(voice);
    // Renderer status is reported one tick behind the generated sink data.
    if (played != 720) hy_fail("CPU renderer played sample count: %llu", (unsigned long long)played);
    for (int tick = 0; tick < 120; tick++) {
        check(render(handle), "wrap render"); check(update(handle, config), "wrap update");
        read_sink(sink, captured, tick_bytes);
        for (size_t i = sink_bytes; i < 32768; i++)
            if (((uint8_t *)output)[i] != 0xa5) hy_fail("circular sink overrun at byte %zu on wrap tick %d", i, tick);
    }
    FN(void (*)(Handle), "_ZN2nn5audio17StopAudioRendererENS0_19AudioRendererHandleE")(handle);
    FN(void (*)(Handle), "_ZN2nn5audio18CloseAudioRendererENS0_19AudioRendererHandleE")(handle);
    printf("PASS: SDK CPU renderer produced 960 frames (%d channels) with 2:1 gains; circular sink API returned 480 identical frames; 120 extra ticks wrapped without overwriting guards\n", channel_count);
    exit(0);
}
int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[2], "--six-channels")) channel_count = 6;
    else if (argc != 2) return 2;
    hy_install_signals(); alarm(30);
    hy_load(argv[1], 0); hy_thread_init(); hy_thread_main();
    void (*start)(uint64_t, uint64_t, void (*)(void), void (*)(void), void (*)(void)) =
        hy_symbol("_ZN2nn4init5StartEmmPFvvES2_S2_");
    if (!start) hy_fail("SDK Start export missing");
    start(0x100, 0, empty, probe, empty);
    return 1;
}
