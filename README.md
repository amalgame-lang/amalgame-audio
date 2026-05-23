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
amc package add github.com/amalgame-lang/amalgame-audio@v0.3.0
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
v0.4+. The `.o` produced from `ma_impl.c` grows from ~250 KB
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

### Sample format

All v1 surface operates on **16-bit signed PCM mono samples**, with
each sample stored in AM as `int` (i64). Signed range `[-32768, 32767]`,
zero = silence. Sample rate is passed as a separate argument
(typical: 44100, 48000, 8000) and is not stored inside the buffer.

Multi-channel + 32-bit float pipelines land in v0.4+.

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

## Deferred to v0.4+

- Real-time synth via user-provided callback (callbacks interact
  subtly with bdwgc thread pinning)
- Pitch shift / time stretch / FFT analysis
- Mixing graph / spatial audio / HRTF
- Stereo and float32 sample formats

## Tests

```bash
./tests/run_tests.sh /path/to/amc
```

Tests are deterministic — synthesis is fully reproducible, WAV
round-trip is lossless, `Audio.Play` is intentionally NOT in the
test suite (silent CI machines have no usable output device).
The v0.3 capture tests SKIP cleanly when no default input device
is available (typical CI box) and otherwise exercise the real
hardware path through `Record` / `RecordStart` / `RecordStop`.

## Licence

Apache-2.0 — see [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md).
The vendored miniaudio header is dual-licensed public-domain / MIT-0
— see the file header and `NOTICE.md`.
