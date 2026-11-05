
#include <jni.h>
#include <android/log.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common_cpu_infra.h"
#include "common_rtl.h"
#include "dkc1_game.h"
#include "dkc1_video.h"
#include "verified_rom.h"
#include "android_audio.h"
#include "android_saves.h"
#include "android_viewport.h"

#define LOG_TAG "DKC1Recomp"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static uint8_t *s_rom = NULL;
static uint8_t s_pixels[kDkc1VideoMaxWidth * kDkc1VideoHeight * 4];
static int s_initialized = 0;
static int s_render_width = kDkc1VideoNativeWidth;

#ifndef NDEBUG
#define DKC1_PERF_LOG 1
#endif

static int64_t PerfNowNs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}
static struct {
    int64_t run_ns, draw_ns, worst_ns, window_start;
    int frames;
} s_perf;

static void PerfRecord(int64_t run_ns, int64_t draw_ns) {
#ifdef DKC1_PERF_LOG
    int64_t now = PerfNowNs();
    if (s_perf.window_start == 0) s_perf.window_start = now;
    s_perf.run_ns += run_ns;
    s_perf.draw_ns += draw_ns;
    if (run_ns + draw_ns > s_perf.worst_ns) s_perf.worst_ns = run_ns + draw_ns;
    s_perf.frames++;
    if (now - s_perf.window_start >= 1000000000LL) {
        double n = s_perf.frames;
        __android_log_print(ANDROID_LOG_INFO, "DKC1Perf",
            "%d frames/s  run %.2f ms  draw %.2f ms  worst %.2f ms  width %d  (budget 16.64 ms)",
            s_perf.frames, s_perf.run_ns / 1e6 / n, s_perf.draw_ns / 1e6 / n,
            s_perf.worst_ns / 1e6, s_render_width);
        memset(&s_perf, 0, sizeof s_perf);
        s_perf.window_start = now;
    }
#else
    (void)run_ns;
    (void)draw_ns;
#endif
}

JNIEXPORT jboolean JNICALL
Java_com_dkc1recomp_android_MainActivity_nativeInit(
    JNIEnv *env, jobject thiz, jstring rom_path, jstring save_root,
    jint view_width, jint view_height) {

    const char *rom = (*env)->GetStringUTFChars(env, rom_path, NULL);
    const char *saves = (*env)->GetStringUTFChars(env, save_root, NULL);

    if (!rom || !saves) {
        if (rom) (*env)->ReleaseStringUTFChars(env, rom_path, rom);
        if (saves) (*env)->ReleaseStringUTFChars(env, save_root, saves);
        return JNI_FALSE;
    }

    size_t rom_size = 0;
    char error[256] = {0};

    s_rom = Dkc1ReadVerifiedRom(rom, &rom_size, error, sizeof(error));

    (*env)->ReleaseStringUTFChars(env, rom_path, rom);

    if (!s_rom) {
        LOGE("ROM loading failed: %s", error);
        (*env)->ReleaseStringUTFChars(env, save_root, saves);
        return JNI_FALSE;
    }

    RtlSetSaveRoot(saves);
    bool saves_ok = Dkc1AndroidSavesSetRoot(saves);
    (*env)->ReleaseStringUTFChars(env, save_root, saves);
    RtlEnsureSaveDir();
    if (!saves_ok) LOGE("Save directory is not usable; progress will not be saved");

    s_render_width = Dkc1AndroidRenderWidth(view_width, view_height);
    if (!Dkc1VideoSetRenderWidth(s_render_width)) {
        LOGE("Could not set render width %d", s_render_width);
        free(s_rom);
        s_rom = NULL;
        return JNI_FALSE;
    }

    Dkc1VideoSetRom(s_rom, rom_size);
    RtlRegisterGame(Dkc1GameInfo());

    if (!SnesInit(s_rom, (int)rom_size)) {
        LOGE("SnesInit failed");
        free(s_rom);
        s_rom = NULL;
        return JNI_FALSE;
    }

    Dkc1BeginDrawing(s_pixels, s_render_width * 4);

    if (!Dkc1AndroidSramLoad(g_sram, (size_t)g_sram_size))
        LOGE("Battery save not loaded (unexpected cartridge RAM size %d)", g_sram_size);

    if (!Dkc1AndroidAudioInit()) {
        LOGE("Audio initialization failed. Continuing without audio.");
    }

    s_initialized = 1;
    LOGI("Engine initialized successfully");

    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_dkc1recomp_android_MainActivity_nativeFrame(
        JNIEnv *env, jobject thiz, jint input
) {
    if (!s_initialized) return JNI_FALSE;

    int64_t t_run = PerfNowNs();
    RtlRunFrame((uint32_t)input);
    int64_t t_after_run = PerfNowNs();

    if (g_fail || !Dkc1LastLleResult()) {
        LOGE("Engine stopped during frame execution");
        return JNI_FALSE;
    }

    Dkc1DrawPpuFrame();
    int64_t t_after_draw = PerfNowNs();

    Dkc1AndroidAudioRenderFrame();

    Dkc1AndroidSramTick(g_sram, (size_t)g_sram_size);

    PerfRecord(t_after_run - t_run, t_after_draw - t_after_run);

    return JNI_TRUE;
}

JNIEXPORT jint JNICALL
Java_com_dkc1recomp_android_MainActivity_nativeSetViewport(
        JNIEnv *env, jobject thiz, jint view_width, jint view_height
) {
    (void)env; (void)thiz;
    if (!s_initialized) return s_render_width;

    int width = Dkc1AndroidRenderWidth(view_width, view_height);
    if (width != s_render_width && Dkc1VideoSetRenderWidth(width)) {
        s_render_width = width;
        Dkc1BeginDrawing(s_pixels, s_render_width * 4);

        memset(s_pixels, 0, sizeof s_pixels);
        LOGI("Render width now %d for a %dx%d surface", s_render_width,
             (int)view_width, (int)view_height);
    }
    return s_render_width;
}

JNIEXPORT void JNICALL
Java_com_dkc1recomp_android_MainActivity_nativeFlushSaves(
        JNIEnv *env, jobject thiz
) {
    (void)env; (void)thiz;
    if (s_initialized) Dkc1AndroidSramFlush(g_sram, (size_t)g_sram_size);
}

JNIEXPORT jobject JNICALL
Java_com_dkc1recomp_android_MainActivity_nativeGetFrameBuffer(
    JNIEnv *env, jobject thiz) {

    if (!s_initialized)
        return NULL;

    return (*env)->NewDirectByteBuffer(
        env,
        s_pixels,
        s_render_width * kDkc1VideoHeight * 4
    );
}

JNIEXPORT void JNICALL
Java_com_dkc1recomp_android_MainActivity_nativeSetPaused(
    JNIEnv *env, jobject thiz, jboolean paused
) {
    Dkc1AndroidAudioSetPaused(paused == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_dkc1recomp_android_MainActivity_nativeShutdown(
    JNIEnv *env, jobject thiz) {

    if (s_initialized) {
        Dkc1AndroidSramFlush(g_sram, (size_t)g_sram_size);
        s_initialized = 0;
    }

    free(s_rom);
    s_rom = NULL;
    Dkc1AndroidAudioShutdown();

    LOGI("Engine shut down");
}
