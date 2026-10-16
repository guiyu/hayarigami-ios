#include "output.h"
#include <AudioToolbox/AudioToolbox.h>
#include <TargetConditionals.h>
#include <stdatomic.h>
#include <string.h>

// One SDK renderer producer and one AudioUnit consumer. No allocation, locks or
// guest code runs inside the system's realtime audio callback.
enum { CAPACITY = 8192 };
static int16_t samples[CAPACITY * 2];
static _Atomic uint64_t read_frame, write_frame;
static AudioUnit unit;
static size_t target_frames;
static OSStatus consume(void *context, AudioUnitRenderActionFlags *flags,
                        const AudioTimeStamp *time, UInt32 bus, UInt32 frames, AudioBufferList *buffers) {
    (void)context; (void)flags; (void)time; (void)bus;
    if (buffers->mNumberBuffers != 1 || buffers->mBuffers[0].mDataByteSize < frames * 4u)
        return kAudio_ParamError;
    int16_t *out = buffers->mBuffers[0].mData;
    uint64_t read = atomic_load_explicit(&read_frame, memory_order_relaxed);
    uint64_t write = atomic_load_explicit(&write_frame, memory_order_acquire);
    size_t available = write - read;
    size_t count = available < frames ? available : frames;
    for (size_t i = 0; i < count; i++) {
        size_t offset = ((read + i) & (CAPACITY - 1)) * 2;
        out[i * 2] = samples[offset]; out[i * 2 + 1] = samples[offset + 1];
    }
    memset(out + count * 2, 0, (frames - count) * 4);
    atomic_store_explicit(&read_frame, read + count, memory_order_release);
    return noErr;
}
int hy_audio_output_open(void) {
    if (unit) return -1;
    AudioComponentDescription description = {kAudioUnitType_Output,
#if TARGET_OS_IPHONE
        kAudioUnitSubType_RemoteIO,
#else
        kAudioUnitSubType_DefaultOutput,
#endif
        kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponent component = AudioComponentFindNext(NULL, &description);
    if (!component || AudioComponentInstanceNew(component, &unit)) return -1;
    AudioStreamBasicDescription format = {48000, kAudioFormatLinearPCM,
        kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked, 4, 1, 4, 2, 16, 0};
    AURenderCallbackStruct callback = {consume, NULL};
    OSStatus result = AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof format);
    if (!result) result = AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof callback);
    if (!result) result = AudioUnitInitialize(unit);
    UInt32 maximum = 0, size = sizeof maximum;
    if (!result) result = AudioUnitGetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &maximum, &size);
    if (!result && (!maximum || maximum > CAPACITY - 720)) result = kAudio_ParamError;
    if (result) { hy_audio_output_close(); return (int)result; }
    target_frames = maximum + 480;
    atomic_store(&read_frame, 0); atomic_store(&write_frame, 0);
    return 0;
}
int hy_audio_output_start(void) { return unit ? (int)AudioOutputUnitStart(unit) : -1; }
size_t hy_audio_output_target(void) { return target_frames; }
void hy_audio_output_stop(void) {
    if (unit) AudioOutputUnitStop(unit);
    atomic_store(&read_frame, 0); atomic_store(&write_frame, 0);
}
size_t hy_audio_output_pending(void) {
    uint64_t write = atomic_load_explicit(&write_frame, memory_order_relaxed);
    uint64_t read = atomic_load_explicit(&read_frame, memory_order_acquire);
    return (size_t)(write - read);
}
int hy_audio_output_push(const int16_t *input, size_t frames) {
    uint64_t write = atomic_load_explicit(&write_frame, memory_order_relaxed);
    uint64_t read = atomic_load_explicit(&read_frame, memory_order_acquire);
    if (frames > CAPACITY - (write - read)) return -1;
    for (size_t i = 0; i < frames; i++) {
        size_t offset = ((write + i) & (CAPACITY - 1)) * 2;
        samples[offset] = input[i * 2]; samples[offset + 1] = input[i * 2 + 1];
    }
    atomic_store_explicit(&write_frame, write + frames, memory_order_release);
    return 0;
}
void hy_audio_output_close(void) {
    if (!unit) return;
    hy_audio_output_stop(); AudioUnitUninitialize(unit); AudioComponentInstanceDispose(unit); unit = NULL;
}
