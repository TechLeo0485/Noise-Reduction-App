/* Compatibility header for building RNNoise 0.2 with MSVC.
 *
 * vec.h includes "os_support.h" on its generic (non-SSE/AVX/NEON) path. GCC/Clang on x86
 * define __SSE2__ and never take that path, but MSVC does not define __SSE2__, and the 0.2
 * release archive does not ship os_support.h. These are the Opus definitions it expects. */
#ifndef VOICESTUDIO_RNNOISE_OS_SUPPORT_H
#define VOICESTUDIO_RNNOISE_OS_SUPPORT_H

#include <string.h>

#ifndef OPUS_COPY
#define OPUS_COPY(dst, src, n) (memcpy((dst), (src), (n) * sizeof(*(dst)) + 0 * ((dst) - (src))))
#endif
#ifndef OPUS_MOVE
#define OPUS_MOVE(dst, src, n) (memmove((dst), (src), (n) * sizeof(*(dst)) + 0 * ((dst) - (src))))
#endif
#ifndef OPUS_CLEAR
#define OPUS_CLEAR(dst, n) (memset((dst), 0, (n) * sizeof(*(dst))))
#endif

#endif
