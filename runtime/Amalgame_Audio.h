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
 *   ── v0.3 — mic capture ──
 *   Audio.Record(durSec, sampleRate)      -> List<int>   blocking
 *   Audio.RecordStart(maxSec, sampleRate) -> handle      non-blocking
 *   Audio.RecordStop(handle)              -> List<int>   drain + free
 *   Audio.RecordIsActive(handle)          -> bool
 *   Audio.RecordSampleCount(handle)       -> int         peek progress
 *
 *   ── v0.4 — streaming playback (Pause / Resume / Stop) ──
 *   Audio.PlayStart(samples, sampleRate)  -> handle      non-blocking
 *   Audio.PlayPause(handle)               -> bool        soft pause (silence)
 *   Audio.PlayResume(handle)              -> bool
 *   Audio.PlayStop(handle)                -> bool        uninit + free
 *   Audio.PlayIsActive(handle)            -> bool        still streaming
 *   Audio.PlayIsPaused(handle)            -> bool
 *   Audio.PlaySampleCount(handle)         -> int         current cursor
 *
 *   ── v0.5 — live AudioMixer (multi-source streaming) ──
 *   Audio.MixerStart(sampleRate)              -> handle
 *   Audio.MixerAddSource(mixer, samples, gain, loop) -> i64 voiceId
 *   Audio.MixerSetGain(mixer, voiceId, gain)  -> bool
 *   Audio.MixerSetPaused(mixer, voiceId, paused) -> bool
 *   Audio.MixerRemoveSource(mixer, voiceId)   -> bool
 *   Audio.MixerSourceCount(mixer)             -> int   alive voices
 *   Audio.MixerStop(mixer)                    -> bool  uninit + free
 *
 *   ── v0.6 — MIDI (Standard MIDI File IO + render) ──
 *   Audio.MidiLoadSmf(path)                          -> List<int> events
 *   Audio.MidiSaveSmf(path, events, tpq, tempoUs)    -> bool
 *   Audio.MidiRenderToAudio(events, tpq, tempoUs, sr)-> List<int> int16 mono
 *   Audio.MidiLastTicksPerQuarter()                  -> int
 *   Audio.MidiLastTempo()                            -> int  us per quarter
 *
 *   Event packing: events are returned as a flat List<int>,
 *   walked in groups of 4 — (delta_ticks, status, data1, data2).
 *   Supported messages: NoteOff/On, PolyAftertouch, CC,
 *   ProgramChange, ChanPressure, PitchBend, Tempo meta.
 *   Sysex / text meta / SMPTE division are out of scope.
 *
 * Threading: synthesis / transformation are pure (return new
 * buffers). Blocking `Play` / `Record` hold the calling thread
 * until the device drains / fills. Streaming `PlayStart` and
 * non-blocking `RecordStart` return immediately; their data
 * callbacks run on miniaudio's own thread but only touch a
 * pre-allocated C int16 buffer (no GC interaction). Concurrent
 * SaveAsWav/LoadWav calls against distinct paths are safe.
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

/* ═══════════════════════════════════════════════════════
 *  v0.3 — mic capture
 * ═══════════════════════════════════════════════════════
 *
 * Two surfaces:
 *   • Blocking — `Record(durSec, sampleRate)` returns a List<int>
 *     of int16 mono samples after recording for exactly durSec
 *     seconds. Symmetric to Play.
 *   • Non-blocking — `RecordStart` returns an opaque handle, the
 *     audio thread keeps appending to a pre-allocated int16 ring
 *     (actually a flat buffer with hard cap maxSec * sampleRate),
 *     `RecordStop` returns the captured List<int> and frees the
 *     handle. `RecordIsActive` peeks at the handle's run flag.
 *
 * Why the C-side int16 buffer (instead of writing directly into an
 * AmalgameList from the data callback)?
 *
 *   miniaudio drives the capture callback on its own OS thread.
 *   bdwgc (Boehm GC) only scans threads it has been told about —
 *   AmalgameList allocations from an un-registered thread would
 *   race the collector. Keeping the callback to pure
 *   memcpy/atomic-store against a pre-allocated C buffer
 *   sidesteps the issue entirely; the AmalgameList is built on
 *   the main (registered) thread after the device is uninit'd.
 *
 * Format is the same canonical contract as the rest of v0.1/v0.2:
 * 16-bit signed PCM mono. miniaudio downmixes whatever the
 * default input device produces.
 */

/* Opaque handle — `AmalgameAudioRec*` is what AM-side code sees,
 * the underlying struct stays implementation-private.
 *
 * `full` is signalled by the data callback once the pre-allocated
 * buffer is exhausted — the blocking `Record` waits on it. The
 * non-blocking trio (`RecordStart` / `RecordStop`) ignores it and
 * just lets the cursor advance until the user calls Stop, but the
 * field is initialised either way so cleanup paths are uniform.
 */
typedef struct AmalgameAudioRec {
    int16_t*       pcm;          /* pre-allocated cap * sizeof(int16_t) */
    ma_uint64      cap;          /* hard ceiling, in samples */
    ma_uint64      pos;          /* atomic-ish write cursor */
    ma_device      dev;
    ma_event       full;         /* signalled when pos reaches cap */
    int            dev_inited;   /* 1 once ma_device_init succeeded */
    int            evt_inited;   /* 1 once ma_event_init succeeded */
    int            running;      /* set 0 by RecordStop to drain the cb */
} AmalgameAudioRec;

static void _amaudio_record_cb(ma_device* dev, void* output, const void* input, ma_uint32 frames) {
    (void) output;
    AmalgameAudioRec* ctx = (AmalgameAudioRec*) dev->pUserData;
    if (!ctx || !ctx->running) return;
    ma_uint64 room = (ctx->cap > ctx->pos) ? (ctx->cap - ctx->pos) : 0;
    ma_uint64 take = (frames < room) ? (ma_uint64) frames : room;
    if (take > 0) {
        memcpy(ctx->pcm + ctx->pos, input, (size_t) take * sizeof(int16_t));
        ctx->pos += take;
    }
    if (ctx->pos >= ctx->cap) {
        ctx->running = 0;        /* buffer full */
        if (ctx->evt_inited) ma_event_signal(&ctx->full);
    }
}

/* Open the default capture device into ctx. Returns 1 on success,
 * 0 on failure (LastError carries the reason). Caller still owns
 * ctx->pcm — on failure we leave it as-is so the caller can free. */
static inline int _amaudio_rec_open(AmalgameAudioRec* ctx, i64 sampleRate, const char* verb) {
    ma_device_config dcfg = ma_device_config_init(ma_device_type_capture);
    dcfg.capture.format   = ma_format_s16;
    dcfg.capture.channels = 1;
    dcfg.sampleRate       = (ma_uint32) sampleRate;
    dcfg.dataCallback     = _amaudio_record_cb;
    dcfg.pUserData        = ctx;
    if (ma_device_init(NULL, &dcfg, &ctx->dev) != MA_SUCCESS) {
        _amaudio_set_error(verb);
        return 0;
    }
    ctx->dev_inited = 1;
    if (ma_device_start(&ctx->dev) != MA_SUCCESS) {
        ma_device_uninit(&ctx->dev);
        ctx->dev_inited = 0;
        _amaudio_set_error(verb);
        return 0;
    }
    return 1;
}

/* Blocking mic capture for `durSec` seconds at `sampleRate` Hz.
 * Returns a List<int> of int16 mono samples (length = durSec * sr).
 * On device-open failure returns an empty list — LastError tells
 * the user what went wrong (typical CI: "no default capture
 * device"). */
static inline AmalgameList* Amalgame_Audio_Record(f64 durSec, i64 sampleRate) {
    if (durSec <= 0.0 || sampleRate <= 0) {
        _amaudio_set_error("Record: durSec and sampleRate must be > 0");
        return AmalgameList_new();
    }
    ma_uint64 want = (ma_uint64) (durSec * (f64) sampleRate);
    if (want == 0) return AmalgameList_new();

    AmalgameAudioRec ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.pcm = (int16_t*) malloc((size_t) want * sizeof(int16_t));
    if (!ctx.pcm) { _amaudio_set_error("Record: OOM"); return AmalgameList_new(); }
    ctx.cap     = want;
    ctx.running = 1;

    if (ma_event_init(&ctx.full) != MA_SUCCESS) {
        free(ctx.pcm);
        _amaudio_set_error("Record: event_init failed");
        return AmalgameList_new();
    }
    ctx.evt_inited = 1;

    if (!_amaudio_rec_open(&ctx, sampleRate, "Record: device_init/start failed")) {
        ma_event_uninit(&ctx.full);
        free(ctx.pcm);
        return AmalgameList_new();
    }

    /* Block until the callback signals the buffer is full. The
     * data callback fires on miniaudio's audio thread, signals
     * `full` once `pos >= cap`, then this thread proceeds. */
    ma_event_wait(&ctx.full);

    ma_device_stop(&ctx.dev);
    ma_device_uninit(&ctx.dev);
    ma_event_uninit(&ctx.full);
    AmalgameList* out = _amaudio_buf_from_pcm(ctx.pcm, (size_t) ctx.pos);
    free(ctx.pcm);
    _amaudio_set_error(NULL);
    return out;
}

/* Start non-blocking capture. Pre-allocates a maxSec ceiling
 * (the callback never writes past it). Returns an opaque
 * handle as void*; pass it to RecordStop / RecordIsActive.
 * Returns NULL on failure (LastError populated). */
static inline AmalgameAudioRec* Amalgame_Audio_RecordStart(f64 maxSec, i64 sampleRate) {
    if (maxSec <= 0.0 || sampleRate <= 0) {
        _amaudio_set_error("RecordStart: maxSec and sampleRate must be > 0");
        return NULL;
    }
    ma_uint64 cap = (ma_uint64) (maxSec * (f64) sampleRate);
    if (cap == 0) {
        _amaudio_set_error("RecordStart: maxSec too short");
        return NULL;
    }
    AmalgameAudioRec* ctx = (AmalgameAudioRec*) calloc(1, sizeof(*ctx));
    if (!ctx) { _amaudio_set_error("RecordStart: OOM"); return NULL; }
    ctx->pcm = (int16_t*) malloc((size_t) cap * sizeof(int16_t));
    if (!ctx->pcm) {
        free(ctx);
        _amaudio_set_error("RecordStart: OOM");
        return NULL;
    }
    ctx->cap     = cap;
    ctx->running = 1;

    if (!_amaudio_rec_open(ctx, sampleRate, "RecordStart: device_init/start failed")) {
        free(ctx->pcm);
        free(ctx);
        return NULL;
    }
    _amaudio_set_error(NULL);
    return ctx;
}

/* Stop a non-blocking capture, drain the buffer into a
 * List<int>, and free the handle. Safe to call once per
 * handle; passing the same handle twice is a no-op returning
 * an empty list. */
static inline AmalgameList* Amalgame_Audio_RecordStop(AmalgameAudioRec* ctx) {
    if (!ctx) { _amaudio_set_error("RecordStop: null handle"); return AmalgameList_new(); }
    ctx->running = 0;
    if (ctx->dev_inited) {
        ma_device_stop(&ctx->dev);
        ma_device_uninit(&ctx->dev);
        ctx->dev_inited = 0;
    }
    AmalgameList* out = _amaudio_buf_from_pcm(ctx->pcm, (size_t) ctx->pos);
    free(ctx->pcm);
    free(ctx);
    _amaudio_set_error(NULL);
    return out;
}

/* True iff the capture device is still actively pulling samples
 * (i.e. RecordStop hasn't been called and the buffer hasn't
 * filled). Useful to poll from the AM side between work items. */
static inline code_bool Amalgame_Audio_RecordIsActive(AmalgameAudioRec* ctx) {
    if (!ctx) return 0;
    return ctx->running ? 1 : 0;
}

/* Returns how many samples have been captured so far on a live
 * handle (or post-mortem if you peek before RecordStop). */
static inline i64 Amalgame_Audio_RecordSampleCount(AmalgameAudioRec* ctx) {
    if (!ctx) return 0;
    return (i64) ctx->pos;
}

/* ═══════════════════════════════════════════════════════
 *  v0.4 — streaming playback (Pause / Resume / Stop)
 * ═══════════════════════════════════════════════════════
 *
 * `Audio.Play(buf, sr)` blocks the caller until the whole buffer
 * has been pushed to the device — fine for short cues, useless
 * the moment you want to pause / cancel mid-flight or layer
 * other work on top. v0.4 adds a handle-based streaming surface
 * symmetric to v0.3's capture trio:
 *
 *   AmalgameAudioPlay* h = Audio.PlayStart(buf, sr)
 *   Audio.PlayPause(h)         -> bool   (silence to device, pos frozen)
 *   Audio.PlayResume(h)        -> bool   (back to streaming)
 *   Audio.PlayStop(h)          -> bool   (uninit device, free handle)
 *   Audio.PlayIsActive(h)      -> bool   (running && pos < n)
 *   Audio.PlayIsPaused(h)      -> bool
 *   Audio.PlaySampleCount(h)   -> int    (current write cursor)
 *
 * Pause is a soft pause — the data callback writes silence into
 * the output frames while the `paused` flag is set, instead of
 * `ma_device_stop`-ing the device. Reasons:
 *
 *   - No audible pop on pause/resume (the device keeps running).
 *   - Reentry is cheap (toggle one int instead of teardown).
 *   - Avoids the rare ALSA / WASAPI quirks around stop+restart.
 *
 * Same bdwgc-safe pattern as capture: the data callback only
 * touches a pre-allocated C int16 buffer (a private copy of the
 * user's samples — we dup at PlayStart so the user can drop /
 * mutate their AmalgameList right after). Zero GC interaction
 * from the audio thread.
 */

typedef struct AmalgameAudioPlay {
    int16_t*       pcm;          /* owned copy of the source samples */
    ma_uint64      n;            /* total samples in pcm */
    ma_uint64      pos;          /* read cursor (advanced by callback) */
    ma_device      dev;
    int            dev_inited;   /* 1 once ma_device_init succeeded */
    int            running;      /* 0 once pos hits n OR PlayStop ran */
    int            paused;       /* set by PlayPause, cleared by PlayResume */
} AmalgameAudioPlay;

static void _amaudio_playstream_cb(ma_device* dev, void* output, const void* input, ma_uint32 frames) {
    (void) input;
    AmalgameAudioPlay* ctx = (AmalgameAudioPlay*) dev->pUserData;
    int16_t* out = (int16_t*) output;
    ma_uint64 want = (ma_uint64) frames;
    if (!ctx || !ctx->running || ctx->paused) {
        /* Silence: either we're done, stopped, or held paused.
         * Keep the device running so resume is glitch-free. */
        memset(out, 0, (size_t) want * sizeof(int16_t));
        return;
    }
    ma_uint64 remaining = (ctx->n > ctx->pos) ? (ctx->n - ctx->pos) : 0;
    ma_uint64 take = (want < remaining) ? want : remaining;
    if (take > 0) {
        memcpy(out, ctx->pcm + ctx->pos, (size_t) take * sizeof(int16_t));
        ctx->pos += take;
    }
    if (take < want) {
        memset(out + take, 0, (size_t) (want - take) * sizeof(int16_t));
        ctx->running = 0;            /* buffer drained — natural end */
    }
}

/* Start non-blocking playback. Dups the AmalgameList contents
 * into a private int16 buffer so the user is free to mutate /
 * drop their list right after. Returns NULL on device-open
 * failure (LastError populated — typical headless CI:
 * "no default playback device"). */
static inline AmalgameAudioPlay* Amalgame_Audio_PlayStart(AmalgameList* samples, i64 sampleRate) {
    if (!samples || sampleRate <= 0) {
        _amaudio_set_error("PlayStart: null samples or bad sampleRate");
        return NULL;
    }
    size_t n = 0;
    int16_t* pcm = _amaudio_pcm_from_buf(samples, &n);
    if (!pcm || n == 0) {
        free(pcm);
        _amaudio_set_error("PlayStart: empty buf");
        return NULL;
    }

    AmalgameAudioPlay* ctx = (AmalgameAudioPlay*) calloc(1, sizeof(*ctx));
    if (!ctx) {
        free(pcm);
        _amaudio_set_error("PlayStart: OOM");
        return NULL;
    }
    ctx->pcm     = pcm;
    ctx->n       = (ma_uint64) n;
    ctx->running = 1;

    ma_device_config dcfg = ma_device_config_init(ma_device_type_playback);
    dcfg.playback.format   = ma_format_s16;
    dcfg.playback.channels = 1;
    dcfg.sampleRate        = (ma_uint32) sampleRate;
    dcfg.dataCallback      = _amaudio_playstream_cb;
    dcfg.pUserData         = ctx;

    if (ma_device_init(NULL, &dcfg, &ctx->dev) != MA_SUCCESS) {
        free(ctx->pcm);
        free(ctx);
        _amaudio_set_error("PlayStart: device_init failed");
        return NULL;
    }
    ctx->dev_inited = 1;

    if (ma_device_start(&ctx->dev) != MA_SUCCESS) {
        ma_device_uninit(&ctx->dev);
        free(ctx->pcm);
        free(ctx);
        _amaudio_set_error("PlayStart: device_start failed");
        return NULL;
    }
    _amaudio_set_error(NULL);
    return ctx;
}

/* Soft pause — the data callback starts writing silence and the
 * read cursor freezes. Device stays running, so PlayResume is
 * pop-free. Safe to call multiple times; null handle is a no-op
 * returning false. */
static inline code_bool Amalgame_Audio_PlayPause(AmalgameAudioPlay* ctx) {
    if (!ctx) { _amaudio_set_error("PlayPause: null handle"); return 0; }
    ctx->paused = 1;
    _amaudio_set_error(NULL);
    return 1;
}

/* Clear the pause flag — callback resumes streaming from `pos`. */
static inline code_bool Amalgame_Audio_PlayResume(AmalgameAudioPlay* ctx) {
    if (!ctx) { _amaudio_set_error("PlayResume: null handle"); return 0; }
    ctx->paused = 0;
    _amaudio_set_error(NULL);
    return 1;
}

/* Stop playback, uninit the device, free the handle. Safe to
 * call once per handle; calling twice is a no-op returning
 * false. After PlayStop the pointer is dead — don't pass it to
 * any other Play* function. */
static inline code_bool Amalgame_Audio_PlayStop(AmalgameAudioPlay* ctx) {
    if (!ctx) { _amaudio_set_error("PlayStop: null handle"); return 0; }
    ctx->running = 0;
    if (ctx->dev_inited) {
        ma_device_stop(&ctx->dev);
        ma_device_uninit(&ctx->dev);
        ctx->dev_inited = 0;
    }
    free(ctx->pcm);
    free(ctx);
    _amaudio_set_error(NULL);
    return 1;
}

/* True iff the device is still streaming samples (i.e. PlayStop
 * hasn't been called AND the buffer hasn't been fully drained).
 * Returns false once the natural end-of-buffer is reached, even
 * before the user calls PlayStop. */
static inline code_bool Amalgame_Audio_PlayIsActive(AmalgameAudioPlay* ctx) {
    if (!ctx) return 0;
    return ctx->running ? 1 : 0;
}

static inline code_bool Amalgame_Audio_PlayIsPaused(AmalgameAudioPlay* ctx) {
    if (!ctx) return 0;
    return ctx->paused ? 1 : 0;
}

/* Current read cursor — how many samples have been pushed to
 * the device so far. Useful for "progress bar" UI or for
 * deciding when to layer another sound in. */
static inline i64 Amalgame_Audio_PlaySampleCount(AmalgameAudioPlay* ctx) {
    if (!ctx) return 0;
    return (i64) ctx->pos;
}

/* ═══════════════════════════════════════════════════════
 *  v0.5 — live AudioMixer (multi-source streaming)
 * ═══════════════════════════════════════════════════════
 *
 * `Audio.Mix(a, b, offset)` (v0.1) composes two buffers offline;
 * `Audio.PlayStart(buf, sr)` (v0.4) streams one buffer to the
 * device. Neither lets the user start a sound, then start
 * another *on top of it*, then stop the first while the second
 * keeps going. That's what a live mixer is for — and it's the
 * last piece of the v1 surface from Amalgame's ROADMAP.
 *
 * Public surface:
 *
 *   AmalgameAudioMixer* m = Audio.MixerStart(sampleRate)
 *   i64 voice = Audio.MixerAddSource(m, samples, gain, loop)
 *   Audio.MixerSetGain(m, voice, gain)         -> bool
 *   Audio.MixerSetPaused(m, voice, paused)     -> bool
 *   Audio.MixerRemoveSource(m, voice)          -> bool
 *   Audio.MixerSourceCount(m)                  -> int (alive only)
 *   Audio.MixerStop(m)                         -> bool   uninit + free
 *
 * Capacity: hard-capped at 32 voices. Why a fixed array instead
 * of a dynamic list? The audio callback iterates voices every
 * device tick (~10 ms); a linked-list with malloc in the audio
 * thread would be much worse than a 32-slot scan + bool check.
 * 32 voices is plenty for game audio / interactive demos —
 * orchestral mockups that need more are a v0.6+ ask.
 *
 * Concurrency: a `ma_mutex` protects the voices array. The
 * audio thread holds the lock for the duration of one callback
 * (~one frames-worth of mixing — typically 256-1024 samples).
 * Main-thread mutations (Add/Remove/SetGain/SetPaused) lock
 * briefly. Contention is negligible in practice.
 *
 * Voice IDs are monotonically increasing i64 — never reused
 * even after a voice drains and its slot is reclaimed. That
 * means stale ID lookups always cleanly return "not found"
 * instead of accidentally hitting a recycled slot.
 *
 * Same bdwgc-safe pattern as the rest of the package: source
 * PCM is dup'd at AddSource time into a private C buffer; the
 * audio callback never touches an AmalgameList.
 */

#define AMAUDIO_MIXER_MAX_VOICES 32

typedef struct {
    int            in_use;       /* 1 if slot holds a live source */
    i64            voice_id;     /* monotonic; 0 = empty slot */
    int16_t*       pcm;          /* owned copy of source samples */
    ma_uint64      n;            /* total samples */
    ma_uint64      pos;          /* read cursor */
    f64            gain;         /* per-voice gain (0..1+) */
    int            paused;       /* main-thread settable */
    int            loop;         /* wrap pos at n instead of finishing */
} _amaudio_mixer_voice;

typedef struct AmalgameAudioMixer {
    _amaudio_mixer_voice voices[AMAUDIO_MIXER_MAX_VOICES];
    ma_mutex             lock;
    ma_device            dev;
    int                  dev_inited;
    int                  lock_inited;
    int                  running;
    i64                  next_voice_id;
} AmalgameAudioMixer;

static void _amaudio_mixer_cb(ma_device* dev, void* output, const void* input, ma_uint32 frames) {
    (void) input;
    AmalgameAudioMixer* m = (AmalgameAudioMixer*) dev->pUserData;
    int16_t* out = (int16_t*) output;
    if (!m || !m->running) {
        memset(out, 0, (size_t) frames * sizeof(int16_t));
        return;
    }

    ma_mutex_lock(&m->lock);

    /* Sample-by-sample mix. We accumulate in f32 to avoid
     * int-overflow during 32-voice summation, clip to s16 at
     * the very end. */
    for (ma_uint32 f = 0; f < frames; f++) {
        f32 sum = 0.0f;
        for (int v = 0; v < AMAUDIO_MIXER_MAX_VOICES; v++) {
            _amaudio_mixer_voice* vc = &m->voices[v];
            if (!vc->in_use || vc->paused) continue;
            if (vc->pos >= vc->n) {
                if (vc->loop) {
                    vc->pos = 0;
                } else {
                    /* Drained — free the slot. Caller can poll
                     * SourceCount or call RemoveSource without
                     * worrying about double-free; in_use=0
                     * already pruned us here. */
                    vc->in_use = 0;
                    free(vc->pcm);
                    vc->pcm = NULL;
                    continue;
                }
            }
            sum += (f32) vc->pcm[vc->pos] * (f32) vc->gain;
            vc->pos++;
        }
        if (sum >  32767.0f) sum =  32767.0f;
        if (sum < -32768.0f) sum = -32768.0f;
        out[f] = (int16_t) sum;
    }

    ma_mutex_unlock(&m->lock);
}

/* Open a default-output device and start a mixer attached to
 * it. No initial sources — call MixerAddSource to layer in
 * voices. Returns NULL on device-open failure (LastError
 * populated; typical headless CI: "no default playback
 * device"). */
static inline AmalgameAudioMixer* Amalgame_Audio_MixerStart(i64 sampleRate) {
    if (sampleRate <= 0) {
        _amaudio_set_error("MixerStart: bad sampleRate");
        return NULL;
    }
    AmalgameAudioMixer* m = (AmalgameAudioMixer*) calloc(1, sizeof(*m));
    if (!m) { _amaudio_set_error("MixerStart: OOM"); return NULL; }
    m->running       = 1;
    m->next_voice_id = 1;

    if (ma_mutex_init(&m->lock) != MA_SUCCESS) {
        free(m);
        _amaudio_set_error("MixerStart: mutex_init failed");
        return NULL;
    }
    m->lock_inited = 1;

    ma_device_config dcfg = ma_device_config_init(ma_device_type_playback);
    dcfg.playback.format   = ma_format_s16;
    dcfg.playback.channels = 1;
    dcfg.sampleRate        = (ma_uint32) sampleRate;
    dcfg.dataCallback      = _amaudio_mixer_cb;
    dcfg.pUserData         = m;

    if (ma_device_init(NULL, &dcfg, &m->dev) != MA_SUCCESS) {
        ma_mutex_uninit(&m->lock);
        free(m);
        _amaudio_set_error("MixerStart: device_init failed");
        return NULL;
    }
    m->dev_inited = 1;

    if (ma_device_start(&m->dev) != MA_SUCCESS) {
        ma_device_uninit(&m->dev);
        ma_mutex_uninit(&m->lock);
        free(m);
        _amaudio_set_error("MixerStart: device_start failed");
        return NULL;
    }
    _amaudio_set_error(NULL);
    return m;
}

/* Add a source. Dups the AmalgameList contents into a private
 * int16 buffer. `loop = 1` wraps the cursor at end-of-buffer
 * (use for backgrounds / ambient pads); `loop = 0` lets the
 * voice drain and self-prune. Returns the new voice id, or 0
 * on failure (mixer full, OOM, null args). */
static inline i64 Amalgame_Audio_MixerAddSource(AmalgameAudioMixer* m, AmalgameList* samples, f64 gain, code_bool loop) {
    if (!m || !samples) { _amaudio_set_error("MixerAddSource: null arg"); return 0; }
    size_t n = 0;
    int16_t* pcm = _amaudio_pcm_from_buf(samples, &n);
    if (!pcm || n == 0) {
        free(pcm);
        _amaudio_set_error("MixerAddSource: empty samples");
        return 0;
    }

    ma_mutex_lock(&m->lock);

    int slot = -1;
    for (int v = 0; v < AMAUDIO_MIXER_MAX_VOICES; v++) {
        if (!m->voices[v].in_use) { slot = v; break; }
    }
    if (slot < 0) {
        ma_mutex_unlock(&m->lock);
        free(pcm);
        _amaudio_set_error("MixerAddSource: mixer full (32 voices max)");
        return 0;
    }
    i64 vid = m->next_voice_id++;
    _amaudio_mixer_voice* vc = &m->voices[slot];
    vc->in_use   = 1;
    vc->voice_id = vid;
    vc->pcm      = pcm;
    vc->n        = (ma_uint64) n;
    vc->pos      = 0;
    vc->gain     = gain;
    vc->paused   = 0;
    vc->loop     = loop ? 1 : 0;

    ma_mutex_unlock(&m->lock);
    _amaudio_set_error(NULL);
    return vid;
}

/* Find the voice with the given id and call the supplied
 * mutator under the lock. Returns true if found. */
static inline int _amaudio_mixer_find_locked(AmalgameAudioMixer* m, i64 voiceId) {
    for (int v = 0; v < AMAUDIO_MIXER_MAX_VOICES; v++) {
        if (m->voices[v].in_use && m->voices[v].voice_id == voiceId) return v;
    }
    return -1;
}

static inline code_bool Amalgame_Audio_MixerSetGain(AmalgameAudioMixer* m, i64 voiceId, f64 gain) {
    if (!m) return 0;
    ma_mutex_lock(&m->lock);
    int v = _amaudio_mixer_find_locked(m, voiceId);
    if (v < 0) { ma_mutex_unlock(&m->lock); return 0; }
    m->voices[v].gain = gain;
    ma_mutex_unlock(&m->lock);
    return 1;
}

static inline code_bool Amalgame_Audio_MixerSetPaused(AmalgameAudioMixer* m, i64 voiceId, code_bool paused) {
    if (!m) return 0;
    ma_mutex_lock(&m->lock);
    int v = _amaudio_mixer_find_locked(m, voiceId);
    if (v < 0) { ma_mutex_unlock(&m->lock); return 0; }
    m->voices[v].paused = paused ? 1 : 0;
    ma_mutex_unlock(&m->lock);
    return 1;
}

static inline code_bool Amalgame_Audio_MixerRemoveSource(AmalgameAudioMixer* m, i64 voiceId) {
    if (!m) return 0;
    ma_mutex_lock(&m->lock);
    int v = _amaudio_mixer_find_locked(m, voiceId);
    if (v < 0) { ma_mutex_unlock(&m->lock); return 0; }
    _amaudio_mixer_voice* vc = &m->voices[v];
    vc->in_use = 0;
    free(vc->pcm);
    vc->pcm = NULL;
    ma_mutex_unlock(&m->lock);
    return 1;
}

/* How many voices are currently alive (not drained, not
 * removed). Use this to decide when to add more layers vs.
 * waiting for old ones to finish. */
static inline i64 Amalgame_Audio_MixerSourceCount(AmalgameAudioMixer* m) {
    if (!m) return 0;
    i64 count = 0;
    ma_mutex_lock(&m->lock);
    for (int v = 0; v < AMAUDIO_MIXER_MAX_VOICES; v++) {
        if (m->voices[v].in_use) count++;
    }
    ma_mutex_unlock(&m->lock);
    return count;
}

/* Stop the mixer, uninit the device, free every voice's PCM
 * buffer, free the mixer. Safe to call once per mixer. */
static inline code_bool Amalgame_Audio_MixerStop(AmalgameAudioMixer* m) {
    if (!m) { _amaudio_set_error("MixerStop: null mixer"); return 0; }
    m->running = 0;
    if (m->dev_inited) {
        ma_device_stop(&m->dev);
        ma_device_uninit(&m->dev);
        m->dev_inited = 0;
    }
    /* Voices are now safe to drain on the main thread — the
     * audio thread has been stopped. */
    for (int v = 0; v < AMAUDIO_MIXER_MAX_VOICES; v++) {
        if (m->voices[v].pcm) {
            free(m->voices[v].pcm);
            m->voices[v].pcm = NULL;
        }
        m->voices[v].in_use = 0;
    }
    if (m->lock_inited) {
        ma_mutex_uninit(&m->lock);
        m->lock_inited = 0;
    }
    free(m);
    _amaudio_set_error(NULL);
    return 1;
}

/* ═══════════════════════════════════════════════════════
 *  v0.6 — MIDI (Standard MIDI File IO + render to audio)
 * ═══════════════════════════════════════════════════════
 *
 * Pure C, cross-platform, no external dep. Parses + writes
 * Standard MIDI File format 0 + format 1 (the two formats in
 * actual use; format 2 is exotic and out of scope).
 *
 * Event encoding into Amalgame:
 *
 *   Events come back as a flat `List<int>` of i64 values,
 *   walked in **groups of 4**: (delta_ticks, status, data1, data2).
 *   Caller iterates `for i in 0..events.Count() step 4`.
 *
 *   `status` is the MIDI status byte (0x80..0xFF). For channel
 *   voice messages the low nibble is the channel (0..15). For
 *   the tempo meta-event we use status=0xFF with data1=0x51
 *   and data2 = microseconds per quarter note.
 *
 *   This packed layout avoids an AmalgameList-of-AmalgameLists
 *   (one allocation per event) which would be unbearable for a
 *   typical multi-thousand-event SMF.
 *
 * Subset supported (read + write):
 *
 *   - NoteOff      0x80 | ch    data1=note  data2=velocity
 *   - NoteOn       0x90 | ch    data1=note  data2=velocity
 *   - PolyAftertch 0xA0 | ch    data1=note  data2=pressure
 *   - CC           0xB0 | ch    data1=cc#   data2=value
 *   - ProgChange   0xC0 | ch    data1=prog  data2=0
 *   - ChanPressure 0xD0 | ch    data1=press data2=0
 *   - PitchBend    0xE0 | ch    data1=lsb   data2=msb
 *   - Tempo meta   0xFF         data1=0x51  data2=usPerQuarter
 *
 * Skipped on read (parsed past but not emitted):
 *
 *   - All text/copyright/marker/cuepoint meta-events (0x01..0x07)
 *   - Sequence/instrument-name meta-events (0x03, 0x04)
 *   - Time signature / key signature meta-events (0x58, 0x59)
 *   - SMPTE offset (0x54)
 *   - Sysex (0xF0 + 0xF7)
 *   - End-of-track meta (0x2F) — marks track boundary internally
 *
 * Multi-track (format 1) merging:
 *
 *   We resolve each track's events into absolute ticks, merge
 *   into a single sorted timeline, then convert back to deltas
 *   in the returned list. The caller sees one linear stream as
 *   if the file were format 0.
 *
 * `Audio.MidiLoadSmf(path)` returns a `List<int>` (the packed
 * events) and updates `Audio.MidiLastTicksPerQuarter` /
 * `Audio.MidiLastTempo` so the caller can later turn deltas
 * into seconds. Empty list on failure (LastError populated).
 *
 * `Audio.MidiSaveSmf(path, events, ticksPerQuarter, tempoUs)`
 * writes a single-track format-0 SMF. We prepend the tempo
 * meta and append the end-of-track meta automatically.
 *
 * `Audio.MidiRenderToAudio(events, ticksPerQuarter, tempoUs,
 * sampleRate)` walks the event stream, tracks per-channel
 * active notes, and synthesises each note as a pure sine wave.
 * Multiple simultaneous notes mix additively with int16 clip.
 * Output is a `List<int>` you can `Audio.SaveAsWav` or
 * `Audio.Play` directly.
 */

/* Module-globals — populated by MidiLoadSmf for callers who
 * later need to convert deltas into wallclock time. */
static i64 _amaudio_midi_last_tpq   = 480;       /* default if file omits division */
static i64 _amaudio_midi_last_tempo = 500000;    /* 500_000 us = 120 BPM, MIDI default */

static inline i64 Amalgame_Audio_MidiLastTicksPerQuarter(void) { return _amaudio_midi_last_tpq; }
static inline i64 Amalgame_Audio_MidiLastTempo(void)           { return _amaudio_midi_last_tempo; }

/* ── SMF parser internals ──────────────────────────────── */

/* Read a variable-length quantity (1..4 bytes, 7 bits each, top
 * bit = continuation). Returns the value, advances *cursor. */
static inline uint32_t _amaudio_midi_read_vlq(const uint8_t* buf, size_t buflen, size_t* cursor) {
    uint32_t value = 0;
    while (*cursor < buflen) {
        uint8_t b = buf[*cursor];
        (*cursor)++;
        value = (value << 7) | (b & 0x7F);
        if ((b & 0x80) == 0) return value;
    }
    return value;
}

/* Read big-endian u16 / u32 — SMF is always big-endian. */
static inline uint16_t _amaudio_midi_read_u16(const uint8_t* p) {
    return (uint16_t) ((p[0] << 8) | p[1]);
}
static inline uint32_t _amaudio_midi_read_u32(const uint8_t* p) {
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] <<  8) |  (uint32_t) p[3];
}

/* One parsed event with an *absolute* tick — we sort by this
 * across tracks before flattening back into deltas. Used only
 * inside MidiLoadSmf; not exposed. */
typedef struct {
    uint64_t tick;
    uint8_t  status;
    uint8_t  data1;
    int32_t  data2;   /* widened for tempo's 24-bit value */
} _amaudio_midi_event;

/* Comparator for qsort — by absolute tick, stable on insertion
 * order via a secondary index trick (we keep input order by
 * appending events sequentially; equal-tick events keep their
 * file order through std qsort's instability tolerance — for
 * typical SMFs the order is "as-written" which is what users
 * expect). */
static inline int _amaudio_midi_evt_cmp(const void* a, const void* b) {
    uint64_t ta = ((const _amaudio_midi_event*) a)->tick;
    uint64_t tb = ((const _amaudio_midi_event*) b)->tick;
    if (ta < tb) return -1;
    if (ta > tb) return 1;
    return 0;
}

/* Parse one track's body into our event vector. `track` points
 * at the first byte after the MTrk-length header. */
static inline int _amaudio_midi_parse_track(const uint8_t* track, size_t track_len,
                                            _amaudio_midi_event** events, size_t* count, size_t* cap) {
    size_t cursor = 0;
    uint64_t abs_tick = 0;
    uint8_t  running_status = 0;

    while (cursor < track_len) {
        uint32_t delta = _amaudio_midi_read_vlq(track, track_len, &cursor);
        abs_tick += delta;
        if (cursor >= track_len) break;

        uint8_t status = track[cursor];
        if (status & 0x80) {
            cursor++;
            running_status = status;
        } else {
            /* Running status — reuse last status byte. */
            status = running_status;
            if (status == 0) {
                _amaudio_set_error("MidiLoadSmf: missing status byte");
                return 0;
            }
        }

        uint8_t hi = status & 0xF0;
        uint8_t emit_status = 0;
        uint8_t emit_d1 = 0;
        int32_t emit_d2 = 0;
        int do_emit = 0;

        if (status == 0xFF) {
            /* Meta-event */
            if (cursor >= track_len) break;
            uint8_t type = track[cursor++];
            uint32_t mlen = _amaudio_midi_read_vlq(track, track_len, &cursor);
            if (cursor + mlen > track_len) {
                _amaudio_set_error("MidiLoadSmf: meta-event overruns track");
                return 0;
            }
            if (type == 0x51 && mlen == 3) {
                /* Set Tempo — 24-bit big-endian us per quarter. */
                emit_status = 0xFF;
                emit_d1     = 0x51;
                emit_d2     = ((int32_t) track[cursor]     << 16) |
                              ((int32_t) track[cursor + 1] <<  8) |
                              ((int32_t) track[cursor + 2]);
                do_emit = 1;
            }
            /* 0x2F = end-of-track; we skip emitting but stop
             * parsing this track since the byte after is the
             * start of the next track (or EOF). */
            cursor += mlen;
            if (type == 0x2F) break;
        } else if (status == 0xF0 || status == 0xF7) {
            /* Sysex — skip its var-length-prefixed payload. */
            uint32_t slen = _amaudio_midi_read_vlq(track, track_len, &cursor);
            if (cursor + slen > track_len) {
                _amaudio_set_error("MidiLoadSmf: sysex overruns track");
                return 0;
            }
            cursor += slen;
            running_status = 0;   /* sysex clears running status */
        } else if (hi == 0xC0 || hi == 0xD0) {
            /* 1-byte messages: ProgramChange (0xCn), ChanPressure (0xDn) */
            if (cursor >= track_len) break;
            emit_status = status;
            emit_d1     = track[cursor++];
            emit_d2     = 0;
            do_emit = 1;
        } else if (hi == 0x80 || hi == 0x90 || hi == 0xA0 || hi == 0xB0 || hi == 0xE0) {
            /* 2-byte messages */
            if (cursor + 1 >= track_len) break;
            emit_status = status;
            emit_d1     = track[cursor++];
            emit_d2     = (int32_t) track[cursor++];
            do_emit = 1;
        } else {
            /* Unknown — give up cleanly on this track. */
            _amaudio_set_error("MidiLoadSmf: unknown status byte");
            return 0;
        }

        if (do_emit) {
            if (*count >= *cap) {
                *cap = (*cap == 0) ? 64 : (*cap * 2);
                _amaudio_midi_event* re = (_amaudio_midi_event*) realloc(*events, *cap * sizeof(**events));
                if (!re) { _amaudio_set_error("MidiLoadSmf: OOM"); return 0; }
                *events = re;
            }
            (*events)[*count].tick    = abs_tick;
            (*events)[*count].status  = emit_status;
            (*events)[*count].data1   = emit_d1;
            (*events)[*count].data2   = emit_d2;
            (*count)++;
        }
    }
    return 1;
}

/* Public: load an SMF file, return events as flat List<int>
 * (groups of 4: delta_ticks, status, data1, data2). Also
 * populates the MidiLast* sondes. */
static inline AmalgameList* Amalgame_Audio_MidiLoadSmf(code_string path) {
    if (!path) { _amaudio_set_error("MidiLoadSmf: null path"); return AmalgameList_new(); }
    FILE* fp = fopen(path, "rb");
    if (!fp) { _amaudio_set_error("MidiLoadSmf: open failed"); return AmalgameList_new(); }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (fsize < 14) {
        fclose(fp);
        _amaudio_set_error("MidiLoadSmf: file too small (no MThd header)");
        return AmalgameList_new();
    }
    uint8_t* file = (uint8_t*) malloc((size_t) fsize);
    if (!file) { fclose(fp); _amaudio_set_error("MidiLoadSmf: OOM"); return AmalgameList_new(); }
    if (fread(file, 1, (size_t) fsize, fp) != (size_t) fsize) {
        fclose(fp); free(file);
        _amaudio_set_error("MidiLoadSmf: short read");
        return AmalgameList_new();
    }
    fclose(fp);

    if (memcmp(file, "MThd", 4) != 0) {
        free(file);
        _amaudio_set_error("MidiLoadSmf: not an SMF (no MThd)");
        return AmalgameList_new();
    }
    uint32_t header_len = _amaudio_midi_read_u32(file + 4);
    if (header_len < 6) {
        free(file);
        _amaudio_set_error("MidiLoadSmf: bad MThd length");
        return AmalgameList_new();
    }
    uint16_t format    = _amaudio_midi_read_u16(file + 8);
    uint16_t ntracks   = _amaudio_midi_read_u16(file + 10);
    uint16_t division  = _amaudio_midi_read_u16(file + 12);
    if (format > 1) {
        free(file);
        _amaudio_set_error("MidiLoadSmf: format 2 not supported");
        return AmalgameList_new();
    }
    if (division & 0x8000) {
        /* SMPTE timecode division — out of scope; reject. */
        free(file);
        _amaudio_set_error("MidiLoadSmf: SMPTE division not supported");
        return AmalgameList_new();
    }
    _amaudio_midi_last_tpq   = (i64) division;
    _amaudio_midi_last_tempo = 500000;  /* default 120 BPM */

    _amaudio_midi_event* events = NULL;
    size_t count = 0, cap = 0;
    size_t pos = 8 + header_len;

    for (uint16_t t = 0; t < ntracks; t++) {
        if (pos + 8 > (size_t) fsize) break;
        if (memcmp(file + pos, "MTrk", 4) != 0) {
            free(events); free(file);
            _amaudio_set_error("MidiLoadSmf: missing MTrk header");
            return AmalgameList_new();
        }
        uint32_t track_len = _amaudio_midi_read_u32(file + pos + 4);
        if (pos + 8 + track_len > (size_t) fsize) {
            free(events); free(file);
            _amaudio_set_error("MidiLoadSmf: track overruns file");
            return AmalgameList_new();
        }
        if (!_amaudio_midi_parse_track(file + pos + 8, (size_t) track_len, &events, &count, &cap)) {
            free(events); free(file);
            return AmalgameList_new();   /* LastError already set */
        }
        pos += 8 + track_len;
    }
    free(file);

    /* Merge: sort by absolute tick across all tracks. The first
     * tempo event we encounter (after sorting) sets MidiLastTempo
     * — most SMFs put it at tick 0. */
    qsort(events, count, sizeof(*events), _amaudio_midi_evt_cmp);

    AmalgameList* out = AmalgameList_new();
    uint64_t last_tick = 0;
    for (size_t i = 0; i < count; i++) {
        uint64_t delta = events[i].tick - last_tick;
        last_tick = events[i].tick;
        AmalgameList_add(out, (void*) (intptr_t) (i64) delta);
        AmalgameList_add(out, (void*) (intptr_t) (i64) events[i].status);
        AmalgameList_add(out, (void*) (intptr_t) (i64) events[i].data1);
        AmalgameList_add(out, (void*) (intptr_t) (i64) events[i].data2);
        if (events[i].status == 0xFF && events[i].data1 == 0x51 && _amaudio_midi_last_tempo == 500000) {
            _amaudio_midi_last_tempo = (i64) events[i].data2;
        }
    }
    free(events);
    _amaudio_set_error(NULL);
    return out;
}

/* ── SMF writer ────────────────────────────────────────── */

/* Append a variable-length quantity to a growing byte buffer. */
static inline void _amaudio_midi_write_vlq(uint8_t** buf, size_t* len, size_t* cap, uint32_t value) {
    uint8_t bytes[4];
    int n = 0;
    bytes[n++] = (uint8_t) (value & 0x7F);
    value >>= 7;
    while (value > 0 && n < 4) {
        bytes[n++] = (uint8_t) ((value & 0x7F) | 0x80);
        value >>= 7;
    }
    /* Emit MSB-first (highest-magnitude byte first, top bit set
     * for all but the last). Reverse the array we built bottom-up. */
    while (*len + (size_t) n > *cap) {
        *cap = (*cap == 0) ? 64 : (*cap * 2);
        *buf = (uint8_t*) realloc(*buf, *cap);
    }
    for (int i = n - 1; i >= 0; i--) (*buf)[(*len)++] = bytes[i];
}

static inline void _amaudio_midi_append(uint8_t** buf, size_t* len, size_t* cap, const void* src, size_t srclen) {
    while (*len + srclen > *cap) {
        *cap = (*cap == 0) ? 64 : (*cap * 2);
        *buf = (uint8_t*) realloc(*buf, *cap);
    }
    memcpy(*buf + *len, src, srclen);
    *len += srclen;
}
static inline void _amaudio_midi_append_u8(uint8_t** b, size_t* l, size_t* c, uint8_t v) {
    _amaudio_midi_append(b, l, c, &v, 1);
}
static inline void _amaudio_midi_append_u16(uint8_t** b, size_t* l, size_t* c, uint16_t v) {
    uint8_t bytes[2] = { (uint8_t)(v >> 8), (uint8_t)(v & 0xFF) };
    _amaudio_midi_append(b, l, c, bytes, 2);
}
static inline void _amaudio_midi_append_u32(uint8_t** b, size_t* l, size_t* c, uint32_t v) {
    uint8_t bytes[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16),
                         (uint8_t)(v >>  8), (uint8_t)(v & 0xFF) };
    _amaudio_midi_append(b, l, c, bytes, 4);
}

/* Public: write a single-track format-0 SMF. Prepends a Set-
 * Tempo meta at tick 0, appends End-of-Track at the end. */
static inline code_bool Amalgame_Audio_MidiSaveSmf(code_string path, AmalgameList* events,
                                                   i64 ticksPerQuarter, i64 tempoUsPerQuarter) {
    if (!path || !events) { _amaudio_set_error("MidiSaveSmf: null arg"); return 0; }
    if (ticksPerQuarter <= 0 || ticksPerQuarter > 0x7FFF) {
        _amaudio_set_error("MidiSaveSmf: ticksPerQuarter out of range");
        return 0;
    }
    if (tempoUsPerQuarter <= 0 || tempoUsPerQuarter > 0xFFFFFF) {
        _amaudio_set_error("MidiSaveSmf: tempo out of range");
        return 0;
    }
    size_t n = (size_t) AmalgameList_count(events);
    if (n % 4 != 0) {
        _amaudio_set_error("MidiSaveSmf: event list count must be a multiple of 4");
        return 0;
    }

    /* Build the MTrk body first so we know its length. */
    uint8_t* track = NULL;
    size_t   tlen = 0, tcap = 0;

    /* Tempo meta at delta=0. */
    _amaudio_midi_write_vlq(&track, &tlen, &tcap, 0);
    _amaudio_midi_append_u8(&track, &tlen, &tcap, 0xFF);
    _amaudio_midi_append_u8(&track, &tlen, &tcap, 0x51);
    _amaudio_midi_append_u8(&track, &tlen, &tcap, 0x03);
    _amaudio_midi_append_u8(&track, &tlen, &tcap, (uint8_t) (tempoUsPerQuarter >> 16));
    _amaudio_midi_append_u8(&track, &tlen, &tcap, (uint8_t) (tempoUsPerQuarter >>  8));
    _amaudio_midi_append_u8(&track, &tlen, &tcap, (uint8_t) (tempoUsPerQuarter & 0xFF));

    for (size_t i = 0; i < n; i += 4) {
        i64 delta  = (i64) (intptr_t) AmalgameList_get(events, (i64) i);
        i64 status = (i64) (intptr_t) AmalgameList_get(events, (i64) (i + 1));
        i64 d1     = (i64) (intptr_t) AmalgameList_get(events, (i64) (i + 2));
        i64 d2     = (i64) (intptr_t) AmalgameList_get(events, (i64) (i + 3));
        _amaudio_midi_write_vlq(&track, &tlen, &tcap, (uint32_t) delta);
        if (status == 0xFF && d1 == 0x51) {
            /* User-supplied tempo meta — write as 24-bit. */
            _amaudio_midi_append_u8(&track, &tlen, &tcap, 0xFF);
            _amaudio_midi_append_u8(&track, &tlen, &tcap, 0x51);
            _amaudio_midi_append_u8(&track, &tlen, &tcap, 0x03);
            _amaudio_midi_append_u8(&track, &tlen, &tcap, (uint8_t) (d2 >> 16));
            _amaudio_midi_append_u8(&track, &tlen, &tcap, (uint8_t) (d2 >>  8));
            _amaudio_midi_append_u8(&track, &tlen, &tcap, (uint8_t) (d2 & 0xFF));
            continue;
        }
        uint8_t hi = (uint8_t) (status & 0xF0);
        _amaudio_midi_append_u8(&track, &tlen, &tcap, (uint8_t) status);
        _amaudio_midi_append_u8(&track, &tlen, &tcap, (uint8_t) (d1 & 0x7F));
        if (hi != 0xC0 && hi != 0xD0) {
            /* 2-byte channel messages */
            _amaudio_midi_append_u8(&track, &tlen, &tcap, (uint8_t) (d2 & 0x7F));
        }
    }

    /* End-of-Track meta */
    _amaudio_midi_write_vlq(&track, &tlen, &tcap, 0);
    _amaudio_midi_append_u8(&track, &tlen, &tcap, 0xFF);
    _amaudio_midi_append_u8(&track, &tlen, &tcap, 0x2F);
    _amaudio_midi_append_u8(&track, &tlen, &tcap, 0x00);

    /* Assemble final file. */
    FILE* fp = fopen(path, "wb");
    if (!fp) {
        free(track);
        _amaudio_set_error("MidiSaveSmf: open for write failed");
        return 0;
    }
    /* MThd */
    uint8_t* hdr = NULL; size_t hlen = 0, hcap = 0;
    _amaudio_midi_append(&hdr, &hlen, &hcap, "MThd", 4);
    _amaudio_midi_append_u32(&hdr, &hlen, &hcap, 6);
    _amaudio_midi_append_u16(&hdr, &hlen, &hcap, 0);                       /* format 0 */
    _amaudio_midi_append_u16(&hdr, &hlen, &hcap, 1);                       /* 1 track */
    _amaudio_midi_append_u16(&hdr, &hlen, &hcap, (uint16_t) ticksPerQuarter);
    _amaudio_midi_append(&hdr, &hlen, &hcap, "MTrk", 4);
    _amaudio_midi_append_u32(&hdr, &hlen, &hcap, (uint32_t) tlen);

    int ok = (fwrite(hdr, 1, hlen, fp) == hlen) && (fwrite(track, 1, tlen, fp) == tlen);
    free(hdr); free(track);
    fclose(fp);
    if (!ok) { _amaudio_set_error("MidiSaveSmf: write failed"); return 0; }
    _amaudio_set_error(NULL);
    return 1;
}

/* ── MidiRenderToAudio ─────────────────────────────────── */

/* Convert MIDI note number (0..127) to frequency in Hz.
 * MIDI 69 = A4 = 440 Hz; each semitone = factor of 2^(1/12). */
static inline f64 _amaudio_midi_note_to_hz(int note) {
    return 440.0 * pow(2.0, ((f64) note - 69.0) / 12.0);
}

/* Synthesise the supplied event stream into mono int16 audio.
 * Each NoteOn starts a sine wave at the note's frequency,
 * scaled by velocity / 127. NoteOff (or NoteOn with velocity 0)
 * stops it. Concurrent notes mix additively with int16 clip.
 *
 * Tempo meta-events update the wallclock rate. The first tempo
 * encountered (if any) before any audio is generated displaces
 * the supplied default; subsequent tempos take effect from
 * their event point onwards.
 *
 * Each active note is stored in a slim per-pitch-per-channel
 * state table (16 × 128 = 2048 slots) — cheap, lookup is O(1). */
static inline AmalgameList* Amalgame_Audio_MidiRenderToAudio(AmalgameList* events,
                                                              i64 ticksPerQuarter,
                                                              i64 tempoUsPerQuarter,
                                                              i64 sampleRate) {
    if (!events || ticksPerQuarter <= 0 || tempoUsPerQuarter <= 0 || sampleRate <= 0) {
        _amaudio_set_error("MidiRenderToAudio: bad arg");
        return AmalgameList_new();
    }
    size_t n = (size_t) AmalgameList_count(events);
    if (n == 0 || (n % 4) != 0) {
        _amaudio_set_error("MidiRenderToAudio: bad event count");
        return AmalgameList_new();
    }

    /* Active-note table: per (channel, note) we track velocity
     * (0 = inactive) + the running phase index so re-trigger
     * doesn't pop. */
    int32_t  vel[16][128];
    uint64_t phase[16][128];
    memset(vel, 0, sizeof(vel));
    memset(phase, 0, sizeof(phase));

    f64 us_per_tick = (f64) tempoUsPerQuarter / (f64) ticksPerQuarter;
    f64 samples_per_us = (f64) sampleRate / 1000000.0;
    f64 samples_per_tick = us_per_tick * samples_per_us;

    /* Pre-allocate a generous initial buffer based on a rough
     * upper bound (sum of all deltas × samples_per_tick + tail). */
    uint64_t total_ticks = 0;
    for (size_t i = 0; i < n; i += 4) {
        total_ticks += (uint64_t) (intptr_t) AmalgameList_get(events, (i64) i);
    }
    size_t initial = (size_t) ((f64) total_ticks * samples_per_tick) + (size_t) sampleRate;   /* + 1 s tail */
    int16_t* out = (int16_t*) calloc(initial > 0 ? initial : 1, sizeof(int16_t));
    size_t out_cap = initial > 0 ? initial : 1;
    size_t out_len = 0;
    if (!out) { _amaudio_set_error("MidiRenderToAudio: OOM"); return AmalgameList_new(); }

    /* For each gap between events, render `gap_samples` of mixed
     * audio from currently-active notes, advance phases. */
    for (size_t i = 0; i < n; i += 4) {
        i64 delta  = (i64) (intptr_t) AmalgameList_get(events, (i64) i);
        i64 status = (i64) (intptr_t) AmalgameList_get(events, (i64) (i + 1));
        i64 d1     = (i64) (intptr_t) AmalgameList_get(events, (i64) (i + 2));
        i64 d2     = (i64) (intptr_t) AmalgameList_get(events, (i64) (i + 3));

        uint64_t gap_samples = (uint64_t) ((f64) delta * samples_per_tick);
        if (out_len + (size_t) gap_samples > out_cap) {
            size_t new_cap = out_cap * 2;
            while (new_cap < out_len + (size_t) gap_samples + (size_t) sampleRate) new_cap *= 2;
            int16_t* re = (int16_t*) realloc(out, new_cap * sizeof(int16_t));
            if (!re) { free(out); _amaudio_set_error("MidiRenderToAudio: OOM"); return AmalgameList_new(); }
            out = re;
            memset(out + out_cap, 0, (new_cap - out_cap) * sizeof(int16_t));
            out_cap = new_cap;
        }

        for (uint64_t s = 0; s < gap_samples; s++) {
            f64 sum = 0.0;
            for (int ch = 0; ch < 16; ch++) {
                for (int nt = 0; nt < 128; nt++) {
                    if (vel[ch][nt] == 0) continue;
                    f64 hz = _amaudio_midi_note_to_hz(nt);
                    f64 step = 2.0 * AMALGAME_AUDIO_PI * hz / (f64) sampleRate;
                    f64 amp = (f64) vel[ch][nt] / 127.0;
                    sum += sin((f64) phase[ch][nt] * step) * 32767.0 * amp * 0.20;   /* 0.2 = headroom for ~5 simul notes */
                    phase[ch][nt]++;
                }
            }
            if (sum >  32767.0) sum =  32767.0;
            if (sum < -32768.0) sum = -32768.0;
            out[out_len++] = (int16_t) sum;
        }

        /* Apply the event itself. */
        if (status == 0xFF && d1 == 0x51) {
            tempoUsPerQuarter = d2;
            us_per_tick = (f64) tempoUsPerQuarter / (f64) ticksPerQuarter;
            samples_per_tick = us_per_tick * samples_per_us;
        } else {
            uint8_t hi = (uint8_t) (status & 0xF0);
            int ch = (int) (status & 0x0F);
            if (hi == 0x90) {
                /* Note On — velocity 0 == NoteOff */
                int note = (int) (d1 & 0x7F);
                if (d2 > 0) {
                    vel[ch][note] = (int32_t) d2;
                    /* Don't reset phase — re-triggering same note
                     * mid-flight just refreshes velocity, which
                     * sounds smoother than a click. */
                } else {
                    vel[ch][note] = 0;
                }
            } else if (hi == 0x80) {
                int note = (int) (d1 & 0x7F);
                vel[ch][note] = 0;
            }
            /* Other channel messages — ignored for synth (no
             * pitch bend / CC support in v0.6; v0.7 ask). */
        }
    }

    AmalgameList* result = _amaudio_buf_from_pcm(out, out_len);
    free(out);
    _amaudio_set_error(NULL);
    return result;
}

#endif /* AMALGAME_AUDIO_H */
