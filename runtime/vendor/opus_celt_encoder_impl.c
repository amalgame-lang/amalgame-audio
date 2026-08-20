/* celt/celt_encoder.c compiled in its OWN translation unit — see
 * the header comment in opus_impl.c for why (a real declaration
 * mismatch when unity-built alongside the rest of libopus, tied to
 * celt.h's OPUS_CUSTOM_NOSTATIC macro seeing different context
 * depending on inclusion order). Needs -DOPUS_BUILD -DUSE_ALLOCA,
 * same as opus_impl.c (this package's amalgame.toml `cflags`). */
#include "opus/celt/celt_encoder.c"
