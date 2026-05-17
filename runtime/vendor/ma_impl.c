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
 * v0.2 — MP3 / FLAC / Vorbis decoders are ENABLED. miniaudio ships
 * the dr_libs implementations (dr_mp3, dr_flac, stb_vorbis) — all
 * public-domain / Unlicense / MIT. See NOTICE.md. The .o grows
 * (~250 KB v0.1 WAV-only → ~700 KB v0.2 with all codecs) —
 * acceptable tradeoff for native multi-format playback without a
 * sibling package per codec.
 *
 * MP3 patent note: the MP3 format itself is now patent-free
 * worldwide (final US patents expired 2017-12-30). FLAC and Vorbis
 * have always been royalty-free. No legal action item.
 */

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
