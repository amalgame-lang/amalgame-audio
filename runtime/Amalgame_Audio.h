/*
 * Amalgame Standard Library — Amalgame.Audio
 * Copyright (c) 2026 Bastien MOUGET
 * https://github.com/amalgame-lang/Amalgame
 *
 * Audio synthesis, playback and capture for Amalgame, built on
 * David Reid's public-domain miniaudio single-header library.
 * miniaudio abstracts the OS-native audio APIs (WASAPI on Windows,
 * Core Audio on macOS, ALSA/PulseAudio on Linux, sndio/JACK on BSD)
 * behind a single API — we wrap that.
 *
 * Surface (v1) — sample-format = int16 signed mono unless noted:
 *
 *   ── Synthesis ──
 *   Audio.GenSine(freqHz, durSec, sampleRate)    -> List<int>
 *   Audio.GenSquare(freqHz, durSec, sampleRate)  -> List<int>
 *   Audio.GenTriangle(freqHz, durSec, sampleRate) -> List<int>
 *   Audio.GenNoise(durSec, sampleRate)           -> List<int>
 *   Audio.GenSilence(durSec, sampleRate)         -> List<int>
 *
 *   ── Transformation ──
 *   Audio.ApplyEnvelope(samples, attackMs, releaseMs, sampleRate)
 *                                                -> List<int> (new buf)
 *   Audio.Scale(samples, gain)                   -> List<int>
 *   Audio.Mix(a, b, offsetSamples)               -> List<int>
 *       Mix b on top of a starting at offset (samples), output is
 *       max(len(a), offset+len(b)) long. Clips to int16 range.
 *   Audio.Echo(samples, delayMs, decay, repeats, sampleRate)
 *                                                -> List<int>
 *       Build the classic feedback echo — `repeats` decayed copies
 *       layered every `delayMs`. Cheap stand-in for full reverb.
 *
 *   ── IO ──
 *   Audio.SaveAsWav(samples, sampleRate, path)   -> bool
 *       16-bit PCM mono WAV via ma_encoder.
 *   Audio.LoadWav(path)                          -> List<int>
 *       Decode a 16-bit mono WAV back to samples. Non-mono /
 *       non-PCM-16 files are downmixed + converted by miniaudio.
 *   Audio.Play(samples, sampleRate)              -> bool
 *       Blocking playback through the default output device.
 *       Returns once every sample has been pushed to the device.
 *
 *   ── Diag ──
 *   Audio.LastError()                            -> string
 *       Process-global last-error string (cleared on each successful
 *       call). Empty when no error has happened yet.
 *
 *   ── v0.2 — multi-format decoders + sondes ──
 *   Audio.Load(path)              -> List<int>   auto-sniff WAV/MP3/FLAC/OGG
 *   Audio.LoadMp3(path)           -> List<int>
 *   Audio.LoadFlac(path)          -> List<int>
 *   Audio.LoadOgg(path)           -> List<int>
 *   Audio.SampleRateOf(path)      -> int (Hz)
 *   Audio.ChannelCountOf(path)    -> int (1 = mono, 2 = stereo, …)
 *   Audio.DurationMsOf(path)      -> int (milliseconds)
 *
 * Threading: synthesis / transformation are pure (return new
 * buffers). Play is blocking single-threaded; one Play call per
 * thread. Concurrent SaveAsWav/LoadWav calls against distinct
 * paths are safe.
 *
 * Pixel-style note on samples: each sample is int16 stored as i64
 * (since AM's int = i64). We clip to [-32768, 32767] on every
 * write path; reads return int16 sign-extended into i64.
 */

#ifndef AMALGAME_AUDIO_H
#define AMALGAME_AUDIO_H

#include "_runtime.h"
#include "Amalgame_Collections.h"
#include "miniaudio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef AMALGAME_AUDIO_PI
#  define AMALGAME_AUDIO_PI 3.14159265358979323846
#endif

/* ── Process-global last-error ──────────────────────── */

static char* _amaudio_last_error = NULL;

static inline void _amaudio_set_error(const char* msg) {
    if (!msg) { _amaudio_last_error = NULL; return; }
    size_t n = strlen(msg);
    char* p = (char*) code_alloc(n + 1);
    memcpy(p, msg, n + 1);
    _amaudio_last_error = p;
}

static inline code_string Amalgame_Audio_LastError(void) {
    if (!_amaudio_last_error) return (code_string) "";
    return _amaudio_last_error;
}

/* ── Small helpers ──────────────────────────────────── */

/* Allocate a fresh AmalgameList* and stuff each int16 sample in.
 * We store i64 to match AM's int type; clipping happens at write
 * time (Mix / Echo). */
static inline AmalgameList* _amaudio_buf_from_pcm(const int16_t* pcm, size_t n) {
    AmalgameList* list = AmalgameList_new();
    for (size_t i = 0; i < n; i++) {
        AmalgameList_add(list, (void*) (intptr_t) (i64) pcm[i]);
    }
    return list;
}

/* Read a List<int> back into a freshly-malloc'd int16 buffer.
 * The caller frees it via free(). Out-of-range values clip. */
static inline int16_t* _amaudio_pcm_from_buf(AmalgameList* buf, size_t* out_n) {
    if (!buf) { *out_n = 0; return NULL; }
    size_t n = (size_t) AmalgameList_count(buf);
    int16_t* pcm = (int16_t*) malloc(n * sizeof(int16_t));
    if (!pcm) { *out_n = 0; return NULL; }
    for (size_t i = 0; i < n; i++) {
        i64 v = (i64) (intptr_t) AmalgameList_get(buf, (i64) i);
        if (v >  32767) v =  32767;
        if (v < -32768) v = -32768;
        pcm[i] = (int16_t) v;
    }
    *out_n = n;
    return pcm;
}

/* ── Synthesis primitives ───────────────────────────── */

static inline AmalgameList* Amalgame_Audio_GenSine(f64 freqHz, f64 durSec, i64 sampleRate) {
    size_t n = (size_t) (durSec * (f64) sampleRate);
    int16_t* pcm = (int16_t*) malloc(n * sizeof(int16_t));
    if (!pcm) { _amaudio_set_error("GenSine: OOM"); return AmalgameList_new(); }
    f64 step = 2.0 * AMALGAME_AUDIO_PI * freqHz / (f64) sampleRate;
    for (size_t i = 0; i < n; i++) {
        f64 s = sin((f64) i * step);
        pcm[i] = (int16_t) (s * 32767.0);
    }
    AmalgameList* out = _amaudio_buf_from_pcm(pcm, n);
    free(pcm);
    return out;
}

static inline AmalgameList* Amalgame_Audio_GenSquare(f64 freqHz, f64 durSec, i64 sampleRate) {
    size_t n = (size_t) (durSec * (f64) sampleRate);
    int16_t* pcm = (int16_t*) malloc(n * sizeof(int16_t));
    if (!pcm) { _amaudio_set_error("GenSquare: OOM"); return AmalgameList_new(); }
    f64 step = 2.0 * AMALGAME_AUDIO_PI * freqHz / (f64) sampleRate;
    for (size_t i = 0; i < n; i++) {
        f64 s = sin((f64) i * step);
        pcm[i] = (s >= 0.0) ? (int16_t)  32767 : (int16_t) -32768;
    }
    AmalgameList* out = _amaudio_buf_from_pcm(pcm, n);
    free(pcm);
    return out;
}

static inline AmalgameList* Amalgame_Audio_GenTriangle(f64 freqHz, f64 durSec, i64 sampleRate) {
    size_t n = (size_t) (durSec * (f64) sampleRate);
    int16_t* pcm = (int16_t*) malloc(n * sizeof(int16_t));
    if (!pcm) { _amaudio_set_error("GenTriangle: OOM"); return AmalgameList_new(); }
    f64 period = (f64) sampleRate / freqHz;
    for (size_t i = 0; i < n; i++) {
        f64 phase = fmod((f64) i, period) / period;       /* 0..1 */
        f64 tri   = (phase < 0.5) ? (4.0 * phase - 1.0)   /* -1..+1 ramp up */
                                  : (3.0 - 4.0 * phase);   /* +1..-1 ramp down */
        pcm[i] = (int16_t) (tri * 32767.0);
    }
    AmalgameList* out = _amaudio_buf_from_pcm(pcm, n);
    free(pcm);
    return out;
}

/* White noise via xorshift32 for determinism + speed. Seed picked
 * from the time once per process to keep call-to-call output
 * non-repeating; users wanting reproducible noise should seed
 * themselves and pre-generate. */
static inline uint32_t _amaudio_xorshift_state(void) {
    static uint32_t state = 0;
    if (state == 0) state = 0x12345678u ^ (uint32_t) clock();
    return state;
}
static inline uint32_t _amaudio_xorshift_next(uint32_t* s) {
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x; return x;
}
static inline AmalgameList* Amalgame_Audio_GenNoise(f64 durSec, i64 sampleRate) {
    size_t n = (size_t) (durSec * (f64) sampleRate);
    int16_t* pcm = (int16_t*) malloc(n * sizeof(int16_t));
    if (!pcm) { _amaudio_set_error("GenNoise: OOM"); return AmalgameList_new(); }
    uint32_t state = _amaudio_xorshift_state();
    for (size_t i = 0; i < n; i++) {
        uint32_t r = _amaudio_xorshift_next(&state);
        /* Map low 16 bits to signed int16 ; bias-shift to center. */
        pcm[i] = (int16_t) (r & 0xFFFF) - 32768;
    }
    AmalgameList* out = _amaudio_buf_from_pcm(pcm, n);
    free(pcm);
    return out;
}

static inline AmalgameList* Amalgame_Audio_GenSilence(f64 durSec, i64 sampleRate) {
    size_t n = (size_t) (durSec * (f64) sampleRate);
    AmalgameList* list = AmalgameList_new();
    for (size_t i = 0; i < n; i++) {
        AmalgameList_add(list, (void*) (intptr_t) (i64) 0);
    }
    return list;
}

/* ── Transformations ────────────────────────────────── */

/* Apply a simple linear attack + release envelope. Useful to avoid
 * the audible "click" at the very start / end of a generated tone.
 * Sustain is implicit — anything between attack and release stays
 * at full amplitude. attackMs / releaseMs are clamped if larger
 * than the buffer.  Returns a fresh buffer (input unchanged). */
static inline AmalgameList* Amalgame_Audio_ApplyEnvelope(
        AmalgameList* samples, f64 attackMs, f64 releaseMs, i64 sampleRate) {
    if (!samples) return AmalgameList_new();
    size_t n = (size_t) AmalgameList_count(samples);
    if (n == 0) return AmalgameList_new();

    size_t att = (size_t) (attackMs  * 0.001 * (f64) sampleRate);
    size_t rel = (size_t) (releaseMs * 0.001 * (f64) sampleRate);
    if (att > n / 2) att = n / 2;
    if (rel > n / 2) rel = n / 2;

    int16_t* pcm = (int16_t*) malloc(n * sizeof(int16_t));
    if (!pcm) { _amaudio_set_error("ApplyEnvelope: OOM"); return AmalgameList_new(); }
    for (size_t i = 0; i < n; i++) {
        i64 v = (i64) (intptr_t) AmalgameList_get(samples, (i64) i);
        f64 gain = 1.0;
        if (i < att)                gain = (f64) i / (f64) att;
        else if (i >= n - rel)      gain = (f64) (n - 1 - i) / (f64) rel;
        f64 scaled = (f64) v * gain;
        if (scaled >  32767.0) scaled =  32767.0;
        if (scaled < -32768.0) scaled = -32768.0;
        pcm[i] = (int16_t) scaled;
    }
    AmalgameList* out = _amaudio_buf_from_pcm(pcm, n);
    free(pcm);
    return out;
}

/* Multiply every sample by gain (0.0..1.0 to attenuate, >1.0
 * boosts but clips on int16 overflow). Pure, returns fresh buf. */
static inline AmalgameList* Amalgame_Audio_Scale(AmalgameList* samples, f64 gain) {
    if (!samples) return AmalgameList_new();
    size_t n = (size_t) AmalgameList_count(samples);
    int16_t* pcm = (int16_t*) malloc(n * sizeof(int16_t));
    if (!pcm) { _amaudio_set_error("Scale: OOM"); return AmalgameList_new(); }
    for (size_t i = 0; i < n; i++) {
        i64 v = (i64) (intptr_t) AmalgameList_get(samples, (i64) i);
        f64 scaled = (f64) v * gain;
        if (scaled >  32767.0) scaled =  32767.0;
        if (scaled < -32768.0) scaled = -32768.0;
        pcm[i] = (int16_t) scaled;
    }
    AmalgameList* out = _amaudio_buf_from_pcm(pcm, n);
    free(pcm);
    return out;
}

/* Mix b on top of a starting at offset (sample index). Output is
 * max(len(a), offset+len(b)) long; positions outside len(a) start
 * silent. Per-sample sum is clipped to int16 range. */
static inline AmalgameList* Amalgame_Audio_Mix(
        AmalgameList* a, AmalgameList* b, i64 offsetSamples) {
    size_t na = a ? (size_t) AmalgameList_count(a) : 0;
    size_t nb = b ? (size_t) AmalgameList_count(b) : 0;
    if (offsetSamples < 0) offsetSamples = 0;
    size_t end_b = (size_t) offsetSamples + nb;
    size_t n_out = na > end_b ? na : end_b;
    if (n_out == 0) return AmalgameList_new();

    int16_t* pcm = (int16_t*) calloc(n_out, sizeof(int16_t));
    if (!pcm) { _amaudio_set_error("Mix: OOM"); return AmalgameList_new(); }
    for (size_t i = 0; i < na; i++) {
        pcm[i] = (int16_t) (i64) (intptr_t) AmalgameList_get(a, (i64) i);
    }
    for (size_t i = 0; i < nb; i++) {
        size_t j = (size_t) offsetSamples + i;
        i64 va = (i64) pcm[j];
        i64 vb = (i64) (intptr_t) AmalgameList_get(b, (i64) i);
        i64 sum = va + vb;
        if (sum >  32767) sum =  32767;
        if (sum < -32768) sum = -32768;
        pcm[j] = (int16_t) sum;
    }
    AmalgameList* out = _amaudio_buf_from_pcm(pcm, n_out);
    free(pcm);
    return out;
}

/* Build a feedback echo: the original buffer overlaid with
 * `repeats` copies, each delayed by an extra `delayMs` and
 * attenuated by `decay` ^ k. Output length = len(samples) +
 * repeats * delaySamples (every echo tail fully contained). */
static inline AmalgameList* Amalgame_Audio_Echo(
        AmalgameList* samples, f64 delayMs, f64 decay,
        i64 repeats, i64 sampleRate) {
    if (!samples || repeats <= 0 || delayMs <= 0.0) {
        /* Echoless — return a fresh copy. */
        return Amalgame_Audio_Scale(samples, 1.0);
    }
    size_t n = (size_t) AmalgameList_count(samples);
    size_t delaySamples = (size_t) (delayMs * 0.001 * (f64) sampleRate);
    size_t n_out = n + (size_t) repeats * delaySamples;
    int16_t* pcm = (int16_t*) calloc(n_out, sizeof(int16_t));
    if (!pcm) { _amaudio_set_error("Echo: OOM"); return AmalgameList_new(); }

    /* Original pass through unchanged. */
    for (size_t i = 0; i < n; i++) {
        pcm[i] = (int16_t) (i64) (intptr_t) AmalgameList_get(samples, (i64) i);
    }
    /* Decayed echoes. */
    f64 gain = decay;
    for (i64 r = 1; r <= repeats; r++) {
        size_t off = (size_t) r * delaySamples;
        for (size_t i = 0; i < n; i++) {
            size_t j = off + i;
            if (j >= n_out) break;
            i64 src = (i64) (intptr_t) AmalgameList_get(samples, (i64) i);
            i64 add = (i64) ((f64) src * gain);
            i64 sum = (i64) pcm[j] + add;
            if (sum >  32767) sum =  32767;
            if (sum < -32768) sum = -32768;
            pcm[j] = (int16_t) sum;
        }
        gain *= decay;
    }
    AmalgameList* out = _amaudio_buf_from_pcm(pcm, n_out);
    free(pcm);
    return out;
}

/* ── IO ─────────────────────────────────────────────── */

/* Save samples as a 16-bit PCM mono WAV via ma_encoder. */
static inline code_bool Amalgame_Audio_SaveAsWav(
        AmalgameList* samples, i64 sampleRate, code_string path) {
    if (!samples || !path) { _amaudio_set_error("SaveAsWav: null arg"); return 0; }
    size_t n = 0;
    int16_t* pcm = _amaudio_pcm_from_buf(samples, &n);
    if (!pcm) { _amaudio_set_error("SaveAsWav: OOM"); return 0; }

    ma_encoder_config cfg = ma_encoder_config_init(
        ma_encoding_format_wav, ma_format_s16, 1, (ma_uint32) sampleRate);
    ma_encoder enc;
    if (ma_encoder_init_file(path, &cfg, &enc) != MA_SUCCESS) {
        free(pcm);
        _amaudio_set_error("SaveAsWav: ma_encoder_init_file failed");
        return 0;
    }
    ma_uint64 written = 0;
    ma_result rc = ma_encoder_write_pcm_frames(&enc, pcm, (ma_uint64) n, &written);
    ma_encoder_uninit(&enc);
    free(pcm);
    if (rc != MA_SUCCESS || written != (ma_uint64) n) {
        _amaudio_set_error("SaveAsWav: write_pcm_frames failed");
        return 0;
    }
    _amaudio_set_error(NULL);
    return 1;
}

/* Decode a WAV file back into a List<int> of int16 mono samples.
 * Non-mono / non-PCM-16 input is converted on the fly by miniaudio
 * to s16 mono so the result matches what GenSine etc. produces. */
static inline AmalgameList* Amalgame_Audio_LoadWav(code_string path) {
    if (!path) { _amaudio_set_error("LoadWav: null path"); return AmalgameList_new(); }
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 1, 0);
    ma_decoder dec;
    if (ma_decoder_init_file(path, &cfg, &dec) != MA_SUCCESS) {
        _amaudio_set_error("LoadWav: ma_decoder_init_file failed");
        return AmalgameList_new();
    }
    ma_uint64 total = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &total) != MA_SUCCESS) {
        ma_decoder_uninit(&dec);
        _amaudio_set_error("LoadWav: get_length failed");
        return AmalgameList_new();
    }
    int16_t* pcm = (int16_t*) malloc((size_t) total * sizeof(int16_t));
    if (!pcm) {
        ma_decoder_uninit(&dec);
        _amaudio_set_error("LoadWav: OOM");
        return AmalgameList_new();
    }
    ma_uint64 got = 0;
    ma_decoder_read_pcm_frames(&dec, pcm, total, &got);
    ma_decoder_uninit(&dec);
    AmalgameList* out = _amaudio_buf_from_pcm(pcm, (size_t) got);
    free(pcm);
    _amaudio_set_error(NULL);
    return out;
}

/* Blocking playback through the default output device. Streams
 * samples through miniaudio's pull-callback model — we register a
 * data callback that copies from the user's PCM buffer until it's
 * drained, then signals an event so the main thread can return.
 *
 * Internal state lives on the stack; nothing escapes the call.
 */
typedef struct {
    const int16_t* pcm;
    size_t         n;
    size_t         pos;
    ma_event       done;
} _amaudio_play_ctx;

static void _amaudio_play_cb(ma_device* dev, void* output, const void* input, ma_uint32 frames) {
    (void) input;
    _amaudio_play_ctx* ctx = (_amaudio_play_ctx*) dev->pUserData;
    int16_t* out = (int16_t*) output;
    size_t remaining = ctx->n - ctx->pos;
    size_t want = (size_t) frames;
    size_t take = (want < remaining) ? want : remaining;
    if (take > 0) {
        memcpy(out, ctx->pcm + ctx->pos, take * sizeof(int16_t));
        ctx->pos += take;
    }
    /* Pad the remainder with silence so the device isn't fed
     * uninitialised memory if we underflow. */
    if (take < want) {
        memset(out + take, 0, (want - take) * sizeof(int16_t));
        ma_event_signal(&ctx->done);
    }
}

static inline code_bool Amalgame_Audio_Play(AmalgameList* samples, i64 sampleRate) {
    if (!samples) { _amaudio_set_error("Play: null buf"); return 0; }
    size_t n = 0;
    int16_t* pcm = _amaudio_pcm_from_buf(samples, &n);
    if (!pcm || n == 0) {
        free(pcm);
        _amaudio_set_error("Play: empty buf");
        return 0;
    }

    _amaudio_play_ctx ctx = { pcm, n, 0, {0} };
    if (ma_event_init(&ctx.done) != MA_SUCCESS) {
        free(pcm); _amaudio_set_error("Play: event_init failed");
        return 0;
    }

    ma_device_config dcfg = ma_device_config_init(ma_device_type_playback);
    dcfg.playback.format   = ma_format_s16;
    dcfg.playback.channels = 1;
    dcfg.sampleRate        = (ma_uint32) sampleRate;
    dcfg.dataCallback      = _amaudio_play_cb;
    dcfg.pUserData         = &ctx;

    ma_device dev;
    if (ma_device_init(NULL, &dcfg, &dev) != MA_SUCCESS) {
        ma_event_uninit(&ctx.done);
        free(pcm); _amaudio_set_error("Play: device_init failed");
        return 0;
    }
    if (ma_device_start(&dev) != MA_SUCCESS) {
        ma_device_uninit(&dev);
        ma_event_uninit(&ctx.done);
        free(pcm); _amaudio_set_error("Play: device_start failed");
        return 0;
    }
    /* Wait until the data callback drained the buffer. */
    ma_event_wait(&ctx.done);

    ma_device_uninit(&dev);
    ma_event_uninit(&ctx.done);
    free(pcm);
    _amaudio_set_error(NULL);
    return 1;
}

/* ═══════════════════════════════════════════════════════
 *  v0.2 — multi-format decoders + sondes
 * ═══════════════════════════════════════════════════════
 *
 * ma_impl.c (v0.2) enables MP3 / FLAC / Vorbis decoding via the
 * dr_libs implementations bundled with miniaudio. The single
 * `ma_decoder_init_file` entry point already auto-detects format
 * via magic bytes, so the per-format Load* verbs below are thin
 * wrappers — useful when call-site code wants to express its
 * intent in the verb name. `Load` is the format-agnostic verb.
 *
 * All loaders return List<int> of int16 mono samples regardless of
 * the source's native format / channel count / bit-depth —
 * miniaudio converts on the fly. Use SampleRateOf to recover the
 * native sample rate from the file (so Play / SaveAsWav can
 * round-trip).
 */

/* Shared helper — load any file format miniaudio recognises into
 * a List<int> of int16 mono samples. Native sample rate is left
 * unchanged (miniaudio resamples only if the decoder config asks
 * for a specific rate; we pass 0 to keep the source rate). */
static inline AmalgameList* _amaudio_load_decoder(code_string path, const char* verb) {
    if (!path) {
        _amaudio_set_error(verb);
        return AmalgameList_new();
    }
    /* sampleRate=0 → keep source rate. format=s16, channels=1 →
     * downmix + bit-depth convert. */
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_s16, 1, 0);
    ma_decoder dec;
    if (ma_decoder_init_file(path, &cfg, &dec) != MA_SUCCESS) {
        _amaudio_set_error(verb);
        return AmalgameList_new();
    }
    ma_uint64 total = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &total) != MA_SUCCESS) {
        ma_decoder_uninit(&dec);
        _amaudio_set_error(verb);
        return AmalgameList_new();
    }
    int16_t* pcm = (int16_t*) malloc((size_t) total * sizeof(int16_t));
    if (!pcm) {
        ma_decoder_uninit(&dec);
        _amaudio_set_error(verb);
        return AmalgameList_new();
    }
    ma_uint64 got = 0;
    ma_decoder_read_pcm_frames(&dec, pcm, total, &got);
    ma_decoder_uninit(&dec);
    AmalgameList* out = _amaudio_buf_from_pcm(pcm, (size_t) got);
    free(pcm);
    _amaudio_set_error(NULL);
    return out;
}

/* Format-agnostic loader. miniaudio sniffs the magic bytes —
 * works for WAV / MP3 / FLAC / OGG-Vorbis. Returns an empty
 * List on failure (LastError carries the reason). */
static inline AmalgameList* Amalgame_Audio_Load(code_string path) {
    return _amaudio_load_decoder(path, "Load: ma_decoder_init_file failed");
}

/* Per-format loaders — same implementation as Load, distinct
 * verb so call-site code can express its intent. */
static inline AmalgameList* Amalgame_Audio_LoadMp3(code_string path) {
    return _amaudio_load_decoder(path, "LoadMp3: ma_decoder_init_file failed");
}
static inline AmalgameList* Amalgame_Audio_LoadFlac(code_string path) {
    return _amaudio_load_decoder(path, "LoadFlac: ma_decoder_init_file failed");
}
static inline AmalgameList* Amalgame_Audio_LoadOgg(code_string path) {
    return _amaudio_load_decoder(path, "LoadOgg: ma_decoder_init_file failed");
}

/* ── Sondes — inspect a file without loading the whole buffer ─ */

/* Sample rate of the audio file (Hz). Returns 0 on failure. */
static inline i64 Amalgame_Audio_SampleRateOf(code_string path) {
    if (!path) { _amaudio_set_error("SampleRateOf: null path"); return 0; }
    ma_decoder dec;
    if (ma_decoder_init_file(path, NULL, &dec) != MA_SUCCESS) {
        _amaudio_set_error("SampleRateOf: ma_decoder_init_file failed");
        return 0;
    }
    i64 rate = (i64) dec.outputSampleRate;
    ma_decoder_uninit(&dec);
    _amaudio_set_error(NULL);
    return rate;
}

/* Channel count of the audio file. Returns 0 on failure. */
static inline i64 Amalgame_Audio_ChannelCountOf(code_string path) {
    if (!path) { _amaudio_set_error("ChannelCountOf: null path"); return 0; }
    ma_decoder dec;
    if (ma_decoder_init_file(path, NULL, &dec) != MA_SUCCESS) {
        _amaudio_set_error("ChannelCountOf: ma_decoder_init_file failed");
        return 0;
    }
    i64 ch = (i64) dec.outputChannels;
    ma_decoder_uninit(&dec);
    _amaudio_set_error(NULL);
    return ch;
}

/* Total playback duration in milliseconds. Returns 0 on failure
 * (or on streams of unknown length, e.g. a malformed MP3). */
static inline i64 Amalgame_Audio_DurationMsOf(code_string path) {
    if (!path) { _amaudio_set_error("DurationMsOf: null path"); return 0; }
    ma_decoder dec;
    if (ma_decoder_init_file(path, NULL, &dec) != MA_SUCCESS) {
        _amaudio_set_error("DurationMsOf: ma_decoder_init_file failed");
        return 0;
    }
    ma_uint64 frames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &frames) != MA_SUCCESS) {
        ma_decoder_uninit(&dec);
        _amaudio_set_error("DurationMsOf: get_length failed");
        return 0;
    }
    i64 rate = (i64) dec.outputSampleRate;
    ma_decoder_uninit(&dec);
    _amaudio_set_error(NULL);
    if (rate <= 0) return 0;
    return (i64) ((frames * 1000) / (ma_uint64) rate);
}

#endif /* AMALGAME_AUDIO_H */
