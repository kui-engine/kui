// ===========================================================================
// kui_media_audio.dll — Audio playback plugin using miniaudio
//
// Pure-C implementation of the ABI declared in kui/media/kui_media_api.h.
// The plugin does not link KUI or kui_c.dll; the host resolves these exports
// dynamically through the independent kui_media_* protocol.
// ===========================================================================

#define KUI_MEDIA_BUILDING_DLL 1
#include <kui/media/kui_media_api.h>

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    KUI_MEDIA_AUDIO_MAX_PLAYERS = 16,
    KUI_MEDIA_AUDIO_ERROR_CAPACITY = 1024
};

static const float KUI_MEDIA_AUDIO_VOLUME_MIN = 0.0f;
static const float KUI_MEDIA_AUDIO_VOLUME_MAX = 1.0f;

// ---------------------------------------------------------------------------
// Global engine (singleton, lazy init)
// ---------------------------------------------------------------------------
static ma_engine g_engine;
static ma_device g_device;
static ma_spinlock g_engineLock = 0;
static ma_atomic_bool32 g_engineInit = {0};
static ma_atomic_bool32 g_noDeviceMode = {0};
static ma_atomic_bool32 g_deviceInit = {0};
static ma_atomic_bool32 g_shuttingDown = {0};
static ma_atomic_bool32 g_deviceThreadRunning = {0};
static ma_atomic_int32 g_playerCount = {0};
static ma_thread g_deviceThread;
static ma_bool32 g_deviceThreadCreated = MA_FALSE;

static float* g_scratchBuffer = NULL;
static size_t g_scratchCapacity = 0;

static ma_bool32 noDeviceRequested(void) {
    const char* value = getenv("KUI_MEDIA_NO_DEVICE");
    return value != NULL && value[0] != '\0' && value[0] != '0';
}

static float clampFloat(float value, float minimum, float maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static void audioDataCallback(ma_device* device, void* output,
                              const void* input, ma_uint32 frameCount) {
    ma_engine* engine;
    (void)input;
    engine = (ma_engine*)device->pUserData;
    if (engine != NULL) {
        ma_engine_read_pcm_frames(engine, output, frameCount, NULL);
    }
}

static ma_thread_result MA_THREADCALL deviceInitThread(void* userData) {
    ma_device_config config;
    (void)userData;

    if (ma_atomic_bool32_get(&g_shuttingDown)) {
        ma_atomic_bool32_set(&g_deviceThreadRunning, MA_FALSE);
        return 0;
    }

    config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = ma_engine_get_channels(&g_engine);
    config.sampleRate = ma_engine_get_sample_rate(&g_engine);
    config.dataCallback = audioDataCallback;
    config.pUserData = &g_engine;

    if (ma_device_init(NULL, &config, &g_device) == MA_SUCCESS) {
        if (ma_atomic_bool32_get(&g_shuttingDown)) {
            ma_device_uninit(&g_device);
        } else if (ma_device_start(&g_device) == MA_SUCCESS) {
            ma_atomic_bool32_set(&g_noDeviceMode, MA_FALSE);
            ma_atomic_bool32_set(&g_deviceInit, MA_TRUE);
        } else {
            ma_device_uninit(&g_device);
        }
    }

    ma_atomic_bool32_set(&g_deviceThreadRunning, MA_FALSE);
    return 0;
}

static ma_bool32 ensureEngine(void) {
    ma_engine_config config;
    ma_result result;

    if (ma_atomic_bool32_get(&g_engineInit)) return MA_TRUE;

    ma_spinlock_lock(&g_engineLock);
    if (ma_atomic_bool32_get(&g_engineInit)) {
        ma_spinlock_unlock(&g_engineLock);
        return MA_TRUE;
    }

    config = ma_engine_config_init();
    config.noDevice = MA_TRUE;
    config.channels = 2;
    config.sampleRate = 48000;
    result = ma_engine_init(&config, &g_engine);
    if (result != MA_SUCCESS) {
        ma_spinlock_unlock(&g_engineLock);
        return MA_FALSE;
    }

    ma_atomic_bool32_set(&g_noDeviceMode, MA_TRUE);
    ma_atomic_bool32_set(&g_engineInit, MA_TRUE);
    ma_atomic_bool32_set(&g_shuttingDown, MA_FALSE);

    if (!noDeviceRequested()) {
        ma_atomic_bool32_set(&g_deviceThreadRunning, MA_TRUE);
        result = ma_thread_create(&g_deviceThread, ma_thread_priority_default, 0,
                                  deviceInitThread, NULL, NULL);
        if (result == MA_SUCCESS) {
            g_deviceThreadCreated = MA_TRUE;
        } else {
            ma_atomic_bool32_set(&g_deviceThreadRunning, MA_FALSE);
        }
    }

    ma_spinlock_unlock(&g_engineLock);
    return MA_TRUE;
}

// ---------------------------------------------------------------------------
// Player implementation
// ---------------------------------------------------------------------------
typedef struct AudioPlayerImpl {
    ma_sound sound;
    ma_bool32 soundLoaded;
    ma_bool32 hasError;
    float volume;
    ma_bool32 muted;
    ma_bool32 looping;
    char errorMessage[KUI_MEDIA_AUDIO_ERROR_CAPACITY];
} AudioPlayerImpl;

static AudioPlayerImpl* validate(KuiAudioPlayer player) {
    return (AudioPlayerImpl*)player;
}

KUI_MEDIA_API KuiMediaPluginInfo kui_media_pluginInfo(void) {
    KuiMediaPluginInfo info;
    info.major = 0;
    info.minor = 1;
    info.patch = 0;
    info.name = "kui_media_audio";
    return info;
}

KUI_MEDIA_API void kui_media_shutdown(void) {
    int i;
    ma_atomic_bool32_set(&g_shuttingDown, MA_TRUE);

    for (i = 0; i < 3000 && ma_atomic_bool32_get(&g_deviceThreadRunning); ++i) {
        ma_sleep(1);
    }

    // Joining an already-finished thread releases its platform handle without
    // risking an unbounded wait on a device driver still inside init.
    if (g_deviceThreadCreated &&
        !ma_atomic_bool32_get(&g_deviceThreadRunning)) {
        ma_thread_wait(&g_deviceThread);
        g_deviceThreadCreated = MA_FALSE;
    }

    ma_spinlock_lock(&g_engineLock);
    if (ma_atomic_bool32_get(&g_deviceInit)) {
        ma_device_uninit(&g_device);
        ma_atomic_bool32_set(&g_deviceInit, MA_FALSE);
    }
    if (ma_atomic_bool32_get(&g_engineInit)) {
        ma_engine_uninit(&g_engine);
        ma_atomic_bool32_set(&g_engineInit, MA_FALSE);
    }
    free(g_scratchBuffer);
    g_scratchBuffer = NULL;
    g_scratchCapacity = 0;
    ma_atomic_bool32_set(&g_noDeviceMode, MA_TRUE);
    ma_spinlock_unlock(&g_engineLock);
}

KUI_MEDIA_API const char* kui_media_getError(void* player) {
    AudioPlayerImpl* impl = validate(player);
    if (impl == NULL) return "null player handle";
    return impl->errorMessage;
}

KUI_MEDIA_API KuiAudioPlayer kui_media_audio_create(void) {
    AudioPlayerImpl* impl;
    if (!ensureEngine()) return NULL;
    if (ma_atomic_int32_get(&g_playerCount) >= KUI_MEDIA_AUDIO_MAX_PLAYERS) {
        return NULL;
    }

    impl = (AudioPlayerImpl*)calloc(1, sizeof(*impl));
    if (impl == NULL) return NULL;
    impl->volume = 1.0f;
    ma_atomic_int32_fetch_add(&g_playerCount, 1);
    return (KuiAudioPlayer)impl;
}

KUI_MEDIA_API void kui_media_audio_destroy(KuiAudioPlayer player) {
    AudioPlayerImpl* impl = validate(player);
    if (impl == NULL) return;
    if (impl->soundLoaded) {
        ma_sound_stop(&impl->sound);
        ma_sound_uninit(&impl->sound);
    }
    free(impl);
    ma_atomic_int32_fetch_sub(&g_playerCount, 1);
}

KUI_MEDIA_API KuiMediaError kui_media_audio_load(KuiAudioPlayer player,
                                                  const char* path) {
    AudioPlayerImpl* impl = validate(player);
    ma_result result;
    if (impl == NULL) return KUI_MEDIA_ERR_BUSY;
    if (path == NULL || path[0] == '\0') return KUI_MEDIA_ERR_INVALID;

    if (impl->soundLoaded) {
        ma_sound_stop(&impl->sound);
        ma_sound_uninit(&impl->sound);
        impl->soundLoaded = MA_FALSE;
    }
    impl->hasError = MA_FALSE;
    impl->errorMessage[0] = '\0';

    result = ma_sound_init_from_file(&g_engine, path, 0, NULL, NULL,
                                     &impl->sound);
    if (result != MA_SUCCESS) {
        impl->hasError = MA_TRUE;
        snprintf(impl->errorMessage, sizeof(impl->errorMessage),
                 "Failed to load audio file: %s (ma_result=%d)",
                 path, (int)result);
        if (result == MA_DOES_NOT_EXIST || result == MA_ACCESS_DENIED) {
            return KUI_MEDIA_ERR_FILE;
        }
        if (result == MA_NO_BACKEND || result == MA_NO_DEVICE) {
            return KUI_MEDIA_ERR_AUDIO;
        }
        return KUI_MEDIA_ERR_CODEC;
    }

    impl->soundLoaded = MA_TRUE;
    ma_sound_set_volume(&impl->sound, impl->muted ? 0.0f : impl->volume);
    ma_sound_set_looping(&impl->sound,
                         impl->looping ? MA_TRUE : MA_FALSE);
    return KUI_MEDIA_OK;
}

KUI_MEDIA_API void kui_media_audio_play(KuiAudioPlayer player) {
    AudioPlayerImpl* impl = validate(player);
    if (impl == NULL || !impl->soundLoaded) return;
    if (ma_sound_at_end(&impl->sound)) {
        ma_sound_seek_to_pcm_frame(&impl->sound, 0);
    }
    ma_sound_start(&impl->sound);
}

KUI_MEDIA_API void kui_media_audio_pause(KuiAudioPlayer player) {
    AudioPlayerImpl* impl = validate(player);
    if (impl != NULL && impl->soundLoaded) ma_sound_stop(&impl->sound);
}

KUI_MEDIA_API void kui_media_audio_stop(KuiAudioPlayer player) {
    AudioPlayerImpl* impl = validate(player);
    if (impl == NULL || !impl->soundLoaded) return;
    ma_sound_stop(&impl->sound);
    ma_sound_seek_to_pcm_frame(&impl->sound, 0);
}

KUI_MEDIA_API int kui_media_audio_isPlaying(KuiAudioPlayer player) {
    AudioPlayerImpl* impl = validate(player);
    return impl != NULL && impl->soundLoaded &&
           ma_sound_is_playing(&impl->sound) ? 1 : 0;
}

KUI_MEDIA_API int kui_media_audio_isEnded(KuiAudioPlayer player) {
    AudioPlayerImpl* impl = validate(player);
    return impl != NULL && impl->soundLoaded &&
           ma_sound_at_end(&impl->sound) ? 1 : 0;
}

KUI_MEDIA_API int kui_media_audio_hasError(KuiAudioPlayer player) {
    AudioPlayerImpl* impl = validate(player);
    return impl == NULL || impl->hasError ? 1 : 0;
}

KUI_MEDIA_API double kui_media_audio_getTime(KuiAudioPlayer player) {
    AudioPlayerImpl* impl = validate(player);
    float cursor = 0.0f;
    if (impl == NULL || !impl->soundLoaded) return 0.0;
    ma_sound_get_cursor_in_seconds(&impl->sound, &cursor);
    return (double)cursor;
}

KUI_MEDIA_API void kui_media_audio_seek(KuiAudioPlayer player,
                                        double seconds) {
    AudioPlayerImpl* impl = validate(player);
    float length = 0.0f;
    ma_uint32 sampleRate;
    ma_uint64 frame;
    if (impl == NULL || !impl->soundLoaded) return;
    ma_sound_get_length_in_seconds(&impl->sound, &length);
    if (seconds < 0.0) seconds = 0.0;
    if (seconds > (double)length) seconds = (double)length;
    sampleRate = ma_engine_get_sample_rate(&g_engine);
    frame = (ma_uint64)(seconds * sampleRate);
    ma_sound_seek_to_pcm_frame(&impl->sound, frame);
}

KUI_MEDIA_API double kui_media_audio_getDuration(KuiAudioPlayer player) {
    AudioPlayerImpl* impl = validate(player);
    float length = 0.0f;
    if (impl == NULL || !impl->soundLoaded) return 0.0;
    ma_sound_get_length_in_seconds(&impl->sound, &length);
    return (double)length;
}

KUI_MEDIA_API void kui_media_audio_setVolume(KuiAudioPlayer player, float volume) {
    AudioPlayerImpl* impl = validate(player);
    if (impl == NULL) return;
    impl->volume = clampFloat(volume, KUI_MEDIA_AUDIO_VOLUME_MIN,
                             KUI_MEDIA_AUDIO_VOLUME_MAX);
    if (impl->soundLoaded && !impl->muted) {
        ma_sound_set_volume(&impl->sound, impl->volume);
    }
}

KUI_MEDIA_API float kui_media_audio_getVolume(KuiAudioPlayer player) {
    AudioPlayerImpl* impl = validate(player);
    return impl != NULL ? impl->volume : 1.0f;
}

KUI_MEDIA_API void kui_media_audio_setMuted(KuiAudioPlayer player, int muted) {
    AudioPlayerImpl* impl = validate(player);
    if (impl == NULL) return;
    impl->muted = muted != 0 ? MA_TRUE : MA_FALSE;
    if (impl->soundLoaded) {
        ma_sound_set_volume(&impl->sound,
                            impl->muted ? 0.0f : impl->volume);
    }
}

KUI_MEDIA_API int kui_media_audio_isMuted(KuiAudioPlayer player) {
    AudioPlayerImpl* impl = validate(player);
    return impl != NULL && impl->muted ? 1 : 0;
}

KUI_MEDIA_API void kui_media_audio_setLooping(KuiAudioPlayer player, int loop) {
    AudioPlayerImpl* impl = validate(player);
    if (impl == NULL) return;
    impl->looping = loop != 0 ? MA_TRUE : MA_FALSE;
    if (impl->soundLoaded) {
        ma_sound_set_looping(&impl->sound, impl->looping);
    }
}

KUI_MEDIA_API int kui_media_audio_isLooping(KuiAudioPlayer player) {
    AudioPlayerImpl* impl = validate(player);
    return impl != NULL && impl->looping ? 1 : 0;
}

KUI_MEDIA_API void kui_media_audio_setPan(KuiAudioPlayer player, float pan) {
    AudioPlayerImpl* impl = validate(player);
    if (impl == NULL || !impl->soundLoaded) return;
    ma_sound_set_pan(&impl->sound, clampFloat(pan, -1.0f, 1.0f));
}

KUI_MEDIA_API float kui_media_audio_getPan(KuiAudioPlayer player) {
    AudioPlayerImpl* impl = validate(player);
    if (impl == NULL || !impl->soundLoaded) return 0.0f;
    return ma_sound_get_pan(&impl->sound);
}

KUI_MEDIA_API void kui_media_audio_setPitch(KuiAudioPlayer player, float pitch) {
    AudioPlayerImpl* impl = validate(player);
    if (impl == NULL || !impl->soundLoaded) return;
    ma_sound_set_pitch(&impl->sound, clampFloat(pitch, 0.1f, 4.0f));
}

KUI_MEDIA_API float kui_media_audio_getPitch(KuiAudioPlayer player) {
    AudioPlayerImpl* impl = validate(player);
    if (impl == NULL || !impl->soundLoaded) return 1.0f;
    return ma_sound_get_pitch(&impl->sound);
}

KUI_MEDIA_API void kui_media_audio_tick(KuiAudioPlayer player, double dt) {
    ma_uint32 sampleRate;
    ma_uint32 channels;
    ma_uint64 framesToRead;
    size_t needed;
    float* resized;
    (void)player;

    if (!ma_atomic_bool32_get(&g_noDeviceMode) ||
        !ma_atomic_bool32_get(&g_engineInit)) {
        return;
    }

    sampleRate = ma_engine_get_sample_rate(&g_engine);
    channels = ma_engine_get_channels(&g_engine);
    framesToRead = (ma_uint64)(dt * sampleRate);
    if (framesToRead == 0) framesToRead = sampleRate / 60;
    if (framesToRead > 8192) framesToRead = 8192;
    needed = (size_t)(framesToRead * channels);

    if (g_scratchCapacity < needed) {
        resized = (float*)realloc(g_scratchBuffer, needed * sizeof(float));
        if (resized == NULL) return;
        g_scratchBuffer = resized;
        g_scratchCapacity = needed;
    }
    ma_engine_read_pcm_frames(&g_engine, g_scratchBuffer, framesToRead, NULL);
}
