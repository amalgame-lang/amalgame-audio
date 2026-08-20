/* Single translation unit for the vendored RNNoise v0.1.1 sources
 * (github.com/xiph/rnnoise, tag v0.1.1, commit 6cbfd53). Unity
 * build — one .o out, mirroring ma_impl.c's role for miniaudio —
 * so amc's per-source precompile step (one .o per `sources` entry)
 * produces exactly one archive member per vendored library rather
 * than scattering RNNoise across several loose .o files.
 *
 * Chosen over the current upstream (v0.2/main) specifically for
 * this: v0.1.1's default model is baked into rnn_data.c (425 KB,
 * checked in below) with no external download at build time —
 * v0.2 moved to a much larger (~22 MB) model fetched by the
 * upstream project's own install script, and added an RTCD/SIMD
 * dispatch layer neither of which this package wants. Same public
 * API (rnnoise_create / rnnoise_process_frame / rnnoise_destroy),
 * same BSD-3-Clause license either way.
 */
#include "rnnoise/kiss_fft.c"
#include "rnnoise/pitch.c"
#include "rnnoise/celt_lpc.c"
#include "rnnoise/rnn.c"
#include "rnnoise/rnn_data.c"
#include "rnnoise/denoise.c"
