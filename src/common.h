#ifndef __YENC_COMMON
#define __YENC_COMMON

#if defined(__x86_64__) || \
    defined(__amd64__ ) || \
    defined(__LP64    ) || \
    defined(_M_X64    ) || \
    defined(_M_AMD64  ) || \
    defined(_WIN64    ) || \
    defined(__i386__  ) || \
    defined(__i486__  ) || \
    defined(__i586__  ) || \
    defined(__i686__  ) || \
    defined(_M_I86    ) || \
    defined(_M_IX86   ) || \
    defined(_WIN32    )
	#define PLATFORM_X86 1
#endif
#if defined(__aarch64__) || \
    defined(__armv7__  ) || \
    defined(__arm__    ) || \
    defined(_M_ARM64   ) || \
    defined(_M_ARM     ) || \
    defined(__ARM_ARCH_6__ ) || \
    defined(__ARM_ARCH_7__ ) || \
    defined(__ARM_ARCH_7A__) || \
    defined(__ARM_ARCH_8A__) || \
    (defined(__ARM_ARCH    ) && __ARM_ARCH >= 6)
	#define PLATFORM_ARM 1
#endif


// MSVC compatibility
#if (defined(_M_IX86_FP) && _M_IX86_FP == 2) || defined(_M_X64)
	#define __SSE2__ 1
	#define __SSSE3__ 1
	//#define __SSE4_1__ 1
	#if defined(_MSC_VER) && _MSC_VER >= 1600
		#define __POPCNT__ 1
	#endif
	#if !defined(__AVX__) && (_MSC_VER >= 1700 && defined(__SSE2__))
		#define __AVX__ 1
	#endif
	#if !defined(__AVX2__) && (_MSC_VER >= 1800 && defined(__SSE2__))
		#define __AVX2__ 1
		#define __BMI2__ 1
	#endif
	/* AVX512 requires VS 15.3 */
	#if !defined(__AVX512F__) && (_MSC_VER >= 1911 && defined(__AVX__))
		#define __AVX512BW__ 1
		#define __AVX512F__ 1
	#endif
	/* AVX512VL not available until VS 15.5 */
	#if defined(__AVX512F__) && _MSC_VER >= 1912
		#define __AVX512VL__ 1
	#endif
	#if defined(__AVX512F__) && _MSC_VER >= 1920
		#define __AVX512VBMI2__ 1
	#endif
#endif
#if defined(_M_ARM64)
	#define __aarch64__ 1
	#define __ARM_NEON 1
#endif
#if defined(_M_ARM)
	/*#define __ARM_NEON 1*/
#endif
#ifdef _MSC_VER
#define __BYTE_ORDER__ 1234
#define __ORDER_BIG_ENDIAN__ 4321
#include <intrin.h>
#endif


// combine two 8-bit ints into a 16-bit one
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define UINT16_PACK(a, b) (((a) << 8) | (b))
#define UINT32_PACK(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))
#define UINT32_16_PACK(a, b) (((a) << 16) | (b))
#else
#define UINT16_PACK(a, b) ((a) | ((b) << 8))
#define UINT32_PACK(a, b, c, d) ((a) | ((b) << 8) | ((c) << 16) | ((d) << 24))
#define UINT32_16_PACK(a, b) ((a) | ((b) << 16))
#endif

#ifdef __SSE2__
#include <emmintrin.h>
#define XMM_SIZE 16 /*== (signed int)sizeof(__m128i)*/

#ifdef __SSSE3__
#include <tmmintrin.h>
#endif
#ifdef __POPCNT__
#include <nmmintrin.h>
// POPCNT can never return a negative result, but GCC doesn't seem to realise this, so typecast it to hint it better
#define popcnt32 (unsigned int)_mm_popcnt_u32 
#endif

#if defined(__AVX2__) || defined(__AVX512F__)
#include <immintrin.h>
#endif


#if defined(__tune_core2__) || defined(__tune_atom__)
/* on older Intel CPUs, plus first gen Atom, it is faster to store XMM registers in half */
# define STOREU_XMM(dest, xmm) \
  _mm_storel_epi64((__m128i*)(dest), xmm); \
  _mm_storeh_pi(((__m64*)(dest) +1), _mm_castsi128_ps(xmm))
#else
# define STOREU_XMM(dest, xmm) \
  _mm_storeu_si128((__m128i*)(dest), xmm)
#endif

#endif

#ifdef __ARM_NEON
# include <arm_neon.h>
#endif

#ifdef PLATFORM_ARM
# ifdef __ANDROID__
#  include <cpu-features.h>
# elif defined(__linux__)
#  include <sys/auxv.h>
#  include <asm/hwcap.h>
# endif
static bool cpu_supports_neon() {
# if defined(AT_HWCAP)
#  ifdef __aarch64__
	return getauxval(AT_HWCAP) & HWCAP_ASIMD;
#  else
	return getauxval(AT_HWCAP) & HWCAP_NEON;
#  endif
# elif defined(ANDROID_CPU_FAMILY_ARM)
#  ifdef __aarch64__
	return android_getCpuFeatures() & ANDROID_CPU_ARM64_FEATURE_ASIMD;
#  else
	return android_getCpuFeatures() & ANDROID_CPU_ARM_FEATURE_NEON;
#  endif
# endif
	return true; // assume NEON support, if compiled as such, otherwise
}
#endif

#ifdef _MSC_VER
#define ALIGN_TO(a, v) __declspec(align(a)) v
#else
#define ALIGN_TO(a, v) v __attribute__((aligned(a)))
#endif



#ifdef PLATFORM_X86
static int cpu_flags() {
#ifdef _MSC_VER
	int cpuInfo[4];
	__cpuid(cpuInfo, 1);
	return cpuInfo[2];
#else
	int flags;
	// conveniently stolen from zlib-ng
	__asm__ __volatile__ (
		"cpuid"
	: "=c" (flags)
	: "a" (1)
	: "%edx", "%ebx"
	);
	return flags;
#endif
}
#ifdef _MSC_VER
# if _MSC_VER >= 1600
#  define _cpuidX __cpuidex
#  include <immintrin.h>
#  define _GET_XCR() _xgetbv(_XCR_XFEATURE_ENABLED_MASK)
# else
// not supported
#  define _cpuidX(ar, eax, ecx) ar[0]=0, ar[1]=0, ar[2]=0, ar[3]=0
#  define _GET_XCR() 0
# endif
#else
# include <cpuid.h>
# define _cpuidX(ar, eax, ecx) __cpuid_count(eax, ecx, ar[0], ar[1], ar[2], ar[3])
static inline int _GET_XCR() {
	int xcr0;
	__asm__ __volatile__("xgetbv" : "=a" (xcr0) : "c" (0) : "%edx");
	return xcr0;
}
#endif

enum YEncDecIsaLevel {
	ISA_LEVEL_SSE2,
	ISA_LEVEL_SSSE3,
	ISA_LEVEL_AVX, // includes POPCNT
	ISA_LEVEL_AVX3, // SKX variant; AVX512VL + AVX512BW
	ISA_LEVEL_VBMI2 // ICL
};
#endif

#include <string.h>
#if !defined(_MSC_VER) || defined(_STDINT) || _MSC_VER >= 1900
# include <stdint.h>
# include <stddef.h>
#else
/* Workaround for older MSVC not supporting stdint.h - just pull it from V8 */
# include <v8.h>
#endif


#ifdef __GNUC__
# if __GNUC__ >= 9
#  define LIKELIHOOD(p, c) (__builtin_expect_with_probability(!!(c), 1, p))
# elif defined(__clang__) && __clang_major__ >= 4 // may be available on earlier Clang, but can't find docs
#  define LIKELIHOOD(p, c) (p>0.3 && p<0.7 ? __builtin_unpredictable(!!(c)) : __builtin_expect(!!(c), (p >= 0.5)))
# else
#  define LIKELIHOOD(p, c) (p>0.3 && p<0.7 ? !!(c) : __builtin_expect(!!(c), (p >= 0.5)))
# endif
#else
# define LIKELIHOOD(p, c) (c)
#endif

#endif /* __YENC_COMMON */
