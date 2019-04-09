#include "common.h"

#if defined(__AVX2__) && defined(YENC_ENABLE_AVX256) && YENC_ENABLE_AVX256!=0
#include "decoder_avx2_base.h"
#include "decoder_common.h"
void decoder_set_avx2_funcs() {
	decoder_init_lut();
	_do_decode = &do_decode_simd<false, false, sizeof(__m256i), do_decode_avx2<false, false, ISA_LEVEL_AVX> >;
	_do_decode_raw = &do_decode_simd<true, false, sizeof(__m256i), do_decode_avx2<true, false, ISA_LEVEL_AVX> >;
	_do_decode_end = &do_decode_simd<false, true, sizeof(__m256i), do_decode_avx2<false, true, ISA_LEVEL_AVX> >;
	_do_decode_end_raw = &do_decode_simd<true, true, sizeof(__m256i), do_decode_avx2<true, true, ISA_LEVEL_AVX> >;
}
#else
void decoder_set_avx_funcs();
void decoder_set_avx2_funcs() {
	decoder_set_avx_funcs();
}
#endif
