#include "../src/audio/renderer.c"
#include <assert.h>
#include <stdarg.h>
static unsigned updates, executions, reads, output_starts, output_stops, closes, foreign_destroys;
static size_t queued;
int hy_graphics_event(void *event, int operation) { (void)event; (void)operation; return -1; }
_Noreturn void hy_fail(const char *format, ...) {
    va_list ap; va_start(ap, format); vfprintf(stderr, format, ap); va_end(ap); abort();
}
static void parameter_check(const uint32_t *p) { assert(p[11] == 1 && p[12] == 1 && p[0] == 48000); }
static unsigned sdk_open(Handle *h, const uint32_t *p, void *work, size_t size) {
    (void)work; assert(size == 4096); parameter_check(p); *h = (Handle){123, 0}; return 0;
}
static void sdk_config(void *config, const uint32_t *p, void *work, size_t size) {
    (void)config; (void)work; assert(size == 4096); parameter_check(p);
}
static int sdk_pool(void *config, uint64_t *pool, void *data, size_t size) {
    assert(config && data && size == 16384); *pool = 456; return 1;
}
static int sdk_attach(uint64_t *pool) { assert(*pool == 456); return 1; }
static unsigned sdk_sink(void *config, void *sink, void *final, const int8_t *indices, int count, void *data, size_t size, int format) {
    assert(config && sink && final && indices && data && size == 14400 && count == 6 && format == 2); return 0;
}
static unsigned sdk_update(Handle h, void *config) { assert(h.object == 123 && config); updates++; return 0; }
static unsigned sdk_start(Handle h) { assert(h.object == 123); return 0; }
static void sdk_stop(Handle h) { assert(h.object == 123); }
static unsigned sdk_render(Handle h) { assert(h.object == 123); executions++; return 0; }
static size_t sdk_read(void *sink, int16_t *out, size_t size) {
    assert(sink && size == 2880); reads++;
    if (reads < 3) return 0;
    memset(out, 0, size);
    for (int i = 0; i < 240; i++) out[480 + i] = 1000; // Centre-only dialogue.
    return size;
}
static void sdk_close(Handle h) { assert(h.object == 123); closes++; }
static void sdk_destroy(void *event) { assert(event); foreign_destroys++; }
void *hy_symbol(const char *name) {
    if (strstr(name, "OpenAudioRenderer")) return sdk_open;
    if (strstr(name, "InitializeAudioRendererConfig")) return sdk_config;
    if (strstr(name, "AcquireMemoryPool")) return sdk_pool;
    if (strstr(name, "RequestAttachMemoryPool")) return sdk_attach;
    if (strstr(name, "AddCircularBufferSink")) return sdk_sink;
    if (strstr(name, "RequestUpdateAudioRenderer")) return sdk_update;
    if (strstr(name, "StartAudioRenderer")) return sdk_start;
    if (strstr(name, "StopAudioRenderer")) return sdk_stop;
    if (strstr(name, "ExecuteAudioRendererRendering")) return sdk_render;
    if (strstr(name, "ReadCircularBufferSink")) return sdk_read;
    if (strstr(name, "CloseAudioRenderer")) return sdk_close;
    if (strstr(name, "DestroySystemEvent")) return sdk_destroy;
    abort();
}
int hy_audio_output_open(void) { return 0; }
int hy_audio_output_start(void) { output_starts++; return 0; }
void hy_audio_output_stop(void) { output_stops++; queued = 0; }
void hy_audio_output_close(void) { hy_audio_output_stop(); }
size_t hy_audio_output_pending(void) { return queued; }
size_t hy_audio_output_target(void) { return 1504; }
int hy_audio_output_push(const int16_t *data, size_t frames) {
    assert(frames == 240);
    for (size_t i = 0; i < frames * 2; i++) assert(data[i] == 500);
    queued += frames; return 0;
}
static void *wait_thread(void *event) { wait_event(event); return NULL; }
int main(void) {
    uint32_t p[14] = {48000, 240, 66, 10, 1024, 1, 16, 0, 0, 0, 0, 0, 0, 0x3b564552};
    uint64_t event = 0, config = 0, sink = 0, final = 0;
    int8_t channels[] = {0, 1, 2, 3, 4, 5}; Handle handle;
    assert(!open_renderer(&handle, &event, p, NULL, 4096));
    config_init(&config, p, NULL, 4096);
    assert(p[11] == 0 && p[12] == 0); // Caller parameter remains unchanged.
    assert(!add_sink(&config, &sink, &final, channels, 6, "MainAudioOut"));
    assert(!update_renderer(handle, &config) && executions == 0);
    assert(!start_renderer(handle));
    for (int i = 0; i < 9; i++) assert(!update_renderer(handle, &config));
    assert(updates == 10 && executions == 9 && output_starts == 1 && queued == 1680);
    stop_renderer(handle); assert(queued == 0);
    close_renderer(handle);
    assert(closes == 1 && !renderer.opened);
    destroy_event(&event); // Closing first must still retain the host-owned event.
    assert(foreign_destroys == 0 && renderer.event == NULL);
    int foreign; destroy_event(&foreign); assert(foreign_destroys == 1);
    assert(!open_renderer(&handle, &event, p, NULL, 4096));
    renderer.running = 1; queued = 2000;
    pthread_t waiter; assert(!pthread_create(&waiter, NULL, wait_thread, &event));
    struct timespec delay = {0, 2000000}; nanosleep(&delay, NULL);
    close_renderer(handle); // A pending wait must also finish without a prior stop.
    assert(!pthread_join(waiter, NULL));
    wait_event(&event); // Closed host event must not fall back to uninitialized SDK storage.
    destroy_event(&event);
    puts("PASS: CPU parameter routing, delayed sink reads, centre downmix, output priming and event/renderer cleanup");
}
