#include "common.h"

#if defined(__AVX512BW__) && defined(__AVX512VL__)
# if defined(YENC_ENABLE_AVX256) && YENC_ENABLE_AVX256!=0
#  include "decoder_common.h"
#  include "decoder_avx2_base.h"
void decoder_set_avx3_funcs() {
	ALIGN_ALLOC(lookups, sizeof(*lookups), 16);
	decoder_init_lut(lookups->eqFix, lookups->compact);
	_do_decode = &do_decode_simd<false, false, sizeof(__m256i)*2, do_decode_avx2<false, false, ISA_LEVEL_AVX3> >;
	_do_decode_raw = &do_decode_simd<true, false, sizeof(__m256i)*2, do_decode_avx2<true, false, ISA_LEVEL_AVX3> >;
	_do_decode_end = &do_decode_simd<false, true, sizeof(__m256i)*2, do_decode_avx2<false, true, ISA_LEVEL_AVX3> >;
	_do_decode_end_raw = &do_decode_simd<true, true, sizeof(__m256i)*2, do_decode_avx2<true, true, ISA_LEVEL_AVX3> >;
}
# else
#  include "decoder_common.h"
#  include "decoder_sse_base.h"
void decoder_set_avx3_funcs() {
	decoder_sse_init();
	decoder_init_lut(lookups->eqFix, lookups->compact);
	_do_decode = &do_decode_simd<false, false, sizeof(__m128i)*2, do_decode_sse<false, false, ISA_LEVEL_AVX3> >;
	_do_decode_raw = &do_decode_simd<true, false, sizeof(__m128i)*2, do_decode_sse<true, false, ISA_LEVEL_AVX3> >;
	_do_decode_end = &do_decode_simd<false, true, sizeof(__m128i)*2, do_decode_sse<false, true, ISA_LEVEL_AVX3> >;
	_do_decode_end_raw = &do_decode_simd<true, true, sizeof(__m128i)*2, do_decode_sse<true, true, ISA_LEVEL_AVX3> >;
}
# endif
#else
void decoder_set_avx_funcs();
void decoder_set_avx3_funcs() {
	decoder_set_avx_funcs();
}
#endif
