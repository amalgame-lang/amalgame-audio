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

The lossy decoders (MP3 / FLAC / Vorbis) are disabled at compile
time in `runtime/vendor/ma_impl.c` to shrink the .o and avoid the
patent / licence considerations of those formats. WAV stays enabled
both directions for round-tripping. Users who need MP3/FLAC/OGG
decode can fork the package and toggle the `MA_NO_*` defines.

## Trademarks

None claimed.
