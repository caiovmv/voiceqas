/*
 * SpanDSP - subset vendored for G.722 (see LICENSE.spandsp.txt)
 * Portable fallbacks for vector_int.h (no MMX/SSE).
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "spandsp/telephony.h"
#include "spandsp/vector_int.h"

SPAN_DECLARE(int32_t) vec_dot_prodi16(const int16_t x[], const int16_t y[], int n)
{
    int32_t z = 0;
    for (int i = 0; i < n; i++) {
        z += (int32_t)x[i] * (int32_t)y[i];
    }
    return z;
}

SPAN_DECLARE(int32_t) vec_circular_dot_prodi16(const int16_t x[], const int16_t y[], int n, int pos)
{
    int32_t z = vec_dot_prodi16(&x[pos], &y[0], n - pos);
    z += vec_dot_prodi16(&x[0], &y[n - pos], pos);
    return z;
}
