/* Single translation unit for the vendored libogg sources
 * (github.com/xiph/ogg, commit 06a5e02) — the Ogg container muxer,
 * used to wrap raw Opus packets into a standard, browser-playable
 * `.opus` file (RFC 7845 requires Opus audio to live inside an Ogg
 * container; libopus itself only produces the raw codec packets,
 * it does no container work). BSD-3-Clause, same as libopus.
 *
 * `ogg/config_types.h` is hand-written here (see that file) rather
 * than autoconf-generated — this package has no ./configure step.
 */
#include "libogg-src/bitwise.c"
#include "libogg-src/framing.c"
