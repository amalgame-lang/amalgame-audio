# amalgame-audio

Audio synthesis, playback, capture and multi-format decode for [Amalgame](https://github.com/amalgame-lang/Amalgame).
Wraps David Reid's public-domain [miniaudio](https://github.com/mackron/miniaudio)
single-header library (vendored at `runtime/vendor/`). miniaudio
abstracts the OS-native audio APIs (WASAPI on Windows, Core Audio
on macOS, ALSA/PulseAudio on Linux) behind a single API — we wrap
that.

## Prerequisites

miniaudio's implementation `dlopen`s the OS-native audio library
**at runtime**, so the host machine where the user binary actually
plays sound needs the matching shared lib installed. **No extra
package is required at build time** — miniaudio is vendored in
this package, no `-dev` header to install.

### Runtime (target machine that plays sound)

| OS / distro | Runtime audio library | Install if missing |
|---|---|---|
| Debian / Ubuntu | ALSA (default) + PulseAudio | `apt install libasound2 libpulse0` (usually pre-installed) |
| Fedora / RHEL | ALSA + PulseAudio / PipeWire | `dnf install alsa-lib pulseaudio-libs` (usually pre-installed) |
| Arch / Manjaro | ALSA + PulseAudio / PipeWire | usually pre-installed |
| Alpine | ALSA / PulseAudio | `apk add alsa-lib pulseaudio` |
| macOS | Core Audio | built into the OS, nothing to install |
| Windows | WASAPI / DirectSound / WinMM | built into the OS, nothing to install |
| BSD | sndio / OSS | usually pre-installed |

No build-time package is needed on any OS — miniaudio.h is
header-only and vendored. The `-lm -ldl -lpthread` link flags
are part of the manifest and forwarded automatically.

## Install

```bash
amc package add audio                                  # via index
amc package add github.com/amalgame-lang/amalgame-audio@v0.6.0
```

Requires **amc 0.5.4+** (precompile-on-install — miniaudio's
~100k LoC compile is heavy enough that the per-platform cache
matters).

## Surface

```amalgame
import Amalgame.Audio

public class Beep {
    public static void Main() {
        // 1 kHz tone, 0.5 second, 44.1 kHz sample rate
        let ping = Audio.GenSine(1000.0, 0.5, 44100)

        // Add a 10 ms attack/release envelope so it doesn't click
        let shaped = Audio.ApplyEnvelope(ping, 10.0, 10.0, 44100)

        // Play through the default output device (blocking)
        Audio.Play(shaped, 44100)

        // Or save to disk to share / inspect
        Audio.SaveAsWav(shaped, 44100, "beep.wav")
    }
}
```

### v0.1.0 method surface

| Method | Returns | Notes |
|---|---|---|
| **Synthesis** | | All return `List<int>` of int16 mono samples |
| `Audio.GenSine(freqHz, durSec, sampleRate)` | `List<int>` | Pure sinusoid, full-amplitude |
| `Audio.GenSquare(freqHz, durSec, sampleRate)` | `List<int>` | Square wave (full-amp) |
| `Audio.GenTriangle(freqHz, durSec, sampleRate)` | `List<int>` | Triangle wave |
| `Audio.GenNoise(durSec, sampleRate)` | `List<int>` | White noise (xorshift) |
| `Audio.GenSilence(durSec, sampleRate)` | `List<int>` | All zeros |
| **Transform** | | Pure — return fresh buffers |
| `Audio.ApplyEnvelope(samples, attackMs, releaseMs, sr)` | `List<int>` | Linear ADSR (sustain implicit) |
| `Audio.Scale(samples, gain)` | `List<int>` | Multiply every sample (clips int16) |
| `Audio.Mix(a, b, offsetSamples)` | `List<int>` | Overlay b on a at offset |
| `Audio.Echo(samples, delayMs, decay, repeats, sr)` | `List<int>` | Build feedback echo |
| **IO** | | |
| `Audio.SaveAsWav(samples, sr, path)` | `bool` | 16-bit PCM mono WAV |
| `Audio.LoadWav(path)` | `List<int>` | Decode (auto-converts to mono int16) |
| `Audio.Play(samples, sr)` | `bool` | Blocking playback (default device) |
| **Diag** | | |
| `Audio.LastError()` | `string` | Empty on success |

### v0.2.0 additions — multi-format decoders + sondes

| Method | Returns | Notes |
|---|---|---|
| `Audio.Load(path)` | `List<int>` | Auto-detects WAV / MP3 / FLAC / OGG via magic bytes |
| `Audio.LoadMp3(path)` | `List<int>` | Same impl, explicit verb for call-site clarity |
| `Audio.LoadFlac(path)` | `List<int>` | Same impl, explicit verb |
| `Audio.LoadOgg(path)` | `List<int>` | Same impl, explicit verb |
| `Audio.SampleRateOf(path)` | `int` (Hz) | Inspect without loading; returns 0 on failure |
| `Audio.ChannelCountOf(path)` | `int` | 1 = mono, 2 = stereo, … |
| `Audio.DurationMsOf(path)` | `int` (ms) | Total playback duration |

```amalgame
// Load any audio file the codec stack recognises and inspect
// its native rate / channel count / duration before deciding
// what to do with it.
let buf = Audio.Load("/tmp/whatever.mp3")
Console.WriteLine("rate: " + String_FromInt(Audio.SampleRateOf("/tmp/whatever.mp3")))
Console.WriteLine("ch:   " + String_FromInt(Audio.ChannelCountOf("/tmp/whatever.mp3")))
Console.WriteLine("dur:  " + String_FromInt(Audio.DurationMsOf("/tmp/whatever.mp3")) + " ms")
Audio.Play(buf, Audio.SampleRateOf("/tmp/whatever.mp3"))
```

All loaders downmix to mono int16 — multi-channel pipelines are
v0.5+. The `.o` produced from `ma_impl.c` grows from ~250 KB
(WAV-only v0.1) to ~1 MB (v0.2 with MP3/FLAC/OGG enabled);
acceptable for native multi-format playback without forking the
package per codec.

### v0.3.0 additions — mic capture

| Method | Returns | Notes |
|---|---|---|
| `Audio.Record(durSec, sampleRate)` | `List<int>` | Blocking capture for exactly `durSec` seconds |
| `Audio.RecordStart(maxSec, sampleRate)` | `AmalgameAudioRec*` | Non-blocking; pre-allocates a `maxSec` ceiling |
| `Audio.RecordStop(handle)` | `List<int>` | Drain the captured samples, free the handle |
| `Audio.RecordIsActive(handle)` | `bool` | True while the device is still pulling samples |
| `Audio.RecordSampleCount(handle)` | `int` | Peek at the current write cursor |

```amalgame
// Blocking: record exactly 2 seconds, then echo and save back.
let raw     = Audio.Record(2.0, 16000)
let echoed  = Audio.Echo(raw, 250.0, 0.4, 3, 16000)
Audio.SaveAsWav(echoed, 16000, "echo.wav")

// Non-blocking: start, do other work, stop when ready.
let h = Audio.RecordStart(10.0, 16000)   // ceiling 10s
while (Audio.RecordSampleCount(h) < 16000) {
    // ...do other work for ~1s worth of audio...
}
let buf = Audio.RecordStop(h)            // freezes + frees h
```

Capture format matches the rest of the API — 16-bit signed PCM
mono. Whatever the default input device reports is downmixed by
miniaudio. On a headless machine without any capture device,
`Record` returns an empty list and populates `Audio.LastError()`.

The data callback runs on miniaudio's own OS thread but only does
`memcpy` into a pre-allocated C buffer — no GC interaction, so it
sidesteps the bdwgc thread-registration gotcha that would
otherwise apply.

### v0.4.0 additions — streaming playback (Pause / Resume / Stop)

`Audio.Play(buf, sr)` holds the calling thread until the whole
buffer has been pushed to the device — convenient for short cues,
useless the moment you want to cancel mid-flight or layer other
work on top. v0.4 adds a handle-based streaming surface symmetric
to v0.3's capture trio.

| Method | Returns | Notes |
|---|---|---|
| `Audio.PlayStart(samples, sampleRate)` | `AmalgameAudioPlay*` | Non-blocking; dups the buffer so the user can drop the list |
| `Audio.PlayPause(handle)` | `bool` | Soft pause — callback writes silence, cursor freezes |
| `Audio.PlayResume(handle)` | `bool` | Resume from the frozen cursor |
| `Audio.PlayStop(handle)` | `bool` | Uninit device, free handle |
| `Audio.PlayIsActive(handle)` | `bool` | False once the buffer drains naturally OR after `PlayStop` |
| `Audio.PlayIsPaused(handle)` | `bool` | |
| `Audio.PlaySampleCount(handle)` | `int` | Current read cursor — useful for progress bars |

```amalgame
// Play a tone, pause it after a beat, resume, stop early.
let tone = Audio.ApplyEnvelope(Audio.GenSine(440.0, 2.0, 44100), 20.0, 20.0, 44100)
let h    = Audio.PlayStart(tone, 44100)

// ...do other work for ~500 ms...

Audio.PlayPause(h)        // device keeps running, silence to speakers
// ...wait, then resume mid-tone:
Audio.PlayResume(h)
// ...decide to cancel:
Audio.PlayStop(h)         // uninit + free, h is dead after this
```

Pause is **soft**: the audio thread writes silence into the
output buffer while the `paused` flag is set, rather than
calling `ma_device_stop`. That avoids the audible pop you would
otherwise get on stop/restart, and keeps resume cheap (one int
toggle). The cost is a tiny amount of work still being done in
the audio thread — fine for any realistic use case.

`PlayStart` returns `null` if no default playback device can be
opened (typical headless CI). On a developer box every member of
the v0.4 surface returns `true`/non-null and the state-machine
tests pass against the real output device.

### v0.5.0 additions — live AudioMixer (multi-source streaming)

`Audio.Mix(a, b, offset)` (v0.1) composes two buffers offline
into a new buffer; `Audio.PlayStart(buf, sr)` (v0.4) streams one
buffer to the device. Neither lets you start a sound, then start
another *on top of it*, then stop the first while the second
keeps playing. That's what a live mixer is for, and it's the
last item from the original v1 audio surface.

| Method | Returns | Notes |
|---|---|---|
| `Audio.MixerStart(sampleRate)` | `AmalgameAudioMixer*` | Open default-output device + empty mixer |
| `Audio.MixerAddSource(mixer, samples, gain, loop)` | `int` voice id | Dups buffer; returns 0 on failure (mixer full / OOM) |
| `Audio.MixerSetGain(mixer, voiceId, gain)` | `bool` | Per-voice volume; 0..1 attenuates, >1 boosts (clips) |
| `Audio.MixerSetPaused(mixer, voiceId, paused)` | `bool` | Voice plays silence; cursor frozen |
| `Audio.MixerRemoveSource(mixer, voiceId)` | `bool` | Frees the voice's buffer immediately |
| `Audio.MixerSourceCount(mixer)` | `int` | Alive voices (drained ones auto-prune) |
| `Audio.MixerStop(mixer)` | `bool` | Uninit device, free every voice, free mixer |

```amalgame
// Layered background music + sound effects.
let bg  = Audio.LoadOgg("ambient.ogg")
let sfx = Audio.GenSine(880.0, 0.2, 44100)
let m   = Audio.MixerStart(44100)

let bgId = Audio.MixerAddSource(m, bg, 0.6, true)   // loop the bg
// ...later, fire a quick effect on top:
let fxId = Audio.MixerAddSource(m, sfx, 1.0, false) // one-shot
// ...turn down the music for a moment:
Audio.MixerSetGain(m, bgId, 0.2)
// ...restore:
Audio.MixerSetGain(m, bgId, 0.6)
// ...done:
Audio.MixerStop(m)
```

**Capacity** is hard-capped at 32 voices. Plenty for game audio /
interactive demos; orchestral mockups that need more are a v0.6+
ask. A dynamic linked list with malloc-in-callback would be
worse than a 32-slot scan + `in_use` check.

**Concurrency**: a `ma_mutex` protects the voices array. The audio
thread holds it for one device-tick worth of mixing (~256-1024
samples), the main thread holds it briefly to mutate. Contention
is negligible.

**Voice IDs are monotonic** — never reused after a voice drains
or is removed. A stale id always cleanly returns "not found"
instead of accidentally hitting a recycled slot.

**Auto-prune**: a non-looping voice whose cursor reaches `n` is
removed from the active set inside the audio callback. The slot
becomes available for future `AddSource` calls.

### v0.6.0 additions — MIDI (SMF file IO + render to audio)

Load and save Standard MIDI Files (.mid), and render them
straight to a `List<int>` audio buffer using an internal sine
synth. Pure C, no external dep, fully cross-platform — every
piece works the same on Linux, macOS and Windows.

| Method | Returns | Notes |
|---|---|---|
| `Audio.MidiLoadSmf(path)` | `List<int>` | Flat list of packed events (groups of 4) |
| `Audio.MidiSaveSmf(path, events, ticksPerQuarter, tempoUsPerQuarter)` | `bool` | Single-track format-0 SMF |
| `Audio.MidiRenderToAudio(events, ticksPerQuarter, tempoUsPerQuarter, sampleRate)` | `List<int>` | 16-bit mono int16, sine-synth per active note |
| `Audio.MidiLastTicksPerQuarter()` | `int` | Sonde — division of the last loaded file |
| `Audio.MidiLastTempo()` | `int` | µs per quarter-note from the first tempo meta in the last loaded file |

**Event packing**: events come back as a flat `List<int>`
walked in **groups of 4** — `(delta_ticks, status, data1, data2)`.
The packed layout avoids a `List<List<int>>` (one allocation
per event) which would be unbearable for a typical multi-
thousand-event SMF.

```amalgame
// Load a .mid, render to audio, save as WAV.
let events = Audio.MidiLoadSmf("song.mid")
let tpq    = Audio.MidiLastTicksPerQuarter()
let tempo  = Audio.MidiLastTempo()                      // µs/quarter
let audio  = Audio.MidiRenderToAudio(events, tpq, tempo, 44100)
Audio.SaveAsWav(audio, 44100, "song.wav")               // or Audio.Play(audio, 44100)
```

```amalgame
// Build a C-major arpeggio from scratch and save.
let evs = new List<int>()
// (delta_ticks, status, data1, data2) — status 144 = NoteOn ch0, 128 = NoteOff ch0
evs.Add(0);   evs.Add(144); evs.Add(60); evs.Add(100)   // NoteOn  C4
evs.Add(480); evs.Add(128); evs.Add(60); evs.Add(0)     // NoteOff C4 (1 quarter later)
evs.Add(0);   evs.Add(144); evs.Add(64); evs.Add(100)   // NoteOn  E4
evs.Add(480); evs.Add(128); evs.Add(64); evs.Add(0)
evs.Add(0);   evs.Add(144); evs.Add(67); evs.Add(100)   // NoteOn  G4
evs.Add(480); evs.Add(128); evs.Add(67); evs.Add(0)

Audio.MidiSaveSmf("/tmp/arpeggio.mid", evs, 480, 500000)  // 120 BPM
```

**Supported event types** (read + write):

- NoteOff (`0x80`), NoteOn (`0x90`)
- PolyAftertouch (`0xA0`), CC (`0xB0`)
- ProgramChange (`0xC0`), ChanPressure (`0xD0`)
- PitchBend (`0xE0`)
- Tempo meta-event (`0xFF 0x51`)

**Skipped on read** (parsed past but not emitted to the user):
text / copyright / time-sig / key-sig / SMPTE-offset meta-events,
sysex (`0xF0` / `0xF7`). End-of-track (`0x2F`) marks track
boundaries internally.

**Multi-track files** (SMF format 1) are merged at load time —
each track's events are resolved into absolute ticks, sorted
across tracks, then re-emitted as deltas. The caller sees one
linear stream as if the file were format 0.

**RenderToAudio** uses simple additive sine synthesis: each
NoteOn starts a sine wave at the note's frequency scaled by
`velocity / 127`; NoteOff (or NoteOn with velocity 0) stops it.
Concurrent notes mix additively with int16 clip. Tempo
meta-events update the wallclock rate mid-stream. Pitch bend
and CC are not currently rendered (v0.7+ ask).

**MIDI device IO** (live keyboard input, hardware-synth
control) is **not** in v0.6 — it needs a portable cross-
platform wrapper across ALSA seq on Linux, CoreMIDI on macOS
and winmm on Windows, which is a meaty separate piece. Tracked
for v0.7+. SMF file IO covers the most common MIDI workflow
(load song → render to audio → save as WAV) without any
device dependency.

### Sample format

All v1 surface operates on **16-bit signed PCM mono samples**, with
each sample stored in AM as `int` (i64). Signed range `[-32768, 32767]`,
zero = silence. Sample rate is passed as a separate argument
(typical: 44100, 48000, 8000) and is not stored inside the buffer.

Multi-channel + 32-bit float pipelines land in v0.7+.

### Saving to a WAV file

`Audio.SaveAsWav` produces a Microsoft PCM 16-bit mono WAV. The
file can be played by any standard audio player (VLC, `aplay`,
QuickTime, Windows Media Player) or loaded by any DAW (Audacity,
Reaper, …) for inspection. WAV round-trip is lossless.

## Submarine ping demo

The `samples/submarine_ping.am` sample synthesises the classic
sonar-style ping (1 kHz pure tone, 400 ms duration, 10 ms attack
and release envelope) and overlays 4 decayed echoes spaced every
700 ms — a credible "submarine sends a ping and listens to the
returns" sound bath. Total duration ~3.2 s, saved as
`/tmp/submarine_ping.wav` and optionally played back if your
machine has audio output.

```bash
cd samples
amc -o submarine_ping submarine_ping.am
./submarine_ping
aplay /tmp/submarine_ping.wav    # or open in any audio player
```

## Deferred to v0.7+

- MIDI device IO (live keyboards, hardware-synth control) —
  needs a portable cross-platform wrapper (ALSA seq on Linux,
  CoreMIDI on macOS, winmm on Windows). v0.6 ships SMF file IO
  + render-to-audio, which covers the most common workflow.
- MIDI render: pitch-bend and CC support (v0.6 renders NoteOn /
  NoteOff only)
- Real-time synth via user-provided callback (callbacks interact
  subtly with bdwgc thread pinning)
- Pitch shift / time stretch / FFT analysis
- Spatial audio / HRTF / panning — the v0.5 mixer is mono summing only
- Stereo and float32 sample formats

## Tests

```bash
./tests/run_tests.sh /path/to/amc
```

Tests are deterministic — synthesis is fully reproducible and WAV
round-trip is lossless. The blocking `Audio.Play` is intentionally
NOT in the test suite (a 100 ms cue would hang an unattended
runner). The v0.3 capture, v0.4 streaming-playback and v0.5
mixer tests SKIP cleanly when no default input / output device
is available (typical CI box) and otherwise exercise the real
hardware path: `Record` / `RecordStart` / `RecordStop` for
capture; the full `PlayStart` → `PlayPause` → `PlayResume` →
`PlayStop` state machine for streaming; and the `MixerStart` →
`MixerAddSource × 2` → `SetGain` / `SetPaused` → `RemoveSource`
→ `MixerStop` flow against two real sine tones for the live
mixer (audible-free against a developer machine — the tones
play during the run but the test is state-machine, not audio).
v0.6 MIDI tests run on every machine (pure file IO + pure synth,
no device access) and verify a full round-trip: build a C-major
arpeggio in-memory → `MidiSaveSmf` → `MidiLoadSmf` → assert the
parsed event count + tempo + first-event-is-tempo-meta →
`MidiRenderToAudio` → assert audio length + non-silent samples
→ `SaveAsWav` → `DurationMsOf` ~= the rendered length.

## Licence

Apache-2.0 — see [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md).
The vendored miniaudio header is dual-licensed public-domain / MIT-0
— see the file header and `NOTICE.md`.
