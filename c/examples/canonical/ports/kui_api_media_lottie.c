/* API tour: game audio, media elements and Lottie (examples/api_tours/media_lottie). At start-up
 * the host drives the document's audio manager (play by path into groups, per-handle volume /
 * pan / pitch / pause / seek, group volume and mute, pause-all / stop-all, preload / unload,
 * counts), an <audio> and a <video> element (src, load, loop / autoplay / muted / volume flags,
 * seek, play / pause, network and ready state) and a <lottie-player> (state, natural size, speed,
 * loop, pause, seek, stop), checking every call against the state read back. Time-dependent
 * results are read once frames have run; #tour-result ends as "PASS n/n". Works without an audio
 * device (KUI_MEDIA_NO_DEVICE=1): the checks read state, they do not listen. */
#include "kui_c_example_dom.h"

#include <math.h>
#include <stdarg.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

static KuiDocument* g_doc;

/* ---- Self-check bookkeeping: count, log one <tr> per check, publish the verdict ---------- */

static int g_total;
static int g_failed;
static int g_known;
static char g_failed_names[512];

static void log_row(const char* name, const char* result, const char* cell) {
    KuiElement* log = kuex_by_id(g_doc, "tour-log");
    KuiElement* row = NULL;
    if (!log) return;
    if (kui_document_create_element(g_doc, "tr", 0, &row) == KUI_OK) {
        kui_element_set_attribute(row, "data-check", 0, name, 0);
        kui_element_set_attribute(row, "data-result", 0, result, 0);
        if (strcmp(result, "ok") != 0) kui_element_class_list_add(row, "bad", 0);
        kuex_append_release(row, kuex_make_element(g_doc, "td", name, NULL));
        kuex_append_release(row, kuex_make_element(g_doc, "td", cell, NULL));
        kuex_append_release(log, row);
    }
    kui_element_release(log);
}

static void check(const char* name, int ok, const char* fmt, ...) {
    char detail[256];
    char cell[300];
    size_t used = strlen(g_failed_names);
    va_list args;

    g_total++;
    va_start(args, fmt);
    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);
    if (ok) {
        log_row(name, "ok", "ok");
        return;
    }
    g_failed++;
    snprintf(g_failed_names + used, sizeof g_failed_names - used, "%s%s", used ? ", " : "", name);
    snprintf(cell, sizeof cell, "FAIL: %s", detail);
    fprintf(stderr, "tour check failed: %s: %s\n", name, detail);
    log_row(name, "fail", cell);
}

/* A documented behaviour the engine currently gets wrong: logged, not counted. */
static void known(const char* name, int ok, const char* bug, const char* fmt, ...) {
    char detail[256];
    char cell[400];
    va_list args;
    if (ok) {
        g_total++;
        log_row(name, "ok", "ok");
        return;
    }
    va_start(args, fmt);
    vsnprintf(detail, sizeof detail, fmt, args);
    va_end(args);
    g_known++;
    snprintf(cell, sizeof cell, "KNOWN BUG: %s (%s)", bug, detail);
    fprintf(stderr, "tour known bug: %s: %s\n", name, detail);
    log_row(name, "known", cell);
}

static void finish(void) {
    char text[640];
    char number[16];
    size_t used;
    KuiElement* result = kuex_by_id(g_doc, "tour-result");
    if (!result) return;
    if (g_failed == 0) {
        snprintf(text, sizeof text, "PASS %d/%d", g_total, g_total);
    } else {
        snprintf(text, sizeof text, "FAIL %d/%d: %s", g_failed, g_total, g_failed_names);
    }
    used = strlen(text);
    if (g_known) snprintf(text + used, sizeof text - used, " (known bugs: %d)", g_known);
    kui_element_set_text_content(result, text, 0);
    snprintf(number, sizeof number, "%d", g_total);
    kui_element_set_attribute(result, "data-total", 0, number, 0);
    snprintf(number, sizeof number, "%d", g_failed);
    kui_element_set_attribute(result, "data-failed", 0, number, 0);
    snprintf(number, sizeof number, "%d", g_known);
    kui_element_set_attribute(result, "data-known", 0, number, 0);
    kui_element_class_list_add(result, g_failed == 0 ? "pass" : "fail", 0);
    kui_element_release(result);
}

/* ---- Paths: the bundled media sit in assets/ next to the executable ---------------------- */

static char g_exe_dir[1024];
static char g_mp3[1200];
static char g_mp4[1200];
static char g_lottie[1200];
static char g_missing[1200];

static void find_paths(void) {
#ifdef _WIN32
    DWORD length = GetModuleFileNameA(NULL, g_exe_dir, (DWORD)sizeof g_exe_dir);
    if (length == 0 || length >= sizeof g_exe_dir) length = 0;
    g_exe_dir[length] = '\0';
#elif defined(__APPLE__)
    uint32_t length = (uint32_t)sizeof g_exe_dir;
    if (_NSGetExecutablePath(g_exe_dir, &length) != 0) g_exe_dir[0] = '\0';
#else
    ssize_t length = readlink("/proc/self/exe", g_exe_dir, sizeof g_exe_dir - 1);
    g_exe_dir[length > 0 ? length : 0] = '\0';
#endif
    {
        char* slash = strrchr(g_exe_dir, '\\');
        char* forward = strrchr(g_exe_dir, '/');
        if (forward && (!slash || forward > slash)) slash = forward;
        if (slash) *slash = '\0';
    }
    snprintf(g_mp3, sizeof g_mp3, "%s/assets/native_media/sample.mp3", g_exe_dir);
    snprintf(g_mp4, sizeof g_mp4, "%s/assets/native_media/sample.mp4", g_exe_dir);
    snprintf(g_lottie, sizeof g_lottie, "%s/assets/native_animation/pulse.json", g_exe_dir);
    snprintf(g_missing, sizeof g_missing, "%s/assets/native_media/no_such_sound.mp3", g_exe_dir);
}

/* ---- Small helpers ---------------------------------------------------------------------- */

static int near_f(double a, double b, double tolerance) { return fabs(a - b) <= tolerance; }

static const char* group_name(KuiCAudioGroup group) {
    switch (group) {
        case KUI_C_AUDIO_MASTER: return "master";
        case KUI_C_AUDIO_MUSIC: return "music";
        case KUI_C_AUDIO_SFX: return "sfx";
        default: return "ui";
    }
}

static KuiCAudioPlayOptions play_options(KuiCAudioGroup group, float volume, float pan,
                                         float pitch, int loop) {
    KuiCAudioPlayOptions options;
    memset(&options, 0, sizeof options);
    options.group = group;
    options.volume = volume;
    options.pan = pan;
    options.pitch = pitch;
    options.loop = loop;
    return options;
}

/* Status of kui_document_audio_get_info; *info zeroed when it fails. */
static KuiStatus sound_info(uint32_t handle, KuiCSoundInfo* info) {
    KuiStatus status;
    memset(info, 0, sizeof *info);
    status = kui_document_audio_get_info(g_doc, handle, info);
    if (status != KUI_OK) memset(info, 0, sizeof *info);
    return status;
}

static uint32_t active_count(int group) {
    uint32_t active = 0;
    kui_document_audio_get_counts(g_doc, group, &active, NULL);
    return active;
}

static uint32_t pooled_count(void) {
    uint32_t pooled = 0;
    kui_document_audio_get_counts(g_doc, -1, NULL, &pooled);
    return pooled;
}

/* ---- Game audio: availability, one looping music sound, sfx, groups, pool --------------- */

static int g_audio_available;
static uint32_t g_music;          /* the looping music handle the buttons drive */
static double g_music_seek_to = 1.5;

static void check_audio_groups(void) {
    float volume = -1.0f;
    int muted = -1;
    float restored = -1.0f;
    int restored_muted = -1;
    KuiStatus bad_volume;
    KuiStatus bad_group;

    /* Group volume / mute live in the manager and work with or without a device. */
    kui_document_audio_set_group(g_doc, KUI_C_AUDIO_MUSIC, 0.4f, 1);
    kui_document_audio_get_group(g_doc, KUI_C_AUDIO_MUSIC, &volume, &muted);
    bad_volume = kui_document_audio_set_group(g_doc, KUI_C_AUDIO_MUSIC, 2.0f, 0);
    bad_group = kui_document_audio_get_group(g_doc, (KuiCAudioGroup)7, NULL, NULL);
    kui_document_audio_set_group(g_doc, KUI_C_AUDIO_MUSIC, 1.0f, 0);
    kui_document_audio_get_group(g_doc, KUI_C_AUDIO_MUSIC, &restored, &restored_muted);
    check("audio.set_group", near_f(volume, 0.4, 0.001) && muted == 1 &&
              bad_volume == KUI_ERR_INVALID_ARG && bad_group == KUI_ERR_INVALID_ARG &&
              near_f(restored, 1.0, 0.001) && restored_muted == 0,
          "music 0.4/muted -> %.2f/%d, volume 2 status %d, group 7 status %d, restored %.2f/%d",
          volume, muted, bad_volume, bad_group, restored, restored_muted);
}

static void check_audio_unavailable(void) {
    KuiCAudioPlayOptions options = play_options(KUI_C_AUDIO_SFX, 1.0f, 0.0f, 1.0f, 0);
    uint32_t handle = 7;
    uint32_t loaded = 9;
    KuiStatus status = kui_document_audio_play(g_doc, g_mp3, 0, &options, &handle);
    kui_document_audio_preload(g_doc, g_mp3, 0, 2, &loaded);
    check("audio.play-unavailable", status == KUI_ERR_NOT_SUPPORTED && handle == 0 && loaded == 0,
          "play status %d handle %u, preload loaded %u", status, handle, loaded);
}

static void check_audio_handles(void) {
    KuiCAudioPlayOptions options = play_options(KUI_C_AUDIO_MUSIC, 0.5f, -0.5f, 1.25f, 1);
    KuiCSoundInfo info;
    KuiStatus status;
    uint32_t loaded = 0;
    uint32_t missing = 7;
    uint32_t pooled;

    status = kui_document_audio_preload(g_doc, g_mp3, 0, 2, &loaded);
    pooled = pooled_count();
    check("audio.preload", status == KUI_OK && loaded == 2 && pooled >= 2,
          "status %d loaded %u pooled %u", status, loaded, pooled);

    status = kui_document_audio_play(g_doc, g_mp3, 0, &options, &g_music);
    sound_info(g_music, &info);
    check("audio.play", status == KUI_OK && g_music != 0 && info.group == KUI_C_AUDIO_MUSIC &&
              near_f(info.volume, 0.5, 0.001) && near_f(info.pan, -0.5, 0.001) &&
              near_f(info.pitch, 1.25, 0.001) && info.looping && info.playing,
          "status %d handle %u group %s vol %.2f pan %.2f pitch %.2f loop %d playing %d", status,
          g_music, group_name(info.group), info.volume, info.pan, info.pitch, info.looping,
          info.playing);

    kui_document_audio_set_volume(g_doc, g_music, 0.3f);
    kui_document_audio_set_pan(g_doc, g_music, 0.7f);
    kui_document_audio_set_pitch(g_doc, g_music, 0.8f);
    sound_info(g_music, &info);
    check("audio.set_volume_pan_pitch", near_f(info.volume, 0.3, 0.001) &&
              near_f(info.pan, 0.7, 0.001) && near_f(info.pitch, 0.8, 0.001),
          "vol %.2f pan %.2f pitch %.2f", info.volume, info.pan, info.pitch);

    kui_document_audio_set_paused(g_doc, g_music, 1);
    sound_info(g_music, &info);
    {
        int paused_playing = info.playing;
        kui_document_audio_set_paused(g_doc, g_music, 0);
        sound_info(g_music, &info);
        check("audio.set_paused", paused_playing == 0 && info.playing == 1,
              "playing while paused %d, after resume %d", paused_playing, info.playing);
    }

    status = kui_document_audio_seek(g_doc, g_music, g_music_seek_to);
    {
        KuiStatus bad = kui_document_audio_seek(g_doc, g_music, -1.0);
        check("audio.seek-args", status == KUI_OK && bad == KUI_ERR_INVALID_ARG,
              "seek status %d, seek(-1) status %d", status, bad);
    }

    status = kui_document_audio_play(g_doc, g_missing, 0, NULL, &missing);
    check("audio.play-missing-file", status == KUI_ERR_IO && missing == 0, "status %d handle %u",
          status, missing);
}

static uint32_t g_sfx[2];

static void check_audio_bulk(void) {
    KuiCAudioPlayOptions sfx = play_options(KUI_C_AUDIO_SFX, 0.2f, 0.0f, 1.0f, 0);
    KuiCSoundInfo info;
    uint32_t music_active, sfx_active, all_active;
    int paused_playing, resumed_playing;

    kui_document_audio_play(g_doc, g_mp3, 0, &sfx, &g_sfx[0]);
    kui_document_audio_play(g_doc, g_mp3, 0, &sfx, &g_sfx[1]);
    music_active = active_count(KUI_C_AUDIO_MUSIC);
    sfx_active = active_count(KUI_C_AUDIO_SFX);
    all_active = active_count(-1);
    check("audio.get_counts", music_active == 1 && sfx_active == 2 && all_active == 3,
          "music %u sfx %u all %u", music_active, sfx_active, all_active);

    kui_document_audio_set_all_paused(g_doc, KUI_C_AUDIO_MUSIC, 1);
    sound_info(g_music, &info);
    paused_playing = info.playing;
    sound_info(g_sfx[0], &info);
    {
        int sfx_playing = info.playing;
        kui_document_audio_set_all_paused(g_doc, -1, 0);
        sound_info(g_music, &info);
        resumed_playing = info.playing;
        check("audio.set_all_paused", paused_playing == 0 && sfx_playing == 1 && resumed_playing == 1,
              "music while group paused %d, sfx %d, music after resume all %d", paused_playing,
              sfx_playing, resumed_playing);
    }

    kui_document_audio_stop_all(g_doc, KUI_C_AUDIO_SFX);
    sfx_active = active_count(KUI_C_AUDIO_SFX);
    music_active = active_count(KUI_C_AUDIO_MUSIC);
    check("audio.stop_all", sfx_active == 0 && music_active == 1, "sfx %u music %u", sfx_active,
          music_active);
}

/* Once frames have run: the seek landed and the position moves; then stop and unload. */
static void check_audio_late(void) {
    KuiCSoundInfo info;
    KuiStatus status;
    uint32_t pooled_before, pooled_after;
    uint32_t stopped = 0;

    sound_info(g_music, &info);
    check("audio.get_info-time", info.duration > 1.0 && info.current_time > g_music_seek_to - 0.05,
          "duration %.2f current %.2f (seeked to %.2f)", info.duration, info.current_time,
          g_music_seek_to);

    stopped = g_music;
    kui_document_audio_stop(g_doc, g_music);
    status = sound_info(stopped, &info);
    check("audio.stop", (status == KUI_ERR_NOT_FOUND || !info.playing) &&
              active_count(KUI_C_AUDIO_MUSIC) == 0,
          "info status %d playing %d, music active %u", status, info.playing,
          active_count(KUI_C_AUDIO_MUSIC));
    status = kui_document_audio_stop(g_doc, 987654u);
    check("audio.stop-unknown", status == KUI_OK, "status %d", status);

    pooled_before = pooled_count();
    kui_document_audio_unload(g_doc, g_mp3, 0);
    pooled_after = pooled_count();
    kui_document_audio_unload(g_doc, NULL, 0);
    check("audio.unload", pooled_before > 0 && pooled_after == 0 && pooled_count() == 0,
          "pooled %u -> %u after unload(path), %u after unload(NULL)", pooled_before, pooled_after,
          pooled_count());
    g_music = 0;
}

static void check_audio(void) {
    int available = -1;
    KuiStatus status = kui_document_audio_is_available(g_doc, &available);
    char text[96];
    g_audio_available = available == 1;
    /* Recorded as is: with KUI_MEDIA_NO_DEVICE=1 the plugin runs without a device. */
    check("audio.is_available", status == KUI_OK && (available == 0 || available == 1),
          "status %d available %d", status, available);
    snprintf(text, sizeof text, "available=%d", available);
    kuex_set_text(g_doc, "au-avail", text);
    check_audio_groups();
    if (!g_audio_available) {
        check_audio_unavailable();
        return;
    }
    check_audio_handles();
    check_audio_bulk();
}

/* ---- Media elements: flags, volume, mute, src / load, seek, play / pause --------------- */

static KuiElement* g_aud;
static KuiElement* g_vid;

static void media_flags(KuiElement* media, int* loop, int* autoplay, int* ended) {
    *loop = *autoplay = *ended = -1;
    kui_media_get_playback_flags(media, loop, autoplay, ended);
}

static double media_time(KuiElement* media) {
    double t = -1;
    kui_media_get_current_time(media, &t);
    return t;
}

static int media_paused(KuiElement* media) {
    int paused = -1;
    kui_media_get_paused(media, &paused);
    return paused;
}

static void check_media_setup(const char* name, KuiElement* media, const char* src) {
    char check_name[64];
    int state = -1;
    int loop, autoplay, ended;
    int muted_on = -1, muted_off = -1;
    float volume = -1.0f;
    int available = -1;

    kui_media_get_network_state(media, &state);
    snprintf(check_name, sizeof check_name, "%s.network_state-empty", name);
    check(check_name, state == 0, "networkState %d before src (expected 0 NETWORK_EMPTY)", state);

    kui_media_set_loop(media, 1);
    kui_media_set_autoplay(media, 0);
    media_flags(media, &loop, &autoplay, &ended);
    snprintf(check_name, sizeof check_name, "%s.playback_flags", name);
    check(check_name, loop == 1 && autoplay == 0 && ended == 0, "loop %d autoplay %d ended %d",
          loop, autoplay, ended);

    kui_media_set_muted(media, 1);
    kui_media_get_muted(media, &muted_on);
    kui_media_set_muted(media, 0);
    kui_media_get_muted(media, &muted_off);
    kui_media_set_volume(media, 0.4f);
    kui_media_get_volume(media, &volume);
    snprintf(check_name, sizeof check_name, "%s.muted_volume", name);
    check(check_name, muted_on == 1 && muted_off == 0 && near_f(volume, 0.4, 0.001),
          "muted on %d off %d, volume %.2f", muted_on, muted_off, volume);

    kui_media_is_available(media, &available);
    kui_media_set_src(media, src, 0);
    snprintf(check_name, sizeof check_name, "%s.load", name);
    {
        KuiStatus status = kui_media_load(media);
        state = -1;
        kui_media_get_network_state(media, &state);
        check(check_name, status == KUI_OK && state == 2 && media_paused(media) == 1 &&
                  media_time(media) == 0.0 && (available == 0 || available == 1),
              "status %d networkState %d (expected 2 LOADING) paused %d available %d", status,
              state, media_paused(media), available);
    }
}

static void check_media_not_media(void) {
    KuiElement* plain = kuex_by_id(g_doc, "plain");
    int paused = -1;
    KuiCLottieState state;
    KuiStatus media_status = kui_media_get_paused(plain, &paused);
    KuiStatus lottie_status = kui_lottie_get_state(plain, &state);
    check("media.not_media_element", media_status == KUI_ERR_NOT_SUPPORTED &&
              lottie_status == KUI_ERR_NOT_SUPPORTED,
          "media_get_paused status %d, lottie_get_state status %d", media_status, lottie_status);
    if (plain) kui_element_release(plain);
}

/* A second after load(): metadata should be there without playing (HTML load()). */
static void check_media_metadata(const char* name, KuiElement* media) {
    char check_name[64];
    double duration = -1;
    int ready = -1;
    kui_media_get_duration(media, &duration);
    kui_media_get_ready_state(media, &ready);
    snprintf(check_name, sizeof check_name, "%s.metadata-before-play", name);
    known(check_name, duration > 0 && ready >= 1,
          "media load lazily on the first play(): load() leaves readyState 0 and duration 0",
          "duration %.2f readyState %d a second after load()", duration, ready);
}

/* Play loads the file: duration, network / ready state; then seek while playing. */
static void check_media_play(const char* name, KuiElement* media, double seek_to) {
    char check_name[64];
    double duration = -1;
    int network = -1, ready = -1;
    double after_seek;

    kui_media_play(media);
    kui_media_get_duration(media, &duration);
    kui_media_get_network_state(media, &network);
    kui_media_get_ready_state(media, &ready);
    snprintf(check_name, sizeof check_name, "%s.play", name);
    check(check_name, media_paused(media) == 0 && duration > seek_to + 1.0 && network == 1 &&
              ready >= 1,
          "paused %d duration %.2f networkState %d readyState %d", media_paused(media), duration,
          network, ready);

    kui_media_set_current_time(media, seek_to);
    after_seek = media_time(media);
    snprintf(check_name, sizeof check_name, "%s.set_current_time", name);
    check(check_name, near_f(after_seek, seek_to, 0.3), "currentTime %.2f right after seek to %.2f",
          after_seek, seek_to);
}

/* A while after the seek: playback continues from there; pause holds it. */
static void check_media_playing(const char* name, KuiElement* media, double seek_to, int seek_bug) {
    char check_name[64];
    double now = media_time(media);
    int paused;
    snprintf(check_name, sizeof check_name, "%s.current_time-after-seek", name);
    if (seek_bug) {
        known(check_name, now >= seek_to && now < seek_to + 60.0,
              "seeking an <audio> element never reaches the player: playback goes on from the old "
              "position and currentTime falls back to it",
              "currentTime %.2f, seeked to %.2f", now, seek_to);
    } else {
        check(check_name, now >= seek_to && now < seek_to + 5.0, "currentTime %.2f, seeked to %.2f",
              now, seek_to);
    }
    kui_media_pause(media);
    paused = media_paused(media);
    snprintf(check_name, sizeof check_name, "%s.pause", name);
    check(check_name, paused == 1 && media_time(media) > 0, "paused %d currentTime %.2f", paused,
          media_time(media));
}

/* ---- Lottie: load, state, speed / loop, pause / seek / stop --------------------------- */

static KuiElement* g_lot;

static KuiCLottieState lottie_state(void) {
    KuiCLottieState state;
    memset(&state, 0, sizeof state);
    kui_lottie_get_state(g_lot, &state);
    return state;
}

static double g_lottie_mark;

static void check_lottie_loaded(void) {
    float width = 0, height = 0;
    KuiCLottieState state = lottie_state();
    kui_lottie_get_natural_size(g_lot, &width, &height);
    check("lottie.get_state-loaded", state.load_state == KUI_C_LOTTIE_COMPLETE && state.playing &&
              near_f(state.speed, 1.0, 0.001),
          "load %d playing %d speed %.2f", state.load_state, state.playing, state.speed);
    check("lottie.get_natural_size", near_f(width, 200, 0.5) && near_f(height, 200, 0.5),
          "%.0fx%.0f (pulse.json is 200x200)", width, height);
    g_lottie_mark = state.position;
}

static void check_lottie_controls(void) {
    KuiCLottieState state = lottie_state();
    KuiCLottieState after;
    KuiStatus bad;
    check("lottie.position-advances", state.position != g_lottie_mark,
          "position %.3f then %.3f", g_lottie_mark, state.position);

    kui_lottie_set_speed(g_lot, 2.0f);
    after = lottie_state();
    bad = kui_lottie_set_speed(g_lot, -1.0f);
    check("lottie.set_speed", near_f(after.speed, 2.0, 0.001) && bad == KUI_ERR_INVALID_ARG,
          "speed %.2f, speed -1 status %d", after.speed, bad);
    kui_lottie_set_speed(g_lot, 1.0f);

    kui_lottie_set_loop(g_lot, 0);
    after = lottie_state();
    {
        int off = after.looping;
        kui_lottie_set_loop(g_lot, 1);
        after = lottie_state();
        check("lottie.set_loop", off == 0 && after.looping == 1, "loop off %d, on %d", off,
              after.looping);
    }

    kui_lottie_pause(g_lot);
    kui_lottie_seek(g_lot, 0.5);
    after = lottie_state();
    check("lottie.pause_seek", !after.playing && near_f(after.position, 0.5, 0.02),
          "playing %d position %.3f", after.playing, after.position);
}

static void check_lottie_held(void) {
    KuiCLottieState state = lottie_state();
    KuiCLottieState stopped;
    check("lottie.paused-holds", !state.playing && near_f(state.position, 0.5, 0.02),
          "playing %d position %.3f after a while", state.playing, state.position);
    kui_lottie_stop(g_lot);
    stopped = lottie_state();
    check("lottie.stop", !stopped.playing && near_f(stopped.position, 0.0, 0.001),
          "playing %d position %.3f", stopped.playing, stopped.position);
    kui_lottie_play(g_lot); /* leave it playing for the panel */
}

/* ---- Phases: immediate calls, then timers for results that need frames ----------------- */

#define AUDIO_SEEK 300.0 /* far from where playback is after a second (the file is 675 s) */
#define VIDEO_SEEK 5.0   /* the clip is 13.3 s */

static void phase_three(void* user_data) {
    (void)user_data;
    check_media_playing("audio", g_aud, AUDIO_SEEK, 1);
    check_media_playing("video", g_vid, VIDEO_SEEK, 0);
    check_lottie_held();
    if (g_audio_available) check_audio_late();
    finish();
}

static void phase_two(void* user_data) {
    KuiTimerId timer;
    (void)user_data;
    check_media_metadata("audio", g_aud);
    check_media_metadata("video", g_vid);
    check_media_play("audio", g_aud, AUDIO_SEEK);
    check_media_play("video", g_vid, VIDEO_SEEK);
    check_lottie_controls();
    kui_platform_set_timeout(phase_three, NULL, 700, &timer);
}

static void phase_one(void* user_data) {
    KuiTimerId timer;
    (void)user_data;
    check_lottie_loaded();
    kui_platform_set_timeout(phase_two, NULL, 900, &timer);
}

/* ---- Panel buttons ---------------------------------------------------------------------- */

static int g_au_paused, g_au_quiet, g_au_right, g_au_fast, g_au_all_paused, g_au_muted;

static void report_audio(void) {
    char text[200];
    char counts[96];
    KuiCSoundInfo info;
    KuiStatus status = sound_info(g_music, &info);
    float group_volume = 0;
    int group_muted = 0;
    if (status == KUI_OK) {
        snprintf(text, sizeof text,
                 "music #%u playing=%d vol=%.2f pan=%.2f pitch=%.2f loop=%d t=%.1f/%.1f", g_music,
                 info.playing, info.volume, info.pan, info.pitch, info.looping, info.current_time,
                 info.duration);
    } else {
        snprintf(text, sizeof text, "music: none (handle %u, status %d)", g_music, status);
    }
    kuex_set_text(g_doc, "au-out", text);
    kui_document_audio_get_group(g_doc, KUI_C_AUDIO_MUSIC, &group_volume, &group_muted);
    snprintf(counts, sizeof counts, "active=%u sfx=%u pooled=%u music-group=%.1f%s",
             active_count(-1), active_count(KUI_C_AUDIO_SFX), pooled_count(), group_volume,
             group_muted ? " muted" : "");
    kuex_set_text(g_doc, "au-counts", counts);
}

static void on_au_play(KuiEvent* e, void* u) {
    KuiCAudioPlayOptions options = play_options(KUI_C_AUDIO_MUSIC, 1.0f, 0.0f, 1.0f, 1);
    (void)e;
    (void)u;
    kui_document_audio_stop(g_doc, g_music);
    if (kui_document_audio_play(g_doc, g_mp3, 0, &options, &g_music) != KUI_OK) g_music = 0;
    g_au_paused = g_au_quiet = g_au_right = g_au_fast = 0;
    report_audio();
}

static void on_au_pause(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    g_au_paused = !g_au_paused;
    kui_document_audio_set_paused(g_doc, g_music, g_au_paused);
    report_audio();
}

static void on_au_volume(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    g_au_quiet = !g_au_quiet;
    kui_document_audio_set_volume(g_doc, g_music, g_au_quiet ? 0.3f : 1.0f);
    report_audio();
}

static void on_au_pan(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    g_au_right = !g_au_right;
    kui_document_audio_set_pan(g_doc, g_music, g_au_right ? 1.0f : -1.0f);
    report_audio();
}

static void on_au_pitch(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    g_au_fast = !g_au_fast;
    kui_document_audio_set_pitch(g_doc, g_music, g_au_fast ? 1.5f : 1.0f);
    report_audio();
}

static void on_au_seek(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_document_audio_seek(g_doc, g_music, 2.0);
    report_audio();
}

static void on_au_stop(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_document_audio_stop(g_doc, g_music);
    report_audio();
}

static void on_au_sfx(KuiEvent* e, void* u) {
    KuiCAudioPlayOptions sfx = play_options(KUI_C_AUDIO_SFX, 0.3f, 0.0f, 1.0f, 0);
    uint32_t handle;
    (void)e;
    (void)u;
    kui_document_audio_play(g_doc, g_mp3, 0, &sfx, &handle);
    kui_document_audio_play(g_doc, g_mp3, 0, &sfx, &handle);
    report_audio();
}

static void on_au_stopsfx(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_document_audio_stop_all(g_doc, KUI_C_AUDIO_SFX);
    report_audio();
}

static void on_au_pauseall(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    g_au_all_paused = !g_au_all_paused;
    kui_document_audio_set_all_paused(g_doc, -1, g_au_all_paused);
    report_audio();
}

static void on_au_mute(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    g_au_muted = !g_au_muted;
    kui_document_audio_set_group(g_doc, KUI_C_AUDIO_MUSIC, g_au_muted ? 0.5f : 1.0f, g_au_muted);
    report_audio();
}

static void on_au_preload(KuiEvent* e, void* u) {
    uint32_t loaded = 0;
    (void)e;
    (void)u;
    kui_document_audio_preload(g_doc, g_mp3, 0, 3, &loaded);
    report_audio();
}

static void on_au_unload(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_document_audio_unload(g_doc, NULL, 0);
    report_audio();
}

static void report_lottie(void) {
    char text[128];
    KuiCLottieState state = lottie_state();
    snprintf(text, sizeof text, "playing=%d loop=%d speed=%.1f position=%.2f load=%d",
             state.playing, state.looping, state.speed, state.position, state.load_state);
    kuex_set_text(g_doc, "lot-out", text);
}

static int g_lot_fast;

static void on_lot_play(KuiEvent* e, void* u) { (void)e; (void)u; kui_lottie_play(g_lot); report_lottie(); }
static void on_lot_pause(KuiEvent* e, void* u) { (void)e; (void)u; kui_lottie_pause(g_lot); report_lottie(); }
static void on_lot_seek0(KuiEvent* e, void* u) { (void)e; (void)u; kui_lottie_seek(g_lot, 0.0); report_lottie(); }
static void on_lot_seek50(KuiEvent* e, void* u) { (void)e; (void)u; kui_lottie_seek(g_lot, 0.5); report_lottie(); }
static void on_lot_stop(KuiEvent* e, void* u) { (void)e; (void)u; kui_lottie_stop(g_lot); report_lottie(); }

static void on_lot_speed(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    g_lot_fast = !g_lot_fast;
    kui_lottie_set_speed(g_lot, g_lot_fast ? 2.0f : 1.0f);
    report_lottie();
}

static void on_lot_loop(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    kui_lottie_set_loop(g_lot, !lottie_state().looping);
    report_lottie();
}

static void report_media(const char* id, KuiElement* media) {
    char text[200];
    int loop, autoplay, ended, muted = -1, network = -1, ready = -1;
    double duration = -1;
    float volume = -1;
    media_flags(media, &loop, &autoplay, &ended);
    kui_media_get_muted(media, &muted);
    kui_media_get_volume(media, &volume);
    kui_media_get_duration(media, &duration);
    kui_media_get_network_state(media, &network);
    kui_media_get_ready_state(media, &ready);
    snprintf(text, sizeof text,
             "paused=%d t=%.1f/%.1f muted=%d vol=%.1f loop=%d autoplay=%d ended=%d net=%d ready=%d",
             media_paused(media), media_time(media), duration, muted, volume, loop, autoplay,
             ended, network, ready);
    kuex_set_text(g_doc, id, text);
}

static void on_aud_play(KuiEvent* e, void* u) { (void)e; (void)u; kui_media_play(g_aud); report_media("aud-out", g_aud); }
static void on_aud_pause(KuiEvent* e, void* u) { (void)e; (void)u; kui_media_pause(g_aud); report_media("aud-out", g_aud); }
static void on_aud_seek(KuiEvent* e, void* u) { (void)e; (void)u; kui_media_set_current_time(g_aud, 3.0); report_media("aud-out", g_aud); }

static void on_aud_mute(KuiEvent* e, void* u) {
    int muted = 0;
    (void)e;
    (void)u;
    kui_media_get_muted(g_aud, &muted);
    kui_media_set_muted(g_aud, !muted);
    report_media("aud-out", g_aud);
}

static void on_aud_loop(KuiEvent* e, void* u) {
    int loop, autoplay, ended;
    (void)e;
    (void)u;
    media_flags(g_aud, &loop, &autoplay, &ended);
    kui_media_set_loop(g_aud, !loop);
    report_media("aud-out", g_aud);
}

static void on_aud_load(KuiEvent* e, void* u) { (void)e; (void)u; kui_media_load(g_aud); report_media("aud-out", g_aud); }
static void on_vid_play(KuiEvent* e, void* u) { (void)e; (void)u; kui_media_play(g_vid); report_media("vid-out", g_vid); }
static void on_vid_pause(KuiEvent* e, void* u) { (void)e; (void)u; kui_media_pause(g_vid); report_media("vid-out", g_vid); }
static void on_vid_seek(KuiEvent* e, void* u) { (void)e; (void)u; kui_media_set_current_time(g_vid, 1.0); report_media("vid-out", g_vid); }

static int g_vid_controls;

static void on_vid_controls(KuiEvent* e, void* u) {
    (void)e;
    (void)u;
    g_vid_controls = !g_vid_controls;
    kui_media_set_controls(g_vid, g_vid_controls);
    report_media("vid-out", g_vid);
}

static void on_vid_autoplay(KuiEvent* e, void* u) {
    int loop, autoplay, ended;
    (void)e;
    (void)u;
    media_flags(g_vid, &loop, &autoplay, &ended);
    kui_media_set_autoplay(g_vid, !autoplay);
    report_media("vid-out", g_vid);
}

int kui_c_example_ready(const KuiCExampleHost* host) {
    int failed = 0;
    KuiTimerId timer;
    g_doc = host->document;
    find_paths();
    g_aud = kuex_by_id(g_doc, "aud"); /* kept for the whole run (buttons use them) */
    g_vid = kuex_by_id(g_doc, "vid");
    g_lot = kuex_by_id(g_doc, "lot");
    if (!g_aud || !g_vid || !g_lot) return 1;

    kui_lottie_set_src(g_lot, g_lottie, 0);
    kui_lottie_set_loop(g_lot, 1);
    kui_lottie_play(g_lot);

    check_audio();
    check_media_setup("audio", g_aud, g_mp3);
    check_media_setup("video", g_vid, g_mp4);
    check_media_not_media();
    kui_platform_set_timeout(phase_one, NULL, 300, &timer);

    failed |= kuex_on(g_doc, "btn-au-play", "click", on_au_play, NULL);
    failed |= kuex_on(g_doc, "btn-au-pause", "click", on_au_pause, NULL);
    failed |= kuex_on(g_doc, "btn-au-volume", "click", on_au_volume, NULL);
    failed |= kuex_on(g_doc, "btn-au-pan", "click", on_au_pan, NULL);
    failed |= kuex_on(g_doc, "btn-au-pitch", "click", on_au_pitch, NULL);
    failed |= kuex_on(g_doc, "btn-au-seek", "click", on_au_seek, NULL);
    failed |= kuex_on(g_doc, "btn-au-stop", "click", on_au_stop, NULL);
    failed |= kuex_on(g_doc, "btn-au-sfx", "click", on_au_sfx, NULL);
    failed |= kuex_on(g_doc, "btn-au-stopsfx", "click", on_au_stopsfx, NULL);
    failed |= kuex_on(g_doc, "btn-au-pauseall", "click", on_au_pauseall, NULL);
    failed |= kuex_on(g_doc, "btn-au-mute", "click", on_au_mute, NULL);
    failed |= kuex_on(g_doc, "btn-au-preload", "click", on_au_preload, NULL);
    failed |= kuex_on(g_doc, "btn-au-unload", "click", on_au_unload, NULL);
    failed |= kuex_on(g_doc, "btn-lot-play", "click", on_lot_play, NULL);
    failed |= kuex_on(g_doc, "btn-lot-pause", "click", on_lot_pause, NULL);
    failed |= kuex_on(g_doc, "btn-lot-seek0", "click", on_lot_seek0, NULL);
    failed |= kuex_on(g_doc, "btn-lot-seek50", "click", on_lot_seek50, NULL);
    failed |= kuex_on(g_doc, "btn-lot-stop", "click", on_lot_stop, NULL);
    failed |= kuex_on(g_doc, "btn-lot-speed", "click", on_lot_speed, NULL);
    failed |= kuex_on(g_doc, "btn-lot-loop", "click", on_lot_loop, NULL);
    failed |= kuex_on(g_doc, "btn-aud-play", "click", on_aud_play, NULL);
    failed |= kuex_on(g_doc, "btn-aud-pause", "click", on_aud_pause, NULL);
    failed |= kuex_on(g_doc, "btn-aud-seek", "click", on_aud_seek, NULL);
    failed |= kuex_on(g_doc, "btn-aud-mute", "click", on_aud_mute, NULL);
    failed |= kuex_on(g_doc, "btn-aud-loop", "click", on_aud_loop, NULL);
    failed |= kuex_on(g_doc, "btn-aud-load", "click", on_aud_load, NULL);
    failed |= kuex_on(g_doc, "btn-vid-play", "click", on_vid_play, NULL);
    failed |= kuex_on(g_doc, "btn-vid-pause", "click", on_vid_pause, NULL);
    failed |= kuex_on(g_doc, "btn-vid-seek", "click", on_vid_seek, NULL);
    failed |= kuex_on(g_doc, "btn-vid-controls", "click", on_vid_controls, NULL);
    failed |= kuex_on(g_doc, "btn-vid-autoplay", "click", on_vid_autoplay, NULL);
    return failed;
}
