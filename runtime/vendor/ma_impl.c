/*
 * ma_impl.c — single translation unit that materialises miniaudio.
 * miniaudio is header-only by default; defining MINIAUDIO_IMPLEMENTATION
 * once across the whole program is what produces the function bodies.
 *
 * Compiled once per platform under [stdlib].precompile=true and
 * cached at ~/.amalgame/packages/<...>/build/<platform>/. Final
 * link pulls the resulting .o into the user binary.
 *
 * The upstream library is dual-licensed public-domain or MIT-0 —
 * see NOTICE.md for the full attribution.
 *
 * We disable the lossy decoders (MP3 / FLAC / Vorbis) — both to
 * shrink the .o and to skip their patent / licence considerations.
 * WAV stays enabled both directions so Save → Load round-trips work
 * without any extra package. Users who need MP3 / FLAC / OGG decode
 * can extend the package later by toggling the defines below.
 */

#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_NO_VORBIS

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
