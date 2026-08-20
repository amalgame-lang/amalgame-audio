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

### RNNoise — Xiph.Org / Mozilla / Jean-Marc Valin et al.

[RNNoise v0.1.1](https://github.com/xiph/rnnoise) (tag `v0.1.1`,
commit `6cbfd53`) is vendored at `runtime/vendor/rnnoise/`, used by
`Audio.DenoiseVoice` (v0.8). BSD-3-Clause — see
`runtime/vendor/rnnoise/COPYING-rnnoise` for the full text.
Real-time recurrent-neural-network background-noise suppression;
default model baked into `rnn_data.c` (425 KB), no external
download needed to build.

Pinned to v0.1.1 rather than the current upstream (v0.2/main)
deliberately: the current version's default model is a much larger
(~22 MB) file fetched by upstream's own install script rather than
checked into the repository, plus an added RTCD/SIMD dispatch layer
— both add build-time complexity and an external-download
dependency this package's philosophy (self-contained, no
surprises at `amc package add` time) avoids. Same public API
(`rnnoise_create` / `rnnoise_process_frame` / `rnnoise_destroy`)
either version.

### libopus — Xiph.Org / Skype / Octasic / Mozilla / Amazon et al.

[libopus v1.5.2](https://github.com/xiph/opus) is vendored at
`runtime/vendor/opus/`, used by `Audio.SaveAsOpusStereo`/
`SaveAsOpusMono` (v0.8). BSD-3-Clause — see
`runtime/vendor/opus/COPYING-opus` for the full text. The full
silk/celt/src source tree from libopus's own portable
`Makefile.unix` build (float, non-fixed-point, no DNN/LPCNet
redundancy features) — verified by actually building `libopus.a`
with that Makefile before vendoring, not hand-curated from reading
the `.mk` file lists. Some headers are physically duplicated across
directories beyond what upstream ships — see the long comment in
`amalgame.toml` `[stdlib]` for why (amc's own package precompile
step can't be given extra include paths beyond this package's
`cflags`, which can't contain a reliable relative path).

Chosen over linking the system `libopus`/`apt install libopus-dev`
deliberately, to keep this package's zero-system-dependency
philosophy (same reasoning as vendoring miniaudio/RNNoise rather
than requiring OS packages) — discussed and decided explicitly
before vendoring, given libopus's build is materially more involved
than RNNoise's.

### libogg — Xiph.Org Foundation

[libogg](https://github.com/xiph/ogg) (commit `06a5e02`) is
vendored at `runtime/vendor/ogg/` (public headers) and
`runtime/vendor/libogg-src/` (implementation), used by
`Audio.SaveAsOpusStereo`/`SaveAsOpusMono` to mux raw Opus packets
into a standard, browser-playable Ogg container (RFC 7845 requires
this — libopus itself only produces the codec packets). BSD-3-Clause
— see `runtime/vendor/libogg-src/COPYING-ogg`. Tiny (2 `.c` files,
~120 KB total) compared to libopus — the muxing logic only, no
demuxing/multi-stream support needed for this package's write-only
use case. `ogg/config_types.h` is hand-written here rather than
autoconf-generated (fixed LP64 `<stdint.h>` typedefs) since this
package has no `./configure` step.

**Modified from upstream**: libogg's own sources reference each
other via angle-bracket `#include <ogg/ogg.h>` (a handful of spots
across `bitwise.c`/`framing.c`/`ogg.h`/`os_types.h`/`crctable.h`) —
angle includes never fall back to searching the including file's
own directory the way quote includes do, so they need a reliable
`-I` pointing at the parent of `ogg/`, which (same constraint as
libopus, see `[stdlib]` in `amalgame.toml`) `amc package add`'s own
precompile step can't be given via a relative path. Switched to
quote-form (`#include "ogg/ogg.h"`, or a bare `"os_types.h"` for
files that already live inside the duplicated `ogg/` directory
itself) so the existing header duplication + same-directory search
resolves everything with no extra `-I`. Content is otherwise
untouched. If libogg is ever re-vendored from a newer upstream,
redo this same substitution.

## Trademarks

None claimed.
