# NOTICE — amalgame-audio

## Authorship

Copyright 2026 Bastien Mouget. Original work — see
`runtime/Amalgame_Audio.h`.

Part of the Amalgame ecosystem
([github.com/amalgame-lang/Amalgame](https://github.com/amalgame-lang/Amalgame)).
External contributions are paused at the ecosystem level; see the
main repo's `CONTRIBUTING.md` for the policy.

AI tools (Anthropic Claude) were used during development. Per
the project's authorship policy, AI is treated as a tool, not a
co-author at law.

## Licence

Apache License 2.0. See `LICENSE` for the full text.

## Third-party content

### miniaudio — David Reid

[miniaudio v0.11.25](https://github.com/mackron/miniaudio) is
vendored at `runtime/vendor/miniaudio.h`. Dual-licensed
public-domain (alternative MIT-0 — the choice is the user's).

> "miniaudio is licensed in your choice of: Public Domain […]
> ALTERNATIVE B - MIT-0 No Attribution License" — `miniaudio.h`

Single-file C library that abstracts the OS-native audio APIs:
WASAPI / DirectSound / WinMM on Windows, Core Audio on macOS / iOS,
ALSA / PulseAudio / JACK on Linux, sndio / audio(4) / OSS on BSD,
AAudio / OpenSL ES on Android, Web Audio on the Web. ~4 MB / ~100 000
LoC of portable C. Upstream:
[github.com/mackron/miniaudio](https://github.com/mackron/miniaudio).

### dr_libs / stb_vorbis (bundled with miniaudio, v0.2+)

Starting with amalgame-audio v0.2, miniaudio's bundled lossy
decoders are enabled — `runtime/vendor/ma_impl.c` no longer
defines `MA_NO_FLAC` / `MA_NO_MP3` / `MA_NO_VORBIS`. The decoders
themselves are vendored inside `miniaudio.h`:

- **dr_mp3** by David Reid — public-domain (Unlicense) / MIT-0
  MPEG-1 Audio Layer III decoder.
- **dr_flac** by David Reid — public-domain (Unlicense) / MIT-0
  FLAC decoder.
- **stb_vorbis** by Sean Barrett — public-domain (Unlicense) /
  MIT-0 Ogg Vorbis decoder.

All three are dual-licensed identically to miniaudio (public
domain / MIT-0). The `.o` produced from `ma_impl.c` grows from
~250 KB (v0.1 WAV-only) to ~1 MB (v0.2 with all codecs) as a
result. No external link-time dep is added — these decoders are
header-only and travel inside miniaudio.h.

**MP3 patent note**: the MP3 format itself is patent-free
worldwide (the final US patents expired 2017-12-30). FLAC and
Vorbis have always been royalty-free.

## Trademarks

None claimed.
