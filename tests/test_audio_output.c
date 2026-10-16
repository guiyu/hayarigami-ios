#include "../src/audio/output.c"
#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>
enum { STRESS_FRAMES = 100000 };
static void *produce(void *unused) {
    (void)unused;
    for (int i = 0; i < STRESS_FRAMES; i++) {
        int16_t frame[] = {(int16_t)(i % 1000 + 1), -(int16_t)(i % 1000 + 1)};
        while (hy_audio_output_push(frame, 1)) sched_yield();
    }
    return NULL;
}
int main(void) {
    int16_t input[CAPACITY * 2], output[CAPACITY * 2];
    for (size_t i = 0; i < CAPACITY; i++) { input[i * 2] = (int16_t)i; input[i * 2 + 1] = -(int16_t)i; }
    AudioBufferList list = {.mNumberBuffers = 1, .mBuffers = {{2, sizeof output, output}}};
    assert(hy_audio_output_push(input, CAPACITY) == 0);
    assert(hy_audio_output_push(input, 1) == -1);
    assert(consume(NULL, NULL, NULL, 0, 4096, &list) == 0);
    assert(memcmp(input, output, 4096 * 4) == 0);
    assert(hy_audio_output_pending() == 4096);
    assert(hy_audio_output_push(input, 4096) == 0);
    assert(consume(NULL, NULL, NULL, 0, CAPACITY, &list) == 0);
    assert(memcmp(input + 8192, output, 4096 * 4) == 0);
    assert(memcmp(input, output + 8192, 4096 * 4) == 0);
    assert(hy_audio_output_pending() == 0);
    memset(output, 1, sizeof output);
    assert(consume(NULL, NULL, NULL, 0, CAPACITY, &list) == 0);
    for (size_t i = 0; i < CAPACITY * 2; i++) assert(output[i] == 0);
    for (size_t i = 0; i < CAPACITY * 2; i++) input[i] = 10000;
    unsigned quanta[] = {1024, 4096};
    for (int q = 0; q < 2; q++) {
        hy_audio_output_stop(); target_frames = quanta[q] + 480;
        for (int tick = 0; tick < 20; tick++) {
            while (hy_audio_output_pending() < hy_audio_output_target()) assert(!hy_audio_output_push(input, 240));
            assert(!consume(NULL, NULL, NULL, 0, quanta[q], &list));
            for (unsigned i = 0; i < quanta[q] * 2; i++) assert(output[i] == 10000);
        }
    }
    hy_audio_output_stop();
    pthread_t producer; assert(!pthread_create(&producer, NULL, produce, NULL));
    int consumed = 0;
    while (consumed < STRESS_FRAMES) {
        assert(!consume(NULL, NULL, NULL, 0, 257, &list));
        for (int i = 0; i < 257; i++) if (output[i * 2]) {
            assert(output[i * 2] == consumed % 1000 + 1);
            assert(output[i * 2 + 1] == -output[i * 2]); consumed++;
        }
    }
    assert(!pthread_join(producer, NULL));
    puts("PASS: stereo ring boundaries, 1024/4096-frame pacing and 100000 concurrent frames in order");
}
