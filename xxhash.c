/*
*  xxHash - Fast Hash algorithm
*  Copyright (C) 2012-2016, Yann Collet
*
*  BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions are
*  met:
*
*  * Redistributions of source code must retain the above copyright
*  notice, this list of conditions and the following disclaimer.
*  * Redistributions in binary form must reproduce the above
*  copyright notice, this list of conditions and the following disclaimer
*  in the documentation and/or other materials provided with the
*  distribution.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
*  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
*  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
*  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
*  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
*  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
*  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
*  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
*  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
*  You can contact the author at :
*  - xxHash homepage: http://www.xxhash.com
*  - xxHash source repository : https://github.com/Cyan4973/xxHash
*/


/* *************************************
*  Tuning parameters
***************************************/
/*!XXH_FORCE_MEMORY_ACCESS :
 * By default, access to unaligned memory is controlled by `memcpy()`, which is safe and portable.
 * Unfortunately, on some target/compiler combinations, the generated assembly is sub-optimal.
 * The below switch allow to select different access method for improved performance.
 * Method 0 (default) : use `memcpy()`. Safe and portable.
 * Method 1 : `__packed` statement. It depends on compiler extension (ie, not portable).
 *            This method is safe if your compiler supports it, and *generally* as fast or faster than `memcpy`.
 * Method 2 : direct access. This method doesn't depend on compiler but violate C standard.
 *            It can generate buggy code on targets which do not support unaligned memory accesses.
 *            But in some circumstances, it's the only known way to get the most performance (ie GCC + ARMv6)
 * See http://stackoverflow.com/a/32095106/646947 for details.
 * Prefer these methods in priority order (0 > 1 > 2)
 */
#ifndef XXH_FORCE_MEMORY_ACCESS   /* can be defined externally, on command line for example */
#  if defined(__GNUC__) && ( defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) \
                        || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) \
                        || defined(__ARM_ARCH_6ZK__) || defined(__ARM_ARCH_6T2__) )
#    define XXH_FORCE_MEMORY_ACCESS 2
#  elif (defined(__INTEL_COMPILER) && !defined(_WIN32)) || \
  (defined(__GNUC__) && ( defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) \
                    || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) \
                    || defined(__ARM_ARCH_7S__) || defined(__ARM_ARCH_V7VE__) \
                    || defined(__aarch64__)))
#    define XXH_FORCE_MEMORY_ACCESS 1
#  endif
#endif

/*!XXH_ACCEPT_NULL_INPUT_POINTER :
 * If input pointer is NULL, xxHash default behavior is to dereference it, triggering a segfault.
 * When this macro is enabled, xxHash actively checks input for null pointer.
 * It it is, result for null input pointers is the same as a null-length input.
 */
#ifndef XXH_ACCEPT_NULL_INPUT_POINTER   /* can be defined externally */
#  define XXH_ACCEPT_NULL_INPUT_POINTER 0
#endif

/*!XXH_FORCE_NATIVE_FORMAT :
 * By default, xxHash library provides endian-independent Hash values, based on little-endian convention.
 * Results are therefore identical for little-endian and big-endian CPU.
 * This comes at a performance cost for big-endian CPU, since some swapping is required to emulate little-endian format.
 * Should endian-independence be of no importance for your application, you may set the #define below to 1,
 * to improve speed for Big-endian CPU.
 * This option has no impact on Little_Endian CPU.
 */
#ifndef XXH_FORCE_NATIVE_FORMAT   /* can be defined externally */
#  define XXH_FORCE_NATIVE_FORMAT 0
#endif

/*!XXH_FORCE_ALIGN_CHECK :
 * This is a minor performance trick, only useful with lots of very small keys
 * or on platforms with an unaligned access penalty.
 *
 * It means : check for aligned/unaligned input.
 * The check costs one initial branch per hash;
 * set it to 0 when the input is guaranteed to be aligned,
 * or when alignment doesn't matter for performance.
 *
 * For example, Sandy Bridge (AVX) and ARMv7 have no unaligned
 * access penalty. Disabling this check can increase performance.
 *
 * However, on a Core 2, which has an unaligned access penalty, enabling
 * these checks is the difference between 3.4 GB/s and 4.9 GB/s in XXH32a
 * on GCC 8.1.
 */
#ifndef XXH_FORCE_ALIGN_CHECK /* can be defined externally */
#  if defined(__AVX__) || defined(__ARM_NEON__) || defined(__ARM_NEON)
#    define XXH_FORCE_ALIGN_CHECK 0
#  else
#    define XXH_FORCE_ALIGN_CHECK 1
#  endif
#endif


/* *************************************
*  Includes & Memory related functions
***************************************/
/*! Modify the local functions below should you wish to use some other memory routines
*   for malloc(), free() */
#include <stdlib.h>
static void* XXH_malloc(size_t s) { return malloc(s); }
static void  XXH_free  (void* p)  { free(p); }
/*! and for memcpy() */
#include <string.h>
static void* XXH_memcpy(void* dest, const void* src, size_t size) { return memcpy(dest,src,size); }

#include <assert.h>   /* assert */

#define XXH_STATIC_LINKING_ONLY
#include "xxhash.h"


/* *************************************
*  Compiler Specific Options
***************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  pragma warning(disable : 4127)      /* disable: C4127: conditional expression is constant */
#  define FORCE_INLINE static __forceinline
#else
#  if defined (__cplusplus) || defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   /* C99 */
#    ifdef __GNUC__
#      define FORCE_INLINE static inline __attribute__((__always_inline__, __unused__))
#    else
#      define FORCE_INLINE static inline
#    endif
#  else
#    ifdef __GNUC__
#      define FORCE_INLINE static __inline__
#    else
#      define FORCE_INLINE static
#    endif
#  endif /* __STDC_VERSION__ */
#endif

/* On Thumb 1, GCC decides that shift/add is __always__ faster than
 * multiplication via a constant, even when it generates 8 times as many
 * instructions and twice as many CPU cycles.
 *
 * If we lie to GCC and tell it that the value is not constant, then it
 * will disable this "optimization".
 * Clang, normal ARM, and Thumb-2 are not affected, and Clang even amusingly
 * converts GCC's shift/add mess back into multiplication.
 * https://godbolt.org/z/2vc82a */
#if defined(__thumb__) && !defined(__thumb2__) && !defined(__ARM_ARCH_V6M__) \
     && defined(__GNUC__) && !defined(__clang__)
#define CONSTANT __attribute__((visibility("hidden")))
#else
#define CONSTANT static const
#endif

/* *************************************
*  Basic Types
***************************************/
#ifndef MEM_MODULE
# if !defined (__VMS) \
  && (defined (__cplusplus) \
  || (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */) )
#   include <stdint.h>
    typedef uint8_t  BYTE;
    typedef uint16_t U16;
    typedef uint32_t U32;
# else
    typedef unsigned char      BYTE;
    typedef unsigned short     U16;
    typedef unsigned int       U32;
# endif
#endif

#if (defined(XXH_FORCE_MEMORY_ACCESS) && (XXH_FORCE_MEMORY_ACCESS==2))

/* Force direct memory access. Only works on CPU which support unaligned memory access in hardware */
static U32 XXH_read32(const void* memPtr) { return *(const U32*) memPtr; }

#elif (defined(XXH_FORCE_MEMORY_ACCESS) && (XXH_FORCE_MEMORY_ACCESS==1))

/* __pack instructions are safer, but compiler specific, hence potentially problematic for some compilers */
/* currently only defined for gcc and icc */
typedef union { U32 u32; } __attribute__((packed)) unalign;
static U32 XXH_read32(const void* ptr) { return ((const unalign*)ptr)->u32; }

#else

/* portable and safe solution. Generally efficient.
 * see : http://stackoverflow.com/a/32095106/646947
 */
static U32 XXH_read32(const void* memPtr)
{
    U32 val;
    memcpy(&val, memPtr, sizeof(val));
    return val;
}

#endif   /* XXH_FORCE_DIRECT_MEMORY_ACCESS */

/* ****************************************
*  Compiler-specific Functions and Macros
******************************************/
#define XXH_GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)

/* Clang's __has_builtin because checking clang versions is impossible,
 * thank you Apple. */
#ifndef __has_builtin
#  define __has_builtin(x) 0
#endif

#if XXH_GCC_VERSION >= 407 || __has_builtin(__builtin_assume_aligned)
#  define XXH_assume_aligned(p, align) __builtin_assume_aligned((p), (align))
#else
#  define XXH_assume_aligned(p, align) (p)
#endif

/* Note : although _rotl exists for minGW (GCC under windows), performance seems poor */
#if defined(_MSC_VER)
#  define XXH_rotl32(x,r) _rotl(x,r)
#  define XXH_rotl64(x,r) _rotl64(x,r)
#else
#  define XXH_rotl32(x,r) ((x << r) | (x >> (32 - r)))
#  define XXH_rotl64(x,r) ((x << r) | (x >> (64 - r)))
#endif

#if defined(_MSC_VER)     /* Visual Studio */
#  define XXH_swap32 _byteswap_ulong
#elif XXH_GCC_VERSION >= 403
#  define XXH_swap32 __builtin_bswap32
#else
static U32 XXH_swap32 (U32 x)
{
    return  ((x << 24) & 0xff000000 ) |
            ((x <<  8) & 0x00ff0000 ) |
            ((x >>  8) & 0x0000ff00 ) |
            ((x >> 24) & 0x000000ff );
}
#endif


/* *************************************
*  Architecture Macros
***************************************/
typedef enum { XXH_bigEndian=0, XXH_littleEndian=1 } XXH_endianess;

/* XXH_CPU_LITTLE_ENDIAN can be defined externally, for example on the compiler command line */
#ifndef XXH_CPU_LITTLE_ENDIAN
#  if defined(__LITTLE_ENDIAN__)
#    define XXH_CPU_LITTLE_ENDIAN XXH_littleEndian
#  elif defined(__BIG_ENDIAN__)
#    define XXH_CPU_LITTLE_ENDIAN XXH_bigEndian
#  else
static XXH_endianess XXH_isLittleEndian(void)
{
    const union { U32 u; BYTE c[4]; } one = { 1 };   /* don't use static : performance detrimental  */
    return one.c[0] ? XXH_littleEndian : XXH_bigEndian;
}
#    define XXH_CPU_LITTLE_ENDIAN   XXH_isLittleEndian()
#  endif
#endif


/* ***************************
*  Memory reads
*****************************/
typedef enum { XXH_aligned, XXH_unaligned } XXH_alignment;

FORCE_INLINE U32 XXH_readLE32_align(const void* ptr, XXH_endianess endian, XXH_alignment align)
{
    if (align==XXH_unaligned)
        return endian==XXH_littleEndian ? XXH_read32(ptr) : XXH_swap32(XXH_read32(ptr));
    else
        return endian==XXH_littleEndian ? *(const U32*)ptr : XXH_swap32(*(const U32*)ptr);
}

FORCE_INLINE U32 XXH_readLE32(const void* ptr, XXH_endianess endian)
{
    return XXH_readLE32_align(ptr, endian, XXH_unaligned);
}

static U32 XXH_readBE32(const void* ptr)
{
    return XXH_CPU_LITTLE_ENDIAN ? XXH_swap32(XXH_read32(ptr)) : XXH_read32(ptr);
}



/* *************************************
*  Macros
***************************************/
#define XXH_STATIC_ASSERT(c)  { enum { XXH_sa = 1/(int)(!!(c)) }; }  /* use after variable declarations */
XXH_PUBLIC_API unsigned XXH_versionNumber (void) { return XXH_VERSION_NUMBER; }

/* *******************************************************************
*  32-bit hash functions
*********************************************************************/
CONSTANT U32 PRIME32_1 = 2654435761U;   /* 0b10011110001101110111100110110001 */
CONSTANT U32 PRIME32_2 = 2246822519U;   /* 0b10000101111010111100101001110111 */
CONSTANT U32 PRIME32_3 = 3266489917U;   /* 0b11000010101100101010111000111101 */
CONSTANT U32 PRIME32_4 =  668265263U;   /* 0b00100111110101001110101100101111 */
CONSTANT U32 PRIME32_5 =  374761393U;   /* 0b00010110010101100110011110110001 */

static U32 XXH32_round(U32 seed, U32 input)
{
    seed += input * PRIME32_2;
    seed  = XXH_rotl32(seed, 13);
    seed *= PRIME32_1;

#if !defined(XXH_FORCE_VECTOR) && (defined(__i386__) || defined(__x86_64__)) && defined(__GNUC__)
    /* UGLY HACK:
     * Clang and GCC don't vectorize XXH32 well for SSE4.1, and will actually slow
     * down XXH32 compared to the normal version, which uses instruction level
     * parallelism to make up for the lack of vectorization.
     *
     * Previously, the workaround was to globally disable it with -mno-sse4.
     * However, that makes it so XXH32a cannot vectorize, and that the vectorization
     * was the whole point of XXH32a.
     *
     * To fix this, we use the following:
     *     __asm__ __volatile__("" : "+r" (seed))
     * which forces seed into a normal register, not an SSE one.
     *
     * When we actually intend to use SSE code, we use the rounding code directly.
     *
     * Define XXH_FORCE_VECTOR to disable. */
    __asm__ __volatile__("" : "+r" (seed));
#endif

    return seed;
}

/* mix all bits */
static U32 XXH32_avalanche(U32 h32)
{
    h32 ^= h32 >> 15;
    h32 *= PRIME32_2;
    h32 ^= h32 >> 13;
    h32 *= PRIME32_3;
    h32 ^= h32 >> 16;
    return(h32);
}


#ifndef XXH_VECTORIZE
/* Clang and GCC 4.6+ can use vectorization properly.
 * Most importantly, shifting vectors. */
#  if (defined(__clang__) || XXH_GCC_VERSION >= 406) && \
    (defined(__SSE4_1__) || defined(__ARM_NEON) || defined(__ARM_NEON__))
#    define XXH_VECTORIZE 1
#  else
#    define XXH_VECTORIZE 0
#  endif
#endif

#if XXH_VECTORIZE

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
#include <arm_neon.h>
# define XXH_NEON

typedef uint32x4_t U32x4;
typedef uint32x4x2_t U32x4x2;

/* Neither GCC or Clang can properly optimize the generic version
 * for Arm NEON.
 * Instead of the optimal version, which is this:
 *      vshr.u32        q9, q8, #19
 *      vsli.32         q9, q8, #13
 * GCC and Clang will produce this slower version:
 *      vshr.u32        q9, q8, #19
 *      vshl.i32        q8, q8, #13
 *      vorr            q8, q8, q9
 * This is much faster, and I think a few intrinsics are acceptable. */
#define XXH_vec_rotl32(x, r) vsliq_n_u32(vshrq_n_u32((x), 32 - (r)), (x), (r))
#define XXH_vec_load_unaligned(p) vld1q_u32((const U32*)p)
#define XXH_vec_store_unaligned(p, v) vst1q_u32((U32*)p, v)
#define XXH_vec_load_unaligned(p) vld1q_u32((const U32*)p)
#define XXH_vec_store_unaligned(p, v) vst1q_u32((U32*)p, v)

/* Like XXH_vec_rotl32, but takes a vector as r. No NEON-optimized
 * version for this one. */
FORCE_INLINE U32x4 XXH_rotlvec_vec32(U32x4 x, const U32x4 r)
{
    const U32x4 v32 = { 32, 32, 32, 32 };
    return (x << r) | (x >> (v32 - r));
}


#else /* not NEON */
/* __m128i (SSE) or uint32x4_t (NEON). */
typedef U32 U32x4 __attribute__((__vector_size__(16)));

/* Two U32x4s. */
typedef struct { U32x4 val[2]; } U32x4x2;

/* Clang < 5.0 doesn't support int -> vector conversions.
 * Yuck. */
FORCE_INLINE U32x4 XXH_vec_rotl32(U32x4 x, U32 r)
{
    const U32x4 left = { r, r, r, r };
    const U32x4 right = {
        32 - r,
        32 - r,
        32 - r,
        32 - r
    };
    return (x << left) | (x >> right);
}

/* emmintrin.h's _mm_loadu_si128 code. */
FORCE_INLINE U32x4 XXH_vec_load_unaligned(const void* p)
{
    struct loader {
        U32x4 v;
    } __attribute__((__packed__, __may_alias__));
    return ((const struct loader*)p)->v;
}

/* _mm_storeu_si128 */
FORCE_INLINE void XXH_vec_store_unaligned(void* p, const U32x4 v)
{
    struct loader {
        U32x4 v;
     } __attribute__((__packed__, __may_alias__));
    ((struct loader*)p)->v = v;
}

#define XXH_vec_load_aligned(p) *(U32x4*)(p)
#define XXH_vec_store_aligned(p, v) (*(U32x4*)(p) = v)

#endif /* not NEON */
#endif /* XXH_VECTORIZE */

#define XXH_get32bits(p) XXH_readLE32_align(p, endian, align)

static U32
XXH32_finalize(U32 h32, const void* ptr, size_t len,
                XXH_endianess endian, XXH_alignment align)

{
    const BYTE* p = (const BYTE*)ptr;

#define PROCESS1               \
    h32 += (*p++) * PRIME32_5; \
    h32 = XXH_rotl32(h32, 11) * PRIME32_1 ;

#define PROCESS4                         \
    h32 += XXH_get32bits(p) * PRIME32_3; \
    p+=4;                                \
    h32  = XXH_rotl32(h32, 17) * PRIME32_4 ;

    switch(len&31)  /* or switch(bEnd - p) */
    {
      case 28:      PROCESS4;
                    /* fallthrough */
      case 24:      PROCESS4;
                    /* fallthrough */
      case 20:      PROCESS4;
                    /* fallthrough */
      case 16:      PROCESS4;
                    /* fallthrough */
      case 12:      PROCESS4;
                    /* fallthrough */
      case 8:       PROCESS4;
                    /* fallthrough */
      case 4:       PROCESS4;
                    return XXH32_avalanche(h32);

      case 29:      PROCESS4;
                    /* fallthrough */
      case 25:       PROCESS4;
                    /* fallthrough */
      case 21:      PROCESS4;
                    /* fallthrough */
      case 17:       PROCESS4;
                    /* fallthrough */
      case 13:      PROCESS4;
                    /* fallthrough */
      case 9:       PROCESS4;
                    /* fallthrough */
      case 5:       PROCESS4;
                    PROCESS1;
                    return XXH32_avalanche(h32);


      case 30:      PROCESS4;
                    /* fallthrough */
      case 26:      PROCESS4;
                    /* fallthrough */
      case 22:      PROCESS4;
                    /* fallthrough */
      case 18:      PROCESS4;
                    /* fallthrough */
      case 14:      PROCESS4;
                    /* fallthrough */
      case 10:      PROCESS4;
                    /* fallthrough */
      case 6:       PROCESS4;
                    PROCESS1;
                    PROCESS1;
                    return XXH32_avalanche(h32);

      case 31:      PROCESS4;
                    /* fallthrough */
      case 27:      PROCESS4;
                    /* fallthrough */
      case 23:      PROCESS4;
                    /* fallthrough */
      case 19:      PROCESS4;
                    /* fallthrough */
      case 15:      PROCESS4;
                    /* fallthrough */
      case 11:      PROCESS4;
                    /* fallthrough */
      case 7:       PROCESS4;
                    /* fallthrough */
      case 3:       PROCESS1;
                    /* fallthrough */
      case 2:       PROCESS1;
                    /* fallthrough */
      case 1:       PROCESS1;
                    /* fallthrough */
      case 0:       return XXH32_avalanche(h32);
    }
    assert(0);
    return h32;   /* reaching this point is deemed impossible */
}

FORCE_INLINE U32
XXH32_endian_align(const void* input, size_t len, U32 seed,
                    XXH_endianess endian, XXH_alignment align)
{
    const BYTE* p = (const BYTE*)input;
    const BYTE* bEnd = p + len;
    U32 h32;

#if defined(XXH_ACCEPT_NULL_INPUT_POINTER) && (XXH_ACCEPT_NULL_INPUT_POINTER>=1)
    if (p==NULL) {
        len=0;
        bEnd=p=(const BYTE*)(size_t)16;
    }
#endif

#ifdef XXH_NEON
    if (len>=16 && endian==XXH_littleEndian) {
        /* This is where NEON gets its special treatment, as it performs
         * terribly with the non-vectorized one. */
        XXH_ALIGN_16
        U32 vx1[4] = {
            PRIME32_1 + PRIME32_2,
            PRIME32_2,
            0,
            -PRIME32_1
        };
        U32x4 v = XXH_vec_load_unaligned(vx1);
        const U32x4 prime1 = vdupq_n_u32(PRIME32_1);
        const U32x4 prime2 = vdupq_n_u32(PRIME32_2);

        const BYTE* const limit = bEnd - 15;

        v += vdupq_n_u32(seed);
        do {
            const U32x4 inp = XXH_vec_load_unaligned((const U32 *)p);
            v += inp * prime2;
            v  = XXH_vec_rotl32(v, 13);
            v *= prime1;

            p += 16;
        } while (p < limit);

        {
            const U32x4 r = { 1, 7, 12, 18 };
            v = XXH_rotlvec_vec32(v, r);
        }
        XXH_vec_store_unaligned(vx1, v);

        h32 = vx1[0] + vx1[1] + vx1[2] + vx1[3];
    } else
#endif /* XXH_NEON */
    if (len>=16) {
        U32 v1 = seed + PRIME32_1 + PRIME32_2;
        U32 v2 = seed + PRIME32_2;
        U32 v3 = seed + 0;
        U32 v4 = seed - PRIME32_1;
        const BYTE* limit = bEnd - 15;

        /* Avoid branching when we don't have to. This helps out ARM Thumb a lot. */
        if (XXH_FORCE_ALIGN_CHECK && align==XXH_aligned && endian==XXH_littleEndian) {
            do {
               const U32* palign = (const U32*)XXH_assume_aligned(p, 4);
               v1 = XXH32_round(v1, palign[0]); p+=4;
               v2 = XXH32_round(v2, palign[1]); p+=4;
               v3 = XXH32_round(v3, palign[2]); p+=4;
               v4 = XXH32_round(v4, palign[3]); p+=4;
            } while (p < limit);
        } else {
            do {
                v1 = XXH32_round(v1, XXH_get32bits(p)); p+=4;
                v2 = XXH32_round(v2, XXH_get32bits(p)); p+=4;
                v3 = XXH32_round(v3, XXH_get32bits(p)); p+=4;
                v4 = XXH32_round(v4, XXH_get32bits(p)); p+=4;
            } while (p < limit);
        }
        h32 = XXH_rotl32(v1, 1)  + XXH_rotl32(v2, 7)
            + XXH_rotl32(v3, 12) + XXH_rotl32(v4, 18);
    } else {
        h32  = seed + PRIME32_5;
    }

    h32 += (U32)len;

    return XXH32_finalize(h32, p, len&15, endian, align);
}

XXH_PUBLIC_API unsigned int XXH32 (const void* input, size_t len, unsigned int seed)
{
#if 0
    /* Simple version, good for code maintenance, but unfortunately slow for small inputs */
    XXH32_state_t state;
    XXH32_reset(&state, seed);
    XXH32_update(&state, input, len);
    return XXH32_digest(&state);
#else
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

    if (XXH_FORCE_ALIGN_CHECK && (((size_t)input) & 3) == 0) {   /* Input is 4-bytes aligned, leverage the speed benefit */
        if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
            return XXH32_endian_align(input, len, seed, XXH_littleEndian, XXH_aligned);
        else
            return XXH32_endian_align(input, len, seed, XXH_bigEndian, XXH_aligned);
    }

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH32_endian_align(input, len, seed, XXH_littleEndian, XXH_unaligned);
    else
        return XXH32_endian_align(input, len, seed, XXH_bigEndian, XXH_unaligned);
#endif
}


/*======   Hash streaming   ======*/

XXH_PUBLIC_API XXH32_state_t* XXH32_createState(void)
{
    return (XXH32_state_t*)XXH_malloc(sizeof(XXH32_state_t));
}
XXH_PUBLIC_API XXH_errorcode XXH32_freeState(XXH32_state_t* statePtr)
{
    XXH_free(statePtr);
    return XXH_OK;
}

XXH_PUBLIC_API void XXH32_copyState(XXH32_state_t* dstState, const XXH32_state_t* srcState)
{
    memcpy(dstState, srcState, sizeof(*dstState));
}

XXH_PUBLIC_API XXH_errorcode XXH32_reset(XXH32_state_t* statePtr, unsigned int seed)
{
    XXH32_state_t state;   /* using a local state to memcpy() in order to avoid strict-aliasing warnings */
    memset(&state, 0, sizeof(state));
    state.v1 = seed + PRIME32_1 + PRIME32_2;
    state.v2 = seed + PRIME32_2;
    state.v3 = seed + 0;
    state.v4 = seed - PRIME32_1;
    /* do not write into reserved, planned to be removed in a future version */
    memcpy(statePtr, &state, sizeof(state) - sizeof(state.reserved));
    return XXH_OK;
}


FORCE_INLINE XXH_errorcode
XXH32_update_endian(XXH32_state_t* state, const void* input, size_t len, XXH_endianess endian)
{
    if (input==NULL)
#if defined(XXH_ACCEPT_NULL_INPUT_POINTER) && (XXH_ACCEPT_NULL_INPUT_POINTER>=1)
        return XXH_OK;
#else
        return XXH_ERROR;
#endif

    {   const BYTE* p = (const BYTE*)input;
        const BYTE* const bEnd = p + len;

        state->total_len_32 += (unsigned)len;
        state->large_len |= (len>=16) | (state->total_len_32>=16);

        if (state->memsize + len < 16)  {   /* fill in tmp buffer */
            XXH_memcpy((BYTE*)(state->mem32) + state->memsize, input, len);
            state->memsize += (unsigned)len;
            return XXH_OK;
        }

        if (state->memsize) {   /* some data left from previous update */
            XXH_memcpy((BYTE*)(state->mem32) + state->memsize, input, 16-state->memsize);
            {   const U32* p32 = state->mem32;
                state->v1 = XXH32_round(state->v1, XXH_readLE32(p32, endian)); p32++;
                state->v2 = XXH32_round(state->v2, XXH_readLE32(p32, endian)); p32++;
                state->v3 = XXH32_round(state->v3, XXH_readLE32(p32, endian)); p32++;
                state->v4 = XXH32_round(state->v4, XXH_readLE32(p32, endian));
            }
            p += 16-state->memsize;
        }
        state->memsize = 0;

        if (p <= bEnd-16) {
            XXH_ALIGN_16
            U32 v1 = state->v1;
            U32 v2 = state->v2;
            U32 v3 = state->v3;
            U32 v4 = state->v4;
            const BYTE* const limit = bEnd - 16;

            /* Aligned pointers and fewer branches are very helpful and worth the
             * duplication on ARM. */
            if (XXH_FORCE_ALIGN_CHECK && ((size_t)p&3)==0 && endian==XXH_littleEndian) {
                do {
                    const U32* p_align = (const U32*)XXH_assume_aligned(p, 4);
                    /* NO SSE */
                    v1 = XXH32_round(v1, p_align[0]);
                    v2 = XXH32_round(v2, p_align[1]);
                    v3 = XXH32_round(v3, p_align[2]);
                    v4 = XXH32_round(v4, p_align[3]);
                    p += 16;
                } while (p <= limit);
            } else {
                do {
                    /* NO SSE */
                    v1 = XXH32_round(v1, XXH_readLE32(p, endian));
                    v2 = XXH32_round(v2, XXH_readLE32(p + 4, endian));
                    v3 = XXH32_round(v3, XXH_readLE32(p + 8, endian));
                    v4 = XXH32_round(v4, XXH_readLE32(p + 12, endian));
                    p += 16;
                } while (p<=limit);
            }
            state->v1 = v1;
            state->v2 = v2;
            state->v3 = v3;
            state->v4 = v4;
        }

        if (p < bEnd) {
            XXH_memcpy(state->mem32, p, (size_t)(bEnd-p));
            state->memsize = (unsigned)(bEnd-p);
        }
    }

    return XXH_OK;
}


XXH_PUBLIC_API XXH_errorcode XXH32_update (XXH32_state_t* state_in, const void* input, size_t len)
{
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH32_update_endian(state_in, input, len, XXH_littleEndian);
    else
        return XXH32_update_endian(state_in, input, len, XXH_bigEndian);
}


FORCE_INLINE U32
XXH32_digest_endian (const XXH32_state_t* state, XXH_endianess endian)
{
    U32 h32;

    if (state->large_len) {
        h32 = XXH_rotl32(state->v1, 1)
            + XXH_rotl32(state->v2, 7)
            + XXH_rotl32(state->v3, 12)
            + XXH_rotl32(state->v4, 18);
    } else {
        h32 = state->v3 /* == seed */ + PRIME32_5;
    }

    h32 += state->total_len_32;

    return XXH32_finalize(h32, state->mem32, state->memsize&15, endian, XXH_aligned);
}


XXH_PUBLIC_API unsigned int XXH32_digest (const XXH32_state_t* state_in)
{
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH32_digest_endian(state_in, XXH_littleEndian);
    else
        return XXH32_digest_endian(state_in, XXH_bigEndian);
}


/*======   Canonical representation   ======*/

/*! Default XXH result types are basic unsigned 32 and 64 bits.
*   The canonical representation follows human-readable write convention, aka big-endian (large digits first).
*   These functions allow transformation of hash result into and from its canonical format.
*   This way, hash values can be written into a file or buffer, remaining comparable across different systems.
*/

XXH_PUBLIC_API void XXH32_canonicalFromHash(XXH32_canonical_t* dst, XXH32_hash_t hash)
{
    XXH_STATIC_ASSERT(sizeof(XXH32_canonical_t) == sizeof(XXH32_hash_t));
    if (XXH_CPU_LITTLE_ENDIAN) hash = XXH_swap32(hash);
    memcpy(dst, &hash, sizeof(*dst));
}

XXH_PUBLIC_API XXH32_hash_t XXH32_hashFromCanonical(const XXH32_canonical_t* src)
{
    return XXH_readBE32(src);
}


#ifndef XXH_NO_LONG_LONG

/* *******************************************************************
*  64-bit hash functions
*********************************************************************/

/*======   Memory access   ======*/

#ifndef MEM_MODULE
# define MEM_MODULE
# if !defined (__VMS) \
  && (defined (__cplusplus) \
  || (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */) )
#   include <stdint.h>
    typedef uint64_t U64;
# else
    /* if compiler doesn't support unsigned long long, replace by another 64-bit type */
    typedef unsigned long long U64;
# endif
#endif


#if (defined(XXH_FORCE_MEMORY_ACCESS) && (XXH_FORCE_MEMORY_ACCESS==2))

/* Force direct memory access. Only works on CPU which support unaligned memory access in hardware */
static U64 XXH_read64(const void* memPtr) { return *(const U64*) memPtr; }

#elif (defined(XXH_FORCE_MEMORY_ACCESS) && (XXH_FORCE_MEMORY_ACCESS==1))

/* __pack instructions are safer, but compiler specific, hence potentially problematic for some compilers */
/* currently only defined for gcc and icc */
typedef union { U32 u32; U64 u64; } __attribute__((packed)) unalign64;
static U64 XXH_read64(const void* ptr) { return ((const unalign64*)ptr)->u64; }

#else

/* portable and safe solution. Generally efficient.
 * see : http://stackoverflow.com/a/32095106/646947
 */

static U64 XXH_read64(const void* memPtr)
{
    U64 val;
    memcpy(&val, memPtr, sizeof(val));
    return val;
}

#endif   /* XXH_FORCE_DIRECT_MEMORY_ACCESS */

#if defined(_MSC_VER)     /* Visual Studio */
#  define XXH_swap64 _byteswap_uint64
#elif XXH_GCC_VERSION >= 403
#  define XXH_swap64 __builtin_bswap64
#else
static U64 XXH_swap64 (U64 x)
{
    return  ((x << 56) & 0xff00000000000000ULL) |
            ((x << 40) & 0x00ff000000000000ULL) |
            ((x << 24) & 0x0000ff0000000000ULL) |
            ((x << 8)  & 0x000000ff00000000ULL) |
            ((x >> 8)  & 0x00000000ff000000ULL) |
            ((x >> 24) & 0x0000000000ff0000ULL) |
            ((x >> 40) & 0x000000000000ff00ULL) |
            ((x >> 56) & 0x00000000000000ffULL);
}
#endif

FORCE_INLINE U64 XXH_readLE64_align(const void* ptr, XXH_endianess endian, XXH_alignment align)
{
    if (align==XXH_unaligned)
        return endian==XXH_littleEndian ? XXH_read64(ptr) : XXH_swap64(XXH_read64(ptr));
    else
        return endian==XXH_littleEndian ? *(const U64*)ptr : XXH_swap64(*(const U64*)ptr);
}

FORCE_INLINE U64 XXH_readLE64(const void* ptr, XXH_endianess endian)
{
    return XXH_readLE64_align(ptr, endian, XXH_unaligned);
}

static U64 XXH_readBE64(const void* ptr)
{
    return XXH_CPU_LITTLE_ENDIAN ? XXH_swap64(XXH_read64(ptr)) : XXH_read64(ptr);
}


/*======   xxh64   ======*/

CONSTANT U64 PRIME64_1 = 11400714785074694791ULL;   /* 0b1001111000110111011110011011000110000101111010111100101010000111 */
CONSTANT U64 PRIME64_2 = 14029467366897019727ULL;   /* 0b1100001010110010101011100011110100100111110101001110101101001111 */
CONSTANT U64 PRIME64_3 =  1609587929392839161ULL;   /* 0b0001011001010110011001111011000110011110001101110111100111111001 */
CONSTANT U64 PRIME64_4 =  9650029242287828579ULL;   /* 0b1000010111101011110010100111011111000010101100101010111001100011 */
CONSTANT U64 PRIME64_5 =  2870177450012600261ULL;   /* 0b0010011111010100111010110010111100010110010101100110011111000101 */

static U64 XXH64_round(U64 acc, U64 input)
{
    acc += input * PRIME64_2;
    acc  = XXH_rotl64(acc, 31);
    acc *= PRIME64_1;
#if !defined(XXH_FORCE_VECTOR) && defined(__x86_64__) && defined(__GNUC__)
    /* See XXH32_round for info. */
    __asm__ __volatile__ ("" : "+r" (acc));
#endif
    return acc;
}

static U64 XXH64_mergeRound(U64 acc, U64 val)
{
    val  = XXH64_round(0, val);
    acc ^= val;
    acc  = acc * PRIME64_1 + PRIME64_4;
    return acc;
}

static U64 XXH64_avalanche(U64 h64)
{
    h64 ^= h64 >> 33;
    h64 *= PRIME64_2;
    h64 ^= h64 >> 29;
    h64 *= PRIME64_3;
    h64 ^= h64 >> 32;
    return h64;
}


#define XXH_get64bits(p) XXH_readLE64_align(p, endian, align)

static U64
XXH64_finalize(U64 h64, const void* ptr, size_t len,
               XXH_endianess endian, XXH_alignment align)
{
    const BYTE* p = (const BYTE*)ptr;

#define PROCESS1_64            \
    h64 ^= (*p++) * PRIME64_5; \
    h64 = XXH_rotl64(h64, 11) * PRIME64_1;

#define PROCESS4_64          \
    h64 ^= (U64)(XXH_get32bits(p)) * PRIME64_1; \
    p+=4;                    \
    h64 = XXH_rotl64(h64, 23) * PRIME64_2 + PRIME64_3;

#define PROCESS8_64 {        \
    U64 const k1 = XXH64_round(0, XXH_get64bits(p)); \
    p+=8;                    \
    h64 ^= k1;               \
    h64  = XXH_rotl64(h64,27) * PRIME64_1 + PRIME64_4; \
}

    switch(len&31) {
      case 24: PROCESS8_64;
                    /* fallthrough */
      case 16: PROCESS8_64;
                    /* fallthrough */
      case  8: PROCESS8_64;
               return XXH64_avalanche(h64);

      case 28: PROCESS8_64;
                    /* fallthrough */
      case 20: PROCESS8_64;
                    /* fallthrough */
      case 12: PROCESS8_64;
                    /* fallthrough */
      case  4: PROCESS4_64;
               return XXH64_avalanche(h64);

      case 25: PROCESS8_64;
                    /* fallthrough */
      case 17: PROCESS8_64;
                    /* fallthrough */
      case  9: PROCESS8_64;
               PROCESS1_64;
               return XXH64_avalanche(h64);

      case 29: PROCESS8_64;
                    /* fallthrough */
      case 21: PROCESS8_64;
                    /* fallthrough */
      case 13: PROCESS8_64;
                    /* fallthrough */
      case  5: PROCESS4_64;
               PROCESS1_64;
               return XXH64_avalanche(h64);

      case 26: PROCESS8_64;
                    /* fallthrough */
      case 18: PROCESS8_64;
                    /* fallthrough */
      case 10: PROCESS8_64;
               PROCESS1_64;
               PROCESS1_64;
               return XXH64_avalanche(h64);

      case 30: PROCESS8_64;
                    /* fallthrough */
      case 22: PROCESS8_64;
                    /* fallthrough */
      case 14: PROCESS8_64;
                    /* fallthrough */
      case  6: PROCESS4_64;
               PROCESS1_64;
               PROCESS1_64;
               return XXH64_avalanche(h64);

      case 27: PROCESS8_64;
                    /* fallthrough */
      case 19: PROCESS8_64;
                    /* fallthrough */
      case 11: PROCESS8_64;
               PROCESS1_64;
               PROCESS1_64;
               PROCESS1_64;
               return XXH64_avalanche(h64);

      case 31: PROCESS8_64;
                    /* fallthrough */
      case 23: PROCESS8_64;
                    /* fallthrough */
      case 15: PROCESS8_64;
                    /* fallthrough */
      case  7: PROCESS4_64;
                    /* fallthrough */
      case  3: PROCESS1_64;
                    /* fallthrough */
      case  2: PROCESS1_64;
                    /* fallthrough */
      case  1: PROCESS1_64;
                    /* fallthrough */
      case  0: return XXH64_avalanche(h64);
    }

    /* impossible to reach */
    assert(0);
    return 0;  /* unreachable, but some compilers complain without it */
}

FORCE_INLINE U64
XXH64_endian_align(const void* input, size_t len, U64 seed,
                XXH_endianess endian, XXH_alignment align)
{
    const BYTE* p = (const BYTE*)input;
    const BYTE* bEnd = p + len;
    U64 h64;

#if defined(XXH_ACCEPT_NULL_INPUT_POINTER) && (XXH_ACCEPT_NULL_INPUT_POINTER>=1)
    if (p==NULL) {
        len=0;
        bEnd=p=(const BYTE*)(size_t)32;
    }
#endif

/* Decent performance (2 GB/s on Core 2 Duo @2.13GHz) is possible on x86_32 with two vectors.
 * It slows down x64 and ARMv7, though. */
#if XXH_VECTORIZE && (defined(__i386__) || defined(XXH_VECTORIZE_XXH64))
    if (len>=32 && endian==XXH_littleEndian) {
        const BYTE* const limit = bEnd - 32;
        typedef U64 U64x2 __attribute__((__vector_size__(16)));
        U64x2 v[2] = {{
            seed + PRIME64_1 + PRIME64_2,
            seed + PRIME64_2
        },{
            seed + 0,
            seed - PRIME64_1
        }};
        if (XXH_FORCE_ALIGN_CHECK && ((size_t)p & 15) == 0) {
            do {
                U64x2 inp = *(const U64x2*)XXH_assume_aligned(p, 16);
                v[0] += inp * PRIME64_2;
                v[0]  = (v[0] << 31) | (v[0] >> 33);
                v[0] *= PRIME64_1;
                p += 16;

                inp = *(const U64x2*)XXH_assume_aligned(p, 16);
                v[1] += inp * PRIME64_2;
                v[1]  = (v[1] << 31) | (v[1] >> 33);
                v[1] *= PRIME64_1;
                p += 16;
            } while (p < limit);
        } else {
            struct loader {
                U64x2 v;
            } __attribute__((__packed__, __may_alias__));
            do {
                U64x2 inp = ((const struct loader *)p)->v;
                v[0] += inp * PRIME64_2;
                v[0]  = (v[0] << 31) | (v[0] >> 33);
                v[0] *= PRIME64_1;
                p += 16;

                inp = ((const struct loader *)p)->v;
                v[1] += inp * PRIME64_2;
                v[1]  = (v[1] << 31) | (v[1] >> 33);
                v[1] *= PRIME64_1;
                p += 16;
            } while (p < limit);
        }
        h64 = XXH_rotl64(v[0][0], 1) + XXH_rotl64(v[0][1], 7) + XXH_rotl64(v[1][0], 12) + XXH_rotl64(v[1][1], 18);

        h64 = XXH64_mergeRound(h64, v[0][0]);
        h64 = XXH64_mergeRound(h64, v[0][1]);
        h64 = XXH64_mergeRound(h64, v[1][0]);
        h64 = XXH64_mergeRound(h64, v[1][1]);
    } else
#endif /* XXH_VECTORIZE && (i386 || XXH_VECTORIZE_XXH64) */
    if (len>=32) {
        const BYTE* const limit = bEnd - 32;

        U64 v1 = seed + PRIME64_1 + PRIME64_2;
        U64 v2 = seed + PRIME64_2;
        U64 v3 = seed + 0;
        U64 v4 = seed - PRIME64_1;
        if (XXH_FORCE_ALIGN_CHECK && endian==XXH_littleEndian && (((size_t)p & 7) == 0)) {
            do {
                const U64* inp = (const U64*)XXH_assume_aligned(p, 8);
                v1 = XXH64_round(v1, inp[0]);
                v2 = XXH64_round(v2, inp[1]);
                v3 = XXH64_round(v3, inp[2]);
                v4 = XXH64_round(v4, inp[3]);
                p += 32;
            } while (p<=limit);
        } else {
            do {
                v1 = XXH64_round(v1, XXH_get64bits(p));
                v2 = XXH64_round(v2, XXH_get64bits(p + 8));
                v3 = XXH64_round(v3, XXH_get64bits(p + 16));
                v4 = XXH64_round(v4, XXH_get64bits(p + 24));
                p += 32;
            } while (p<=limit);
        }

        h64 = XXH_rotl64(v1, 1) + XXH_rotl64(v2, 7) + XXH_rotl64(v3, 12) + XXH_rotl64(v4, 18);
        h64 = XXH64_mergeRound(h64, v1);
        h64 = XXH64_mergeRound(h64, v2);
        h64 = XXH64_mergeRound(h64, v3);
        h64 = XXH64_mergeRound(h64, v4);
    } else {
        h64  = seed + PRIME64_5;
    }

    h64 += (U64) len;

    return XXH64_finalize(h64, p, len, endian, align);
}


XXH_PUBLIC_API unsigned long long XXH64 (const void* input, size_t len, unsigned long long seed)
{
#if 0
    /* Simple version, good for code maintenance, but unfortunately slow for small inputs */
    XXH64_state_t state;
    XXH64_reset(&state, seed);
    XXH64_update(&state, input, len);
    return XXH64_digest(&state);
#else
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

    if (XXH_FORCE_ALIGN_CHECK) {
        if ((((size_t)input) & 7)==0) {  /* Input is aligned, let's leverage the speed advantage */
            if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
                return XXH64_endian_align(input, len, seed, XXH_littleEndian, XXH_aligned);
            else
                return XXH64_endian_align(input, len, seed, XXH_bigEndian, XXH_aligned);
    }   }

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH64_endian_align(input, len, seed, XXH_littleEndian, XXH_unaligned);
    else
        return XXH64_endian_align(input, len, seed, XXH_bigEndian, XXH_unaligned);
#endif
}

/*======   Hash Streaming   ======*/

XXH_PUBLIC_API XXH64_state_t* XXH64_createState(void)
{
    return (XXH64_state_t*)XXH_malloc(sizeof(XXH64_state_t));
}
XXH_PUBLIC_API XXH_errorcode XXH64_freeState(XXH64_state_t* statePtr)
{
    XXH_free(statePtr);
    return XXH_OK;
}

XXH_PUBLIC_API void XXH64_copyState(XXH64_state_t* dstState, const XXH64_state_t* srcState)
{
    memcpy(dstState, srcState, sizeof(*dstState));
}

XXH_PUBLIC_API XXH_errorcode XXH64_reset(XXH64_state_t* statePtr, unsigned long long seed)
{
    XXH64_state_t state;   /* using a local state to memcpy() in order to avoid strict-aliasing warnings */
    memset(&state, 0, sizeof(state));
    state.v1 = seed + PRIME64_1 + PRIME64_2;
    state.v2 = seed + PRIME64_2;
    state.v3 = seed + 0;
    state.v4 = seed - PRIME64_1;
     /* do not write into reserved, planned to be removed in a future version */
    memcpy(statePtr, &state, sizeof(state) - sizeof(state.reserved));
    return XXH_OK;
}

FORCE_INLINE XXH_errorcode
XXH64_update_endian (XXH64_state_t* state, const void* input, size_t len, XXH_endianess endian)
{
    if (input==NULL)
#if defined(XXH_ACCEPT_NULL_INPUT_POINTER) && (XXH_ACCEPT_NULL_INPUT_POINTER>=1)
        return XXH_OK;
#else
        return XXH_ERROR;
#endif

    {   const BYTE* p = (const BYTE*)input;
        const BYTE* const bEnd = p + len;

        state->total_len += len;

        if (state->memsize + len < 32) {  /* fill in tmp buffer */
            XXH_memcpy(((BYTE*)state->mem64) + state->memsize, input, len);
            state->memsize += (U32)len;
            return XXH_OK;
        }

        if (state->memsize) {   /* tmp buffer is full */
            XXH_memcpy(((BYTE*)state->mem64) + state->memsize, input, 32-state->memsize);
            state->v1 = XXH64_round(state->v1, XXH_readLE64(state->mem64+0, endian));
            state->v2 = XXH64_round(state->v2, XXH_readLE64(state->mem64+1, endian));
            state->v3 = XXH64_round(state->v3, XXH_readLE64(state->mem64+2, endian));
            state->v4 = XXH64_round(state->v4, XXH_readLE64(state->mem64+3, endian));
            p += 32-state->memsize;
            state->memsize = 0;
        }

        if (p+32 <= bEnd) {
            const BYTE* const limit = bEnd - 32;
            U64 v1 = state->v1;
            U64 v2 = state->v2;
            U64 v3 = state->v3;
            U64 v4 = state->v4;

            if (XXH_FORCE_ALIGN_CHECK && endian==XXH_littleEndian && (((size_t)p & 7) == 0)) {
                do {
                    const U64* inp = (const U64*)XXH_assume_aligned(p, 8);
                    v1 = XXH64_round(v1, inp[0]);
                    v2 = XXH64_round(v2, inp[1]);
                    v3 = XXH64_round(v3, inp[2]);
                    v4 = XXH64_round(v4, inp[3]);
                    p += 32;
                } while (p<=limit);
            } else {
                do {
                    v1 = XXH64_round(v1, XXH_readLE64(p, endian)); p+=8;
                    v2 = XXH64_round(v2, XXH_readLE64(p, endian)); p+=8;
                    v3 = XXH64_round(v3, XXH_readLE64(p, endian)); p+=8;
                    v4 = XXH64_round(v4, XXH_readLE64(p, endian)); p+=8;
                } while (p<=limit);
            }
            state->v1 = v1;
            state->v2 = v2;
            state->v3 = v3;
            state->v4 = v4;
        }

        if (p < bEnd) {
            XXH_memcpy(state->mem64, p, (size_t)(bEnd-p));
            state->memsize = (unsigned)(bEnd-p);
        }
    }

    return XXH_OK;
}

XXH_PUBLIC_API XXH_errorcode XXH64_update (XXH64_state_t* state_in, const void* input, size_t len)
{
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH64_update_endian(state_in, input, len, XXH_littleEndian);
    else
        return XXH64_update_endian(state_in, input, len, XXH_bigEndian);
}

FORCE_INLINE U64 XXH64_digest_endian (const XXH64_state_t* state, XXH_endianess endian)
{
    U64 h64;

    if (state->total_len >= 32) {
        U64 const v1 = state->v1;
        U64 const v2 = state->v2;
        U64 const v3 = state->v3;
        U64 const v4 = state->v4;

        h64 = XXH_rotl64(v1, 1) + XXH_rotl64(v2, 7) + XXH_rotl64(v3, 12) + XXH_rotl64(v4, 18);
        h64 = XXH64_mergeRound(h64, v1);
        h64 = XXH64_mergeRound(h64, v2);
        h64 = XXH64_mergeRound(h64, v3);
        h64 = XXH64_mergeRound(h64, v4);
    } else {
        h64  = state->v3 /*seed*/ + PRIME64_5;
    }

    h64 += (U64) state->total_len;

    return XXH64_finalize(h64, state->mem64, (size_t)state->total_len, endian, XXH_aligned);
}

XXH_PUBLIC_API unsigned long long XXH64_digest (const XXH64_state_t* state_in)
{
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH64_digest_endian(state_in, XXH_littleEndian);
    else
        return XXH64_digest_endian(state_in, XXH_bigEndian);
}


/*====== Canonical representation   ======*/

XXH_PUBLIC_API void XXH64_canonicalFromHash(XXH64_canonical_t* dst, XXH64_hash_t hash)
{
    XXH_STATIC_ASSERT(sizeof(XXH64_canonical_t) == sizeof(XXH64_hash_t));
    if (XXH_CPU_LITTLE_ENDIAN) hash = XXH_swap64(hash);
    memcpy(dst, &hash, sizeof(*dst));
}

XXH_PUBLIC_API XXH64_hash_t XXH64_hashFromCanonical(const XXH64_canonical_t* src)
{
    return XXH_readBE64(src);
}

#ifndef XXH_NO_ALT_HASHES
/* *******************************************************************
*  32-bit hash functions (alternative)
*********************************************************************/

/* XXH32a and XXH64a share the same inner loop. They just handle
 * the beginning and end differently.
 *
 * The main difference is that XXH32a will duplicate and merge 32-bit
 * values, while XXH64a will split and join 64-bit values. */

/* Note: Used by both XXH32a and XXH64a. */
FORCE_INLINE const BYTE* /* p */
XXH32a_XXH64a_endian_align(U32 state[2][4], const BYTE* p, size_t len,
                    XXH_endianess endian, XXH_alignment align)
{
    const BYTE* bEnd = p + len;


/* SIMD-optimized code */
#if XXH_VECTORIZE
/* x86 slows down significantly on unaligned reads with SSE4.1 instructions.
 * On x86, we want to only do SIMD on aligned pointers.
 * For some reason, clang still produces faster code unaligned on i386, but not GCC.
 * NEON prefers unaligned reads, so we always use SIMD. */
#if XXH_FORCE_ALIGN_CHECK && !(defined(__clang__) && defined(__i386__))
    if (len >= 32 && endian==XXH_littleEndian && align==XXH_aligned) {
#else
    if (len >= 32 && endian==XXH_littleEndian) {
#endif
        /* Assume that v is not aligned, don't take a chance. */
        U32x4 v[2] = {
            XXH_vec_load_unaligned(state[0]),
            XXH_vec_load_unaligned(state[1])
        };

        const U32x4 prime1 = { PRIME32_1, PRIME32_1, PRIME32_1, PRIME32_1 };
        const U32x4 prime2 = { PRIME32_2, PRIME32_2, PRIME32_2, PRIME32_2 };
        const BYTE* const limit = bEnd - 31;
        /* https://moinakg.wordpress.com/2013/01/19/vectorizing-xxhash-for-fun-and-profit/
         * shows that performing two parallel hashes at a time is much better for
         * performance. It produces a different hash, though. */

        /* Aligned reads are faster on all targets except NEON. We want a 16-byte align. */
        if (XXH_FORCE_ALIGN_CHECK && ((size_t)p&15) == 0) {
            do {
                const U32x4 *inp = (const U32x4*)XXH_assume_aligned(p, 16);

                /* XXH32_round */
                v[0] += prime2 * inp[0];
                v[0]  = XXH_vec_rotl32(v[0], 13);
                v[0] *= prime1;

                v[1] += prime2 * inp[1];
                v[1]  = XXH_vec_rotl32(v[1], 13);
                v[1] *= prime1;
                p += 32;
            } while (p < limit);
        } else {
            do {
                /* Load 32 bytes at a time. */
                const U32x4 inp[2] = {
                    XXH_vec_load_unaligned(p),
                    XXH_vec_load_unaligned((p + 16)),
                };
                /* XXH32_round */
                v[0] += inp[0] * prime2;
                v[0]  = XXH_vec_rotl32(v[0], 13);
                v[0] *= prime1;

                v[1] += inp[1] * prime2;
                v[1]  = XXH_vec_rotl32(v[1], 13);
                v[1] *= prime1;

                p += 32;
            } while (p < limit);
        }
        XXH_vec_store_unaligned(state[0], v[0]);
        XXH_vec_store_unaligned(state[1], v[1]);

    } else
#endif /* XXH_VECTORIZE */
    /* no vectorizing or wrong endian */
    if (len >= 32) {
        /* Duplicate the state locally, to make it use registers if possible. */
        XXH_ALIGN_16
        U32 v[2][4];
        const BYTE* const limit = bEnd - 31;

        XXH_memcpy(v, state, sizeof(v));

        if (XXH_FORCE_ALIGN_CHECK && align==XXH_aligned && endian==XXH_littleEndian) {
            do {
                const U32* const inp = (const U32*)XXH_assume_aligned(p, 4);
                /* NO SSE */
                v[0][0] = XXH32_round(v[0][0], inp[0]);
                v[0][1] = XXH32_round(v[0][1], inp[1]);
                v[0][2] = XXH32_round(v[0][2], inp[2]);
                v[0][3] = XXH32_round(v[0][3], inp[3]);

                v[1][0] = XXH32_round(v[1][0], inp[4]);
                v[1][1] = XXH32_round(v[1][1], inp[5]);
                v[1][2] = XXH32_round(v[1][2], inp[6]);
                v[1][3] = XXH32_round(v[1][3], inp[7]);
                p += 32;
            } while (p < limit);
        } else {
            do {
                /* NO SSE */
                v[0][0] = XXH32_round(v[0][0], XXH_get32bits(p)); p+=4;
                v[0][1] = XXH32_round(v[0][1], XXH_get32bits(p)); p+=4;
                v[0][2] = XXH32_round(v[0][2], XXH_get32bits(p)); p+=4;
                v[0][3] = XXH32_round(v[0][3], XXH_get32bits(p)); p+=4;

                v[1][0] = XXH32_round(v[1][0], XXH_get32bits(p)); p+=4;
                v[1][1] = XXH32_round(v[1][1], XXH_get32bits(p)); p+=4;
                v[1][2] = XXH32_round(v[1][2], XXH_get32bits(p)); p+=4;
                v[1][3] = XXH32_round(v[1][3], XXH_get32bits(p)); p+=4;
            } while (p < limit);
        }
       XXH_memcpy(state, v, 8 * sizeof(U32));
    }

    return p;
}

/* Synopsis:
 * U32 seed;
 * U32 v[2][4];
 *            [ 0x01234567 ] // seed
 *                /     \
 * v = [ 0x01234567 ] [ 0x01234567 ] + PRIME32_1 + PRIME32_2 // v[0][0], v[1][0]
 *     [ 0x01234567 ] [ 0x01234567 ] + PRIME32_2             // v[0][1], v[1][1]
 *     [ 0x01234567 ] [ 0x01234567 ] + 0                     // v[0][2], v[1][2]
 *     [ 0x01234567 ] [ 0x01234567 ] - PRIME32_1             // v[0][3], v[1][3]
 *                   v
 *     XXH32a_XXH64a_endian_align();
 *                   v
 * v = [ 0x01234567 ] [ 0x01234567 ] (x4)
 *                   v
 *             XXH32_round();
 *                   v
 *      v[0] = [ 0x4e0d564b ] (x4) // note: We reuse the arrays, so we don't have to waste
 *                                 // the stack. At this point, we are only using v[0].
 *                   v
 *            XXH32_digest();
 */
XXH_PUBLIC_API unsigned int XXH32a (const void* input, size_t len, unsigned int seed)
{
#if 0
    /* Simple version, good for code maintenance, but unfortunately slow for small inputs */
    XXH32a_state_t state;
    XXH32a_reset(&state, seed);
    XXH32a_update(&state, input, len);
    return XXH32a_digest(&state);
#else
    U32 h32;
    const BYTE* p = (const BYTE*)input;
    XXH_endianess endian = ((XXH_endianess)XXH_CPU_LITTLE_ENDIAN==XXH_littleEndian
                                    || XXH_FORCE_NATIVE_FORMAT) ? XXH_littleEndian
                                                                : XXH_bigEndian;
    XXH_alignment align = ((((size_t)input) & 3) == 0) ? XXH_aligned : XXH_unaligned;

#if defined(XXH_ACCEPT_NULL_INPUT_POINTER) && (XXH_ACCEPT_NULL_INPUT_POINTER>=1)
    if (p==NULL) {
        len=0;
        p=(const BYTE*)32;
    }
#endif

    if (len >= 32) {
        U32 v[2][4];
        v[0][0] = v[1][0] = seed + PRIME32_1 + PRIME32_2;
        v[0][1] = v[1][1] = seed + PRIME32_2;
        v[0][2] = v[1][2] = seed + 0;
        v[0][3] = v[1][3] = seed - PRIME32_1;

        p = XXH32a_XXH64a_endian_align(v, p, len, endian, align);

        v[0][0] = XXH32_round(v[0][0], v[1][0]);
        v[0][1] = XXH32_round(v[0][1], v[1][1]);
        v[0][2] = XXH32_round(v[0][2], v[1][2]);
        v[0][3] = XXH32_round(v[0][3], v[1][3]);

        h32 = XXH_rotl32(v[0][0], 1)  + XXH_rotl32(v[0][1], 7)
            + XXH_rotl32(v[0][2], 12) + XXH_rotl32(v[0][3], 18);
    } else {
        h32  = seed + PRIME32_5;
    }

    h32 += (U32)len;
    return XXH32_finalize(h32, p, len&31, endian, align);
#endif
}



/*======   Hash streaming   ======*/

XXH_PUBLIC_API XXH32a_state_t* XXH32a_createState(void)
{
    return (XXH32a_state_t*)XXH_malloc(sizeof(XXH32a_state_t));
}
XXH_PUBLIC_API XXH_errorcode XXH32a_freeState(XXH32a_state_t* statePtr)
{
    XXH_free(statePtr);
    return XXH_OK;
}

XXH_PUBLIC_API void XXH32a_copyState(XXH32a_state_t* dstState, const XXH32a_state_t* srcState)
{
    memcpy(dstState, srcState, sizeof(*dstState));
}

XXH_PUBLIC_API XXH_errorcode XXH32a_reset(XXH32a_state_t* statePtr, unsigned int seed)
{
    XXH32a_state_t state;   /* using a local state to memcpy() in order to avoid strict-aliasing warnings */
    memset(&state, 0, sizeof(state));
    state.v[0][0] = state.v[1][0] = seed + PRIME32_1 + PRIME32_2;
    state.v[0][1] = state.v[1][1] = seed + PRIME32_2;
    state.v[0][2] = state.v[1][2] = seed + 0;
    state.v[0][3] = state.v[1][3] = seed - PRIME32_1;
    /* do not write into reserved, planned to be removed in a future version */
    memcpy(statePtr, &state, sizeof(state) - sizeof(state.reserved));
    return XXH_OK;
}

/* Again, this code is reused for both XXH32a and XXH64a */
FORCE_INLINE XXH_errorcode
XXH32a_XXH64a_update_endian(XXH32a_state_t* state, const void* input, size_t len, XXH_endianess endian)
{
    if (input==NULL)
#if defined(XXH_ACCEPT_NULL_INPUT_POINTER) && (XXH_ACCEPT_NULL_INPUT_POINTER>=1)
        return XXH_OK;
#else
        return XXH_ERROR;
#endif

    {   const BYTE* p = (const BYTE*)input;
        const BYTE* const bEnd = p + len;

        state->total_len_32 += (unsigned)len;
        state->large_len |= (len>=32) | (state->total_len_32>=32);

        if (state->memsize + len < 32)  {   /* fill in tmp buffer */
            XXH_memcpy((BYTE*)(state->mem32) + state->memsize, input, len);
            state->memsize += (unsigned)len;
            return XXH_OK;
        }

        if (state->memsize) {   /* some data left from previous update */
            XXH_memcpy((BYTE*)(state->mem32) + state->memsize, input, 32-state->memsize);
            /* Only one round, not worth it to vectorize. */
            {   const U32* p32 = state->mem32;
                state->v[0][0] = XXH32_round(state->v[0][0], XXH_readLE32(p32, endian)); p32++;
                state->v[0][1] = XXH32_round(state->v[0][1], XXH_readLE32(p32, endian)); p32++;
                state->v[0][2] = XXH32_round(state->v[0][2], XXH_readLE32(p32, endian)); p32++;
                state->v[0][3] = XXH32_round(state->v[0][3], XXH_readLE32(p32, endian)); p32++;

                state->v[1][0] = XXH32_round(state->v[1][0], XXH_readLE32(p32, endian)); p32++;
                state->v[1][1] = XXH32_round(state->v[1][1], XXH_readLE32(p32, endian)); p32++;
                state->v[1][2] = XXH32_round(state->v[1][2], XXH_readLE32(p32, endian)); p32++;
                state->v[1][3] = XXH32_round(state->v[1][3], XXH_readLE32(p32, endian)); p32++;
            }
            p += 32-state->memsize;
            state->memsize = 0;
        }


/* SIMD-optimized code */
#if XXH_VECTORIZE
/* Older x86_64 processors slow down significantly on unaligned reads with
 * SSE4.1 instructions. Sandy Bridge (AVX) is unaffected, so we don't check
 * if you target AVX.
 * NEON is just as fast with unaligned reads, so we always use SIMD. */
#if XXH_FORCE_ALIGN_CHECK && !(defined(__clang__) && defined(__i386__))
    if (len >= 32 && endian==XXH_littleEndian && ((size_t)p&3)==0) {
#else
    if (len >= 32 && endian==XXH_littleEndian) {
#endif
            const BYTE* const limit = bEnd - 31;
            U32x4x2 v;

            const U32x4 prime1 = { PRIME32_1, PRIME32_1, PRIME32_1, PRIME32_1 };
            const U32x4 prime2 = { PRIME32_2, PRIME32_2, PRIME32_2, PRIME32_2 };
            XXH_memcpy(&v, state->v, sizeof(v));

            /* https://moinakg.wordpress.com/2013/01/19/vectorizing-xxhash-for-fun-and-profit/
             * shows that performing two parallel hashes at a time is much better for
             * performance. It produces a different hash, though. */

            /* If we have a 16-byte aligned pointer, we can reinterpret the pointer and
             * use a direct dereference. */
           if (XXH_FORCE_ALIGN_CHECK && ((size_t)p&15) == 0) {
                do {
                    const U32x4* inp = (const U32x4*)__builtin_assume_aligned(p, 16);

                    /* XXH32_round */
                    v.val[0] += inp[0] * prime2;
                    v.val[1] += inp[1] * prime2;

                    v.val[0]  = XXH_vec_rotl32(v.val[0], 13);
                    v.val[1]  = XXH_vec_rotl32(v.val[1], 13);

                    v.val[0] *= prime1;
                    v.val[1] *= prime1;
                    p += 32;
                } while (p <= limit);
            } else {
                do {
                    /* Load 32 bytes at a time. */
                    const U32x4 inp[2] = {
                        XXH_vec_load_unaligned(p),
                        XXH_vec_load_unaligned(p + 16),
                    };
                    /* XXH32_round */
                    v.val[0] += inp[0] * prime2;
                    v.val[1] += inp[1] * prime2;

                    v.val[0]  = XXH_vec_rotl32(v.val[0], 13);
                    v.val[1]  = XXH_vec_rotl32(v.val[1], 13);

                    v.val[0] *= prime1;
                    v.val[1] *= prime1;
                    p += 32;

                } while (p <= limit);
            }
            XXH_memcpy(state->v, &v, sizeof(v));
        } else
#endif /* XXH_VECTORIZE */
        if (len >= 32) {
            U32 v[2][4];
            const BYTE* const limit = bEnd - 31;

            XXH_memcpy(v, state->v, sizeof(v));

            if (XXH_FORCE_ALIGN_CHECK && ((size_t)p&3)==0 && endian==XXH_littleEndian) {
                do {
                    const U32* const inp = (const U32*)XXH_assume_aligned(p, 4);

                    v[0][0] = XXH32_round(v[0][0], inp[0]);
                    v[0][1] = XXH32_round(v[0][1], inp[1]);
                    v[0][2] = XXH32_round(v[0][2], inp[2]);
                    v[0][3] = XXH32_round(v[0][3], inp[3]);

                    v[1][0] = XXH32_round(v[1][0], inp[4]);
                    v[1][1] = XXH32_round(v[1][1], inp[5]);
                    v[1][2] = XXH32_round(v[1][2], inp[6]);
                    v[1][3] = XXH32_round(v[1][3], inp[7]);
                    p += 32;
                } while (p <= limit);
            } else {
                do {
                    v[0][0] = XXH32_round(v[0][0], XXH_readLE32(p, endian)); p+=4;
                    v[0][1] = XXH32_round(v[0][1], XXH_readLE32(p, endian)); p+=4;
                    v[0][2] = XXH32_round(v[0][2], XXH_readLE32(p, endian)); p+=4;
                    v[0][3] = XXH32_round(v[0][3], XXH_readLE32(p, endian)); p+=4;

                    v[1][0] = XXH32_round(v[1][0], XXH_readLE32(p, endian)); p+=4;
                    v[1][1] = XXH32_round(v[1][1], XXH_readLE32(p, endian)); p+=4;
                    v[1][2] = XXH32_round(v[1][2], XXH_readLE32(p, endian)); p+=4;
                    v[1][3] = XXH32_round(v[1][3], XXH_readLE32(p, endian)); p+=4;


                } while (p <= limit);
            }
            XXH_memcpy(state->v, v, sizeof(v));
        }

        if (p < bEnd) {
            XXH_memcpy(state->mem32, p, (size_t)(bEnd-p));
            state->memsize = (unsigned)(bEnd-p);
        }
    }

    return XXH_OK;
}


XXH_PUBLIC_API XXH_errorcode XXH32a_update (XXH32a_state_t* state_in, const void* input, size_t len)
{
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH32a_XXH64a_update_endian(state_in, input, len, XXH_littleEndian);
    else
        return XXH32a_XXH64a_update_endian(state_in, input, len, XXH_bigEndian);
}


FORCE_INLINE U32
XXH32a_digest_endian (const XXH32a_state_t* state, XXH_endianess endian)
{
    U32 h32;

    if (state->large_len) {
        U32 v1 = XXH32_round(state->v[0][0], state->v[1][0]);
        U32 v2 = XXH32_round(state->v[0][1], state->v[1][1]);
        U32 v3 = XXH32_round(state->v[0][2], state->v[1][2]);
        U32 v4 = XXH32_round(state->v[0][3], state->v[1][3]);
        h32 = XXH_rotl32(v1, 1)  + XXH_rotl32(v2, 7)
            + XXH_rotl32(v3, 12) + XXH_rotl32(v4, 18);
    } else {
        h32 = state->v[0][2] /* == seed */ + PRIME32_5;

    }

    h32 += state->total_len_32;

    return  XXH32_finalize(h32, state->mem32, state->memsize, endian, XXH_aligned);
}


XXH_PUBLIC_API unsigned int XXH32a_digest (const XXH32a_state_t* state_in)
{
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH32a_digest_endian(state_in, XXH_littleEndian);
    else
        return XXH32a_digest_endian(state_in, XXH_bigEndian);
}
/* Synopsis:
 * U32 v[2][4];
 * U64 seed;
 * U64 v64[4];
 *
 *        [ 0x0123456789ABCDEF ] // seed, 64-bit
 *                   X
 * v = [ 0x89ABCDEF ] [ 0x01234567 ] + PRIME32_1 + PRIME32_2 // v[0][0], v[1][0]
 *     [ 0x89ABCDEF ] [ 0x01234567 ] + PRIME32_2             // v[0][1], v[1][1]
 *     [ 0x89ABCDEF ] [ 0x01234567 ] + 0                     // v[0][2], v[1][2]
 *     [ 0x89ABCDEF ] [ 0x01234567 ] - PRIME32_1             // v[0][3], v[1][3]
 *                   v
 *     XXH32a_XXH64a_endian_align();
 *                   v
 * v = [ 0x89ABCDEF ] [ 0x01234567 ] (x4)
 *                   X
 *  v64 = [ 0x0123456789ABCDEF ] (x4)
 *                   v
 *           XXH64_finalize();
 */
XXH_PUBLIC_API unsigned long long XXH64a (const void* input, size_t len, unsigned long long seed)
{
#if 0
    /* Simple version, good for code maintenance, but unfortunately slow for small inputs */
    XXH64a_state_t state;
    XXH64a_reset(&state, seed);
    XXH64a_update(&state, input, len);
    return XXH64a_digest(&state);
#else
    U64 h64;
    U32 seeds[2];
    const BYTE* p = (const BYTE*)input;
    XXH_endianess endian = ((XXH_endianess)XXH_CPU_LITTLE_ENDIAN==XXH_littleEndian
                                    || XXH_FORCE_NATIVE_FORMAT) ? XXH_littleEndian : XXH_bigEndian;
    XXH_alignment align = ((((size_t)input) & 3) == 0) ? XXH_aligned : XXH_unaligned;

#if defined(XXH_ACCEPT_NULL_INPUT_POINTER) && (XXH_ACCEPT_NULL_INPUT_POINTER>=1)
    if (p==NULL) {
        len=0;
        p=(const BYTE*)32;
    }
#endif

    /* Set up our state. We use the same primes, but we split the LSB
     * and MSB of the seed between the four lanes. */
    seeds[0] = (U32)(seed & 0xFFFFFFFF);
    seeds[1] = (U32)(seed >> 32);

    if (len >= 32) {
        U32 v[2][4];
        U64 v64[4];

        /* Set up our array */
        v[0][0] = seeds[0] + PRIME32_1 + PRIME32_2;
        v[0][1] = seeds[0] + PRIME32_2;
        v[0][2] = seeds[0] + 0;
        v[0][3] = seeds[0] - PRIME32_1;
        v[1][0] = seeds[1] + PRIME32_1 + PRIME32_2;
        v[1][1] = seeds[1] + PRIME32_2;
        v[1][2] = seeds[1] + 0;
        v[1][3] = seeds[1] - PRIME32_1;

        p = XXH32a_XXH64a_endian_align(v, p, len, endian, align);

        /* Merge the 8 32-bit lanes into 4 64-bit lanes. */
        v64[0] = v[0][0] | ((U64)v[1][0] << 32);
        v64[1] = v[0][1] | ((U64)v[1][1] << 32);
        v64[2] = v[0][2] | ((U64)v[1][2] << 32);
        v64[3] = v[0][3] | ((U64)v[1][3] << 32);

        /* The usual XXH64 ending routine */
        h64 = XXH_rotl64(v64[0], 1) + XXH_rotl64(v64[1], 7)
            + XXH_rotl64(v64[2], 12) + XXH_rotl64(v64[3], 18);
        h64 = XXH64_mergeRound(h64, v64[0]);
        h64 = XXH64_mergeRound(h64, v64[1]);
        h64 = XXH64_mergeRound(h64, v64[2]);
        h64 = XXH64_mergeRound(h64, v64[3]);
    } else {
        h64  = (seeds[0] | ((U64)seeds[1] << 32)) /* seed */ + PRIME64_5;
    }

    h64 += (U32)len;
    return XXH64_finalize(h64, p, len&31, endian, align);
#endif
}


/*======   Hash Streaming   ======*/

XXH_PUBLIC_API XXH64a_state_t* XXH64a_createState(void)
{
    return (XXH64a_state_t*)XXH_malloc(sizeof(XXH64a_state_t));
}
XXH_PUBLIC_API XXH_errorcode XXH64a_freeState(XXH64a_state_t* statePtr)
{
    XXH_free(statePtr);
    return XXH_OK;
}

XXH_PUBLIC_API void XXH64a_copyState(XXH64a_state_t* dstState, const XXH64a_state_t* srcState)
{
    memcpy(dstState, srcState, sizeof(*dstState));
}

XXH_PUBLIC_API XXH_errorcode XXH64a_reset(XXH64a_state_t* statePtr, unsigned long long seed)
{
    XXH64a_state_t state;   /* using a local state to memcpy() in order to avoid strict-aliasing warnings */
    U32 seeds[2];
    memset(&state, 0, sizeof(state));

    /* Split the 64-bit seed into two 32-bit seeds, and distribute it. */
    seeds[0] = (U32)(seed & 0xFFFFFFFF);
    seeds[1] = (U32)(seed >> 32);
    state.v[0][0] = seeds[0] + PRIME32_1 + PRIME32_2;
    state.v[0][1] = seeds[0] + PRIME32_2;
    state.v[0][2] = seeds[0] + 0;
    state.v[0][3] = seeds[0] - PRIME32_1;
    state.v[1][0] = seeds[1] + PRIME32_1 + PRIME32_2;
    state.v[1][1] = seeds[1] + PRIME32_2;
    state.v[1][2] = seeds[1] + 0;
    state.v[1][3] = seeds[1] - PRIME32_1;

    /* do not write into reserved, planned to be removed in a future version */
    memcpy(statePtr, &state, sizeof(state) - sizeof(state.reserved));
    return XXH_OK;
}

XXH_PUBLIC_API XXH_errorcode XXH64a_update (XXH64a_state_t* state_in, const void* input, size_t len)
{
    return XXH32a_update((XXH32a_state_t*)state_in, input, len);
}

FORCE_INLINE U64 XXH64a_digest_endian (const XXH64a_state_t* state, XXH_endianess endian)
{
    U64 h64;
    if (state->large_len) {
        U64 const v1 = state->v[0][0] | ((U64)state->v[1][0] << 32);
        U64 const v2 = state->v[0][1] | ((U64)state->v[1][1] << 32);
        U64 const v3 = state->v[0][2] | ((U64)state->v[1][2] << 32);
        U64 const v4 = state->v[0][3] | ((U64)state->v[1][3] << 32);

        h64 = XXH_rotl64(v1, 1) + XXH_rotl64(v2, 7) + XXH_rotl64(v3, 12) + XXH_rotl64(v4, 18);
        h64 = XXH64_mergeRound(h64, v1);
        h64 = XXH64_mergeRound(h64, v2);
        h64 = XXH64_mergeRound(h64, v3);
        h64 = XXH64_mergeRound(h64, v4);
    } else {
        h64  = (state->v[0][2] | ((U64)state->v[1][2] << 32)) /*seed*/ + PRIME64_5;
    }

    h64 += (U64) state->total_len_32;

    if (((size_t)state->mem32 & 7) == 0) {
        return XXH64_finalize(h64, (U64*)XXH_assume_aligned(state->mem32, 8), (size_t)state->memsize, endian, XXH_aligned);
    } else {
        U64 mem[32];
        memcpy(mem, state->mem32, 32);
        return XXH64_finalize(h64, mem, (size_t)state->memsize, endian, XXH_aligned);
    }
}

XXH_PUBLIC_API unsigned long long XXH64a_digest (const XXH64a_state_t* state_in)
{
    XXH_endianess endian_detected = (XXH_endianess)XXH_CPU_LITTLE_ENDIAN;

    if ((endian_detected==XXH_littleEndian) || XXH_FORCE_NATIVE_FORMAT)
        return XXH64a_digest_endian(state_in, XXH_littleEndian);
    else
        return XXH64a_digest_endian(state_in, XXH_bigEndian);
}

#endif  /* XXH_NO_LONG_LONG */
#endif /* !XXH_NO_ALT_HASHES */
