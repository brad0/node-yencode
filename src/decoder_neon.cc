#include "common.h"
#ifdef __ARM_NEON

#ifndef __aarch64__
#define YENC_DEC_USE_THINTABLE 1
#endif
#include "decoder_common.h"

static uint16_t neon_movemask(uint8x16_t in) {
	uint8x16_t mask = vandq_u8(in, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
# if defined(__aarch64__)
	/* if VADD is slow, can save one using 
	mask = vzip1q_u8(mask, vextq_u8(mask, mask, 8));
	return vaddvq_u16(vreinterpretq_u16_u8(mask));
	*/
	return (vaddv_u8(vget_high_u8(mask)) << 8) | vaddv_u8(vget_low_u8(mask));
# else
	uint8x8_t res = vpadd_u8(vget_low_u8(mask), vget_high_u8(mask));
	res = vpadd_u8(res, res);
	res = vpadd_u8(res, res);
	return vget_lane_u16(vreinterpret_u16_u8(res), 0);
# endif
}
static bool neon_vect_is_nonzero(uint8x16_t v) {
# ifdef __aarch64__
	return !!(vget_lane_u64(vreinterpret_u64_u32(vqmovn_u64(vreinterpretq_u64_u8(v))), 0));
# else
	uint32x4_t tmp1 = vreinterpretq_u32_u8(v);
	uint32x2_t tmp2 = vorr_u32(vget_low_u32(tmp1), vget_high_u32(tmp1));
	return !!(vget_lane_u32(vpmax_u32(tmp2, tmp2), 0));
# endif
}


template<bool isRaw, bool searchEnd>
HEDLEY_ALWAYS_INLINE void do_decode_neon(const uint8_t* HEDLEY_RESTRICT src, long& len, unsigned char* HEDLEY_RESTRICT & p, unsigned char& escFirst, uint16_t& nextMask) {
	uint8x16_t yencOffset = escFirst ? (uint8x16_t){42+64,42,42,42,42,42,42,42,42,42,42,42,42,42,42,42} : vdupq_n_u8(42);
#ifndef __aarch64__
	uint8x16_t lfCompare = vdupq_n_u8('\n');
	if(isRaw) {
		if(nextMask == 1)
			lfCompare[0] = '.';
		if(nextMask == 2)
			lfCompare[1] = '.';
	}
#endif
	for(long i = -len; i; i += sizeof(uint8x16_t)) {
		uint8x16_t data = vld1q_u8(src+i);
		
		// search for special chars
		uint8x16_t cmpEq = vceqq_u8(data, vdupq_n_u8('=')),
#ifdef __aarch64__
		cmp = vqtbx1q_u8(
			cmpEq,
			//                                \n      \r
			(uint8x16_t){0,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
			data
		);
#else
		cmpCr = vceqq_u8(data, vdupq_n_u8('\r')),
		cmp = vorrq_u8(
			vorrq_u8(
				cmpCr,
				vceqq_u8(data, lfCompare)
			),
			cmpEq
		);
#endif
		
		
		uint8x16_t oData = vsubq_u8(data, yencOffset);
		
#ifdef __aarch64__
		if (LIKELIHOOD(0.25 /*guess*/, neon_vect_is_nonzero(cmp)) || (isRaw && LIKELIHOOD(0.001, nextMask!=0))) {
			cmp = vandq_u8(cmp, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
			/* for if CPU has fast VADD?
			uint16_t mask = (vaddv_u8(vget_high_u8(cmp)) << 8) | vaddv_u8(vget_low_u8(cmp));
			uint16_t maskEq = neon_movemask(cmpEq);
			*/
			
			uint8x16_t cmpEqMasked = vandq_u8(cmpEq, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
			uint8x16_t cmpCombined = vpaddq_u8(cmp, cmpEqMasked);
			uint8x8_t cmpPacked = vpadd_u8(vget_low_u8(cmpCombined), vget_high_u8(cmpCombined));
			cmpPacked = vpadd_u8(cmpPacked, cmpPacked);
			uint16_t mask = vget_lane_u16(vreinterpret_u16_u8(cmpPacked), 0);
			uint16_t maskEq = vget_lane_u16(vreinterpret_u16_u8(cmpPacked), 1);
			
			if(isRaw) mask |= nextMask;
#else
		cmp = vandq_u8(cmp, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
		uint8x8_t cmpPacked = vpadd_u8(vget_low_u8(cmp), vget_high_u8(cmp));
		cmpPacked = vpadd_u8(cmpPacked, cmpPacked);
		if(LIKELIHOOD(0.25, vget_lane_u32(vreinterpret_u32_u8(cmpPacked), 0) != 0)) {
			uint8x16_t cmpEqMasked = vandq_u8(cmpEq, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
			uint8x8_t cmpEqPacked = vpadd_u8(vget_low_u8(cmpEqMasked), vget_high_u8(cmpEqMasked));
			cmpEqPacked = vpadd_u8(cmpEqPacked, cmpEqPacked);
			
			cmpPacked = vpadd_u8(cmpPacked, cmpEqPacked);
			uint16_t mask = vget_lane_u16(vreinterpret_u16_u8(cmpPacked), 0);
			uint16_t maskEq = vget_lane_u16(vreinterpret_u16_u8(cmpPacked), 2);
#endif
			
			// handle \r\n. sequences
			// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
			if((isRaw || searchEnd) && LIKELIHOOD(0.15, mask != maskEq)) {
				// vext seems to be a cheap operation on ARM, relative to loads, so only avoid it if there's only one load (isRaw only)
				uint8x16_t tmpData2, nextData;
				if(isRaw && !searchEnd) {
					tmpData2 = vld1q_u8(src+i + 2);
				} else {
					nextData = vld1q_u8(src+i + sizeof(uint8x16_t)); // only 32-bits needed, but there doesn't appear a nice way to do this via intrinsics: https://stackoverflow.com/questions/46910799/arm-neon-intrinsics-convert-d-64-bit-register-to-low-half-of-q-128-bit-regis
					tmpData2 = vextq_u8(data, nextData, 2);
				}
#ifdef __aarch64__
				uint8x16_t cmpCr = vceqq_u8(data, vdupq_n_u8('\r'));
#endif
				uint8x16_t match2Eq, match3Y, match2Dot;
				if(searchEnd) {
					match2Eq = vceqq_u8(tmpData2, vdupq_n_u8('='));
					match3Y  = vceqq_u8(vextq_u8(data, nextData, 3), vdupq_n_u8('y'));
				}
				if(isRaw)
					match2Dot = vceqq_u8(tmpData2, vdupq_n_u8('.'));
				
				// find patterns of \r_.
				if(isRaw && LIKELIHOOD(0.001, neon_vect_is_nonzero(vandq_u8(cmpCr, match2Dot)))) {
					uint8x16_t match1Lf;
					if(searchEnd)
						match1Lf = vceqq_u8(vextq_u8(data, nextData, 1), vdupq_n_u8('\n'));
					else
						match1Lf = vceqq_u8(vld1q_u8(src+i + 1), vdupq_n_u8('\n'));
					uint8x16_t match1Nl = vandq_u8(match1Lf, cmpCr);
					// merge matches of \r\n with those for .
					uint8x16_t match2NlDot = vandq_u8(match2Dot, match1Nl);
					if(searchEnd) {
						uint8x16_t tmpData4 = vextq_u8(data, nextData, 4);
						// match instances of \r\n.\r\n and \r\n.=y
						uint8x16_t match3Cr = vceqq_u8(vextq_u8(data, nextData, 3), vdupq_n_u8('\r'));
						uint8x16_t match4Lf = vceqq_u8(tmpData4, vdupq_n_u8('\n'));
						uint8x16_t match4EqY = vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData4), vdupq_n_u16(0x793d))); // =y
						
						uint8x16_t match3EqY = vandq_u8(match2Eq, match3Y);
						match4EqY = vreinterpretq_u8_u16(vshlq_n_u16(vreinterpretq_u16_u8(match4EqY), 8));
						// merge \r\n and =y matches for tmpData4
						uint8x16_t match4End = vorrq_u8(
							vandq_u8(match3Cr, match4Lf),
							vorrq_u8(match4EqY, vreinterpretq_u8_u16(vshrq_n_u16(vreinterpretq_u16_u8(match3EqY), 8)))
						);
						// merge with \r\n.
						match4End = vandq_u8(match4End, match2NlDot);
						// match \r\n=y
						uint8x16_t match3End = vandq_u8(match3EqY, match1Nl);
						// combine match sequences
						uint8x16_t matchEnd = vorrq_u8(match4End, match3End);
						if(LIKELIHOOD(0.001, neon_vect_is_nonzero(matchEnd))) {
							// terminator found
							// there's probably faster ways to do this, but reverting to scalar code should be good enough
							len += i;
							break;
						}
					}
					uint16_t killDots = neon_movemask(match2NlDot);
					mask |= (killDots << 2) & 0xffff;
#ifdef __aarch64__
					nextMask = killDots >> (sizeof(uint8x16_t)-2);
#else
					// this bitiwse trick works because '.'|'\n' == '.'
					lfCompare = vcombine_u8(vorr_u8(
						vand_u8(
							vext_u8(vget_high_u8(match2NlDot), vdup_n_u8(0), 6),
							vdup_n_u8('.')
						),
						vget_high_u8(lfCompare)
					), vget_high_u8(lfCompare));
#endif
				} else if(searchEnd) {
					if(LIKELIHOOD(0.001, neon_vect_is_nonzero(
						vandq_u8(match2Eq, match3Y)
					))) {
						uint8x16_t match1Lf = vceqq_u8(vextq_u8(data, nextData, 1), vdupq_n_u8('\n'));
						uint8x16_t matchEnd = vandq_u8(
							vandq_u8(match2Eq, match3Y),
							vandq_u8(match1Lf, cmpCr)
						);
						if(LIKELIHOOD(0.001, neon_vect_is_nonzero(matchEnd))) {
							len += i;
							break;
						}
					}
					if(isRaw)
#ifdef __aarch64__
						nextMask = 0;
#else
						lfCompare = vcombine_u8(vget_high_u8(lfCompare), vget_high_u8(lfCompare));
#endif
				} else if(isRaw) // no \r_. found
#ifdef __aarch64__
					nextMask = 0;
#else
					lfCompare = vcombine_u8(vget_high_u8(lfCompare), vget_high_u8(lfCompare));
#endif
			}
			
			// a spec compliant encoder should never generate sequences: ==, =\n and =\r, but we'll handle them to be spec compliant
			// the yEnc specification requires any character following = to be unescaped, not skipped over, so we'll deal with that
			// firstly, check for invalid sequences of = (we assume that these are rare, as a spec compliant yEnc encoder should not generate these)
			if(LIKELIHOOD(0.0001, (mask & ((maskEq << 1) | escFirst)) != 0)) {
				uint16_t tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
				maskEq = (eqFixLUT[(maskEq>>8) & ~(tmp>>7)] << 8) | tmp;
				
				mask &= ~escFirst;
				escFirst = (maskEq >> (sizeof(uint8x16_t)-1));
				// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
				maskEq <<= 1;
				mask &= ~maskEq;
				
				// unescape chars following `=`
				oData = vaddq_u8(
					oData,
					vcombine_u8(
						vld1_u8((uint8_t*)(eqAddLUT + (maskEq&0xff))),
						vld1_u8((uint8_t*)(eqAddLUT + ((maskEq>>8)&0xff)))
					)
				);
			} else {
				// no invalid = sequences found - we can cut out some things from above
				// this code path is a shortened version of above; it's here because it's faster, and what we'll be dealing with most of the time
				escFirst = (maskEq >> (sizeof(uint8x16_t)-1));
				
				oData = vaddq_u8(
					oData,
					vandq_u8(
						vextq_u8(vdupq_n_u8(0), cmpEq, 15),
						vdupq_n_u8(-64)
					)
				);
			}
			yencOffset[0] = (escFirst << 6) | 42;
			
			// all that's left is to 'compress' the data (skip over masked chars)
#ifdef __aarch64__
			vst1q_u8(p, vqtbl1q_u8(
				oData,
				vld1q_u8((uint8_t*)(unshufLUTBig + (mask&0x7fff)))
			));
			p += BitsSetTable256inv[mask & 0xff] + BitsSetTable256inv[mask >> 8];
#else
			// lookup compress masks and shuffle
# ifndef YENC_DEC_USE_THINTABLE
#  define unshufLUT unshufLUTBig
# endif
			vst1_u8(p, vtbl1_u8(
				vget_low_u8(oData),
				vld1_u8((uint8_t*)(unshufLUT + (mask&0xff)))
			));
			p += BitsSetTable256inv[mask & 0xff];
			vst1_u8(p, vtbl1_u8(
				vget_high_u8(oData),
				vld1_u8((uint8_t*)(unshufLUT + (mask>>8)))
			));
			p += BitsSetTable256inv[mask >> 8];
# ifndef YENC_DEC_USE_THINTABLE
#  undef unshufLUT
# endif
			
#endif
			
		} else {
			vst1q_u8(p, oData);
			p += sizeof(uint8x16_t);
			escFirst = 0;
#ifdef __aarch64__
			yencOffset = vdupq_n_u8(42);
#else
			yencOffset = vcombine_u8(vdup_n_u8(42), vget_high_u8(yencOffset));
#endif
		}
	}
#ifndef __aarch64__
	if(lfCompare[0] == '.')
		nextMask = 1;
	else if(lfCompare[1] == '.')
		nextMask = 2;
	else
		nextMask = 	0;
#endif
}

void decoder_set_neon_funcs() {
	decoder_init_lut();
	_do_decode = &do_decode_simd<false, false, sizeof(uint8x16_t), do_decode_neon<false, false> >;
	_do_decode_raw = &do_decode_simd<true, false, sizeof(uint8x16_t), do_decode_neon<true, false> >;
	_do_decode_end = &do_decode_simd<false, true, sizeof(uint8x16_t), do_decode_neon<false, true> >;
	_do_decode_end_raw = &do_decode_simd<true, true, sizeof(uint8x16_t), do_decode_neon<true, true> >;
}
#else
void decoder_set_neon_funcs() {}
#endif
