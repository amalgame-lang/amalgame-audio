/* Hand-written replacement for autoconf-generated config_types.h —
 * this package builds via plain gcc, no ./configure step. Fixed
 * typedefs for a standard LP64 Unix target (Linux/macOS x86_64 /
 * ARM64 — what amc/gcc actually targets here), matching what
 * configure would have produced on any such platform. */
#ifndef __CONFIG_TYPES_H__
#define __CONFIG_TYPES_H__

#include <stdint.h>

typedef int16_t   ogg_int16_t;
typedef uint16_t  ogg_uint16_t;
typedef int32_t   ogg_int32_t;
typedef uint32_t  ogg_uint32_t;
typedef int64_t   ogg_int64_t;
typedef uint64_t  ogg_uint64_t;

#endif
