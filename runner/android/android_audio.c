#include "android_audio.h"

#include <aaudio/AAudio.h>
#include <android/log.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#include "common_rtl.h"

#define LOG_TAG "DKC_AUDIO"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define GAME_HZ 60.098811862
#define AUDIO_CAPACITY 16384
#define MAX_FRAME_SAMPLES 4096

typedef struct {
    _Atomic uint32_t read;
    _Atomic uint32_t write;
    int16_t samples[AUDIO_CAPACITY * 2];
} AudioRing;

static AudioRing s_ring;
static AAudioStream *s_stream;
static double s_fraction;
static int s_sample_rate = 32040;
static bool s_initialized;
static bool s_paused;

static _Atomic bool s_restart_requested;
static int s_retry_frames;

static void ring_reset(void) {
    atomic_store(&s_ring.read, 0);
    atomic_store(&s_ring.write, 0);
}

static void ring_write(const int16_t *samples, uint32_t frames) {
    uint32_t write = atomic_load_explicit(&s_ring.write, memory_order_relaxed);
    uint32_t read = atomic_load_explicit(&s_ring.read, memory_order_acquire);

    uint32_t available = AUDIO_CAPACITY - (write - read);
    if (frames > available) frames = available;

    for (uint32_t i = 0; i < frames; ++i) {
        uint32_t index = ((write + i) % AUDIO_CAPACITY) * 2;
        s_ring.samples[index] = samples[i * 2];
        s_ring.samples[index + 1] = samples[i * 2 + 1];
    }

    atomic_store_explicit(&s_ring.write, write + frames, memory_order_release);
}

static void ring_read(int16_t *output, uint32_t frames) {
    uint32_t read = atomic_load_explicit(&s_ring.read, memory_order_relaxed);
    uint32_t write = atomic_load_explicit(&s_ring.write, memory_order_acquire);

    uint32_t available = write - read;
    uint32_t count = available < frames ? available : frames;

    memset(output, 0, (size_t)frames * 2 * sizeof(int16_t));

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t index = ((read + i) % AUDIO_CAPACITY) * 2;
        output[i * 2] = s_ring.samples[index];
        output[i * 2 + 1] = s_ring.samples[index + 1];
    }

    atomic_store_explicit(&s_ring.read, read + count, memory_order_release);
}

static aaudio_data_callback_result_t audio_callback(
    AAudioStream *stream,
    void *user_data,
    void *audio_data,
    int32_t num_frames
) {
    (void)stream;
    (void)user_data;

    ring_read((int16_t *)audio_data, (uint32_t)num_frames);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void error_callback(
    AAudioStream *stream,
    void *user_data,
    aaudio_result_t error
) {
    (void)stream;
    (void)user_data;

    LOGE("AAudio stream error: %s", AAudio_convertResultToText(error));
    atomic_store(&s_restart_requested, true);
}

static bool open_and_start(void) {
    AAudioStreamBuilder *builder = NULL;
    aaudio_result_t result = AAudio_createStreamBuilder(&builder);

    if (result != AAUDIO_OK) {
        LOGE("Could not create AAudio builder: %s",
             AAudio_convertResultToText(result));
        return false;
    }

    AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setSampleRate(builder, 32040);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setDataCallback(builder, audio_callback, NULL);
    AAudioStreamBuilder_setErrorCallback(builder, error_callback, NULL);

    result = AAudioStreamBuilder_openStream(builder, &s_stream);
    AAudioStreamBuilder_delete(builder);

    if (result != AAUDIO_OK) {
        LOGE("Could not open AAudio stream: %s",
             AAudio_convertResultToText(result));
        s_stream = NULL;
        return false;
    }

    s_sample_rate = AAudioStream_getSampleRate(s_stream);
    RtlSetAudioOutputRate(s_sample_rate);

    ring_reset();
    s_fraction = 0.0;

    LOGI("Audio opened: %d Hz, %d channels",
         s_sample_rate, AAudioStream_getChannelCount(s_stream));

    result = AAudioStream_requestStart(s_stream);

    if (result != AAUDIO_OK) {
        LOGE("Could not start audio: %s", AAudio_convertResultToText(result));
        AAudioStream_close(s_stream);
        s_stream = NULL;
        return false;
    }

    return true;
}

static void reopen_stream(void) {
    if (s_stream) {
        AAudioStream_close(s_stream);
        s_stream = NULL;
    }

    if (open_and_start()) {
        s_retry_frames = 0;
    } else {
        atomic_store(&s_restart_requested, true);
        s_retry_frames = 120;
    }
}

bool Dkc1AndroidAudioInit(void) {
    if (s_initialized) return true;

    atomic_store(&s_restart_requested, false);
    s_retry_frames = 0;
    s_paused = false;

    if (!open_and_start()) return false;

    s_initialized = true;
    return true;
}

void Dkc1AndroidAudioRenderFrame(void) {
    if (!s_initialized || s_paused) return;

    if (atomic_load(&s_restart_requested)) {
        if (s_retry_frames > 0) {
            s_retry_frames--;
        } else {
            atomic_store(&s_restart_requested, false);
            LOGI("Audio output changed; reopening stream");
            reopen_stream();
        }
    }


    s_fraction += (double)s_sample_rate / GAME_HZ;

    int frames = (int)s_fraction;
    s_fraction -= frames;

    if (frames <= 0) return;

    if (frames > MAX_FRAME_SAMPLES) {
        LOGE("Audio frame unexpectedly large: %d", frames);
        return;
    }

    int16_t samples[MAX_FRAME_SAMPLES * 2] = {0};

    RtlRenderAudio(samples, frames, 2);
    ring_write(samples, (uint32_t)frames);
}

void Dkc1AndroidAudioSetPaused(bool paused) {
    if (!s_initialized || s_paused == paused) return;

    if (paused) {
        if (s_stream) {
            AAudioStream_requestPause(s_stream);
            AAudioStream_waitForStateChange(
                s_stream,
                AAUDIO_STREAM_STATE_PAUSING,
                NULL,
                1000000000
            );
        }
        ring_reset();
        s_paused = true;
    } else {
        ring_reset();
        s_fraction = 0.0;
        s_paused = false;

        if (!s_stream || atomic_load(&s_restart_requested)) {
            atomic_store(&s_restart_requested, false);
            s_retry_frames = 0;
            reopen_stream();
        } else {
            aaudio_result_t result = AAudioStream_requestStart(s_stream);
            if (result != AAUDIO_OK) {
                LOGE("Could not resume audio: %s",
                     AAudio_convertResultToText(result));
                atomic_store(&s_restart_requested, true);
                s_retry_frames = 0;
            }
        }
    }
}

void Dkc1AndroidAudioShutdown(void) {
    if (!s_initialized) return;

    if (s_stream) {
        AAudioStream_requestStop(s_stream);
        AAudioStream_close(s_stream);
    }

    s_stream = NULL;
    atomic_store(&s_restart_requested, false);
    s_initialized = false;
    s_paused = false;
    ring_reset();

    LOGI("Audio shut down");
}
