# amalgame-audio

Audio synthesis, playback and WAV IO for [Amalgame](https://github.com/amalgame-lang/Amalgame).
Wraps David Reid's public-domain [miniaudio](https://github.com/mackron/miniaudio)
single-header library (vendored at `runtime/vendor/`). miniaudio
abstracts the OS-native audio APIs (WASAPI on Windows, Core Audio
on macOS, ALSA/PulseAudio on Linux) behind a single API — we wrap
that.

## Install

```bash
amc package add audio                                  # via index
amc package add github.com/amalgame-lang/amalgame-audio@v0.1.0
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

### Sample format

All v1 surface operates on **16-bit signed PCM mono samples**, with
each sample stored in AM as `int` (i64). Signed range `[-32768, 32767]`,
zero = silence. Sample rate is passed as a separate argument
(typical: 44100, 48000, 8000) and is not stored inside the buffer.

Multi-channel + 32-bit float pipelines land in v2.

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

## Deferred to v2

- Capture (mic input) — needs the dual-callback shape
- Real-time synth via user-provided callback (callbacks interact
  subtly with bdwgc thread pinning)
- MP3 / FLAC / Vorbis decode (toggle `MA_NO_*` in `ma_impl.c`)
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

## Licence

Apache-2.0 — see [`LICENSE`](LICENSE) and [`NOTICE.md`](NOTICE.md).
The vendored miniaudio header is dual-licensed public-domain / MIT-0
— see the file header and `NOTICE.md`.
