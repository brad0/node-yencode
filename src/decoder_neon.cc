#include "common.h"
#ifdef __ARM_NEON

#ifndef __aarch64__
#define YENC_DEC_USE_THINTABLE 1
#endif
#include "decoder_common.h"


#ifdef _MSC_VER
# define vld1_u8_align vld1_u8_ex
# define vld1q_u8_align vld1q_u8_ex
#elif defined(__GNUC__)
# define vld1_u8_align(p, n) vld1_u8((uint8_t*)__builtin_assume_aligned(p, n))
# define vld1q_u8_align(p, n) vld1q_u8((uint8_t*)__builtin_assume_aligned(p, n))
#else
# define vld1_u8_align(p, n) vld1_u8(p)
# define vld1q_u8_align(p, n) vld1q_u8(p)
#endif

#ifdef YENC_DEC_USE_THINTABLE
uint64_t ALIGN_TO(8, compactLUT[256]);
#else
# pragma pack(16)
struct { char bytes[16]; } ALIGN_TO(16, compactLUT[32768]);
# pragma pack()
#endif

uint8_t eqFixLUT[256];



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
	HEDLEY_ASSUME(escFirst == 0 || escFirst == 1);
	HEDLEY_ASSUME(nextMask == 0 || nextMask == 1 || nextMask == 2);
	uint8x16_t yencOffset = escFirst ? (uint8x16_t){42+64,42,42,42,42,42,42,42,42,42,42,42,42,42,42,42} : vdupq_n_u8(42);
#ifdef __aarch64__
	uint8x8_t nextMaskMix = vdup_n_u8(0);
	if(nextMask)
		nextMaskMix[nextMask-1] = nextMask;
#else
	uint8x16_t lfCompare = vdupq_n_u8('\n');
	if(isRaw) {
		if(nextMask == 1)
			lfCompare[0] = '.';
		if(nextMask == 2)
			lfCompare[1] = '.';
	}
#endif
	for(long i = -len; i; i += sizeof(uint8x16_t)*2) {
		uint8x16_t dataA = vld1q_u8_align(src+i, 16);
		uint8x16_t dataB = vld1q_u8_align(src+i+sizeof(uint8x16_t), 16);
		
		// search for special chars
		uint8x16_t cmpEqA = vceqq_u8(dataA, vdupq_n_u8('=')),
		cmpEqB = vceqq_u8(dataB, vdupq_n_u8('=')),
#ifdef __aarch64__
		cmpA = vqtbx1q_u8(
			cmpEqA,
			//                                \n      \r
			(uint8x16_t){0,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
			dataA
		),
		cmpB = vqtbx1q_u8(
			cmpEqB,
			(uint8x16_t){0,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
			dataB
		);
		if(isRaw) cmpA = vorrq_u8(cmpA, vcombine_u8(nextMaskMix, vdup_n_u8(0)));
#else
		cmpCrA = vceqq_u8(dataA, vdupq_n_u8('\r')),
		cmpCrB = vceqq_u8(dataB, vdupq_n_u8('\r')),
		cmpA = vorrq_u8(
			vorrq_u8(
				cmpCrA,
				vceqq_u8(dataA, lfCompare)
			),
			cmpEqA
		),
		cmpB = vorrq_u8(
			vorrq_u8(
				cmpCrB,
				vceqq_u8(dataB, vdupq_n_u8('\n'))
			),
			cmpEqB
		);
#endif
		
		
#ifdef __aarch64__
		if (LIKELIHOOD(0.42 /*guess*/, neon_vect_is_nonzero(vorrq_u8(cmpA, cmpB)))) {
			cmpA = vandq_u8(cmpA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
			cmpB = vandq_u8(cmpB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
			uint8x16_t cmpMerge = vpaddq_u8(cmpA, cmpB);
			uint8x16_t cmpEqMerge = vpaddq_u8(
				vandq_u8(cmpEqA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128}),
				vandq_u8(cmpEqB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128})
			);
			
			uint8x16_t cmpCombined = vpaddq_u8(cmpMerge, cmpEqMerge);
			uint8x8_t cmpPacked = vpadd_u8(vget_low_u8(cmpCombined), vget_high_u8(cmpCombined));
			uint32_t mask = vget_lane_u32(vreinterpret_u32_u8(cmpPacked), 0);
			uint32_t maskEq = vget_lane_u32(vreinterpret_u32_u8(cmpPacked), 1);
#else
		cmpA = vandq_u8(cmpA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
		cmpB = vandq_u8(cmpB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
		// no vpaddq_u8 in ARMv7, so need extra 64-bit VPADD
		uint8x8_t cmpPacked = vpadd_u8(
			vpadd_u8(
				vget_low_u8(cmpA), vget_high_u8(cmpA)
			),
			vpadd_u8(
				vget_low_u8(cmpB), vget_high_u8(cmpB)
			)
		);
		cmpPacked = vpadd_u8(cmpPacked, cmpPacked);
		uint32_t mask = vget_lane_u32(vreinterpret_u32_u8(cmpPacked), 0);
		if(LIKELIHOOD(0.25, mask != 0)) {
			uint8x16_t cmpEqMaskedA = vandq_u8(cmpEqA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
			uint8x16_t cmpEqMaskedB = vandq_u8(cmpEqB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
			uint8x8_t cmpEqPacked = vpadd_u8(
				vpadd_u8(
					vget_low_u8(cmpEqMaskedA), vget_high_u8(cmpEqMaskedA)
				),
				vpadd_u8(
					vget_low_u8(cmpEqMaskedB), vget_high_u8(cmpEqMaskedB)
				)
			);
			cmpEqPacked = vpadd_u8(cmpEqPacked, cmpEqPacked);
			uint32_t maskEq = vget_lane_u32(vreinterpret_u32_u8(cmpEqPacked), 0);
#endif
			
			// handle \r\n. sequences
			// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
			if((isRaw || searchEnd) && LIKELIHOOD(0.15, mask != maskEq)) {
				// vext seems to be a cheap operation on ARM, relative to loads, so only avoid it if there's only one load (isRaw only)
				uint8x16_t tmpData2, nextData;
				if(isRaw && !searchEnd) {
					tmpData2 = vld1q_u8_align(src+i + 2 + sizeof(uint8x16_t), 2);
				} else {
					nextData = vld1q_u8_align(src+i + sizeof(uint8x16_t)*2, 16); // only 32-bits needed, but there doesn't appear a nice way to do this via intrinsics: https://stackoverflow.com/questions/46910799/arm-neon-intrinsics-convert-d-64-bit-register-to-low-half-of-q-128-bit-regis
					tmpData2 = vextq_u8(dataB, nextData, 2);
				}
#ifdef __aarch64__
				uint8x16_t cmpCrA = vceqq_u8(dataA, vdupq_n_u8('\r'));
				uint8x16_t cmpCrB = vceqq_u8(dataB, vdupq_n_u8('\r'));
# define NEXT_DATA(n) vextq_u8(dataB, nextData, n)
#else
// on ARMv7, prefer loading over VEXT to avoid holding onto nextData reference; this reduces register spills. Shouldn't be an issue on ARMv8 due to 32x 128-bit registers
# define NEXT_DATA(n) vld1q_u8(src+i + n+sizeof(uint8x16_t))
#endif
				uint8x16_t match2EqA, match2DotA;
				uint8x16_t match2EqB, match2DotB;
				if(searchEnd) {
					match2EqA = vextq_u8(cmpEqA, cmpEqB, 2);
					match2EqB = vceqq_u8(tmpData2, vdupq_n_u8('='));
				}
				if(isRaw) {
					match2DotA = vceqq_u8(vextq_u8(dataA, dataB, 2), vdupq_n_u8('.'));
					match2DotB = vceqq_u8(tmpData2, vdupq_n_u8('.'));
				}
				
				// find patterns of \r_.
				if(isRaw && LIKELIHOOD(0.001, neon_vect_is_nonzero(vorrq_u8(
					vandq_u8(cmpCrA, match2DotA),
					vandq_u8(cmpCrB, match2DotB)
				)))) {
					uint8x16_t match1LfA = vceqq_u8(vextq_u8(dataA, dataB, 1), vdupq_n_u8('\n'));
					uint8x16_t match1LfB;
					if(searchEnd)
						match1LfB = vceqq_u8(NEXT_DATA(1), vdupq_n_u8('\n'));
					else
						match1LfB = vceqq_u8(vld1q_u8(src+i + 1+sizeof(uint8x16_t)), vdupq_n_u8('\n'));
					uint8x16_t match1NlA = vandq_u8(match1LfA, cmpCrA);
					uint8x16_t match1NlB = vandq_u8(match1LfB, cmpCrB);
					// merge matches of \r\n with those for .
#ifdef __aarch64__
					uint8x16_t match2NlDotA = vandq_u8(match2DotA, match1NlA);
					uint8x16_t match2NlDotB = vandq_u8(match2DotB, match1NlB);
#else
					// try to get compiler to recompute match2Dot to avoid register spilling above
					uint8x16_t match2NlDotA = vandq_u8(vceqq_u8(vextq_u8(dataA, dataB, 2), vdupq_n_u8('.')), match1NlA);
					uint8x16_t match2NlDotB = vandq_u8(vceqq_u8(NEXT_DATA(2), vdupq_n_u8('.')), match1NlB);
#endif
					if(searchEnd) {
						uint8x16_t tmpData3 = NEXT_DATA(3);
						uint8x16_t tmpData4 = NEXT_DATA(4);
						// match instances of \r\n.\r\n and \r\n.=y
						uint8x16_t match3Cr = vbslq_u8(vdupq_n_u8(0x0f),
							vextq_u8(cmpCrA, cmpCrB, 3),
							vceqq_u8(tmpData3, vdupq_n_u8('\r'))
						);
						uint8x16_t match4Lf = vbslq_u8(vdupq_n_u8(0x0f),
							vextq_u8(match1LfA, match1LfB, 3),
							vceqq_u8(tmpData4, vdupq_n_u8('\n'))
						);
						uint8x16_t match4EqY = vbslq_u8(vdupq_n_u8(0x0f),
							// match =y
							vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(vextq_u8(dataA, dataB, 4)), vdupq_n_u16(0x793d))),
							vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData4), vdupq_n_u16(0x793d)))
						);
						
						uint8x16_t match3Y = vbslq_u8(vdupq_n_u8(0x0f),
							vceqq_u8(vextq_u8(dataA, dataB, 3), vdupq_n_u8('y')),
							vceqq_u8(tmpData3, vdupq_n_u8('y'))
						);
						uint8x16_t match3EqY = vandq_u8(match3Y, vbslq_u8(vdupq_n_u8(0x0f), match2EqA, match2EqB));
						// merge \r\n and =y matches for tmpData4
						uint8x16_t match4End = vorrq_u8(
							vandq_u8(match3Cr, match4Lf),
							vreinterpretq_u8_u16(vsriq_n_u16(vreinterpretq_u16_u8(match4EqY), vreinterpretq_u16_u8(match3EqY), 8))
						);
						// merge with \r\n.
						uint8x16_t match2NlDot = vbslq_u8(vdupq_n_u8(0x0f), match2NlDotA, match2NlDotB);
						match4End = vandq_u8(match4End, match2NlDot);
						// match \r\n=y
						uint8x16_t match1Nl = vbslq_u8(vdupq_n_u8(0x0f), match1NlA, match1NlB);
						uint8x16_t match3End = vandq_u8(match3EqY, match1Nl);
						// combine match sequences
						if(LIKELIHOOD(0.001, neon_vect_is_nonzero(vorrq_u8(match4End, match3End)))) {
							// terminator found
							// there's probably faster ways to do this, but reverting to scalar code should be good enough
							len += i;
							break;
						}
					}
#ifdef __aarch64__
					uint8x16_t match2NlDotBMasked = vandq_u8(match2NlDotB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
					uint8x16_t mergeKillDots = vpaddq_u8(
						vandq_u8(match2NlDotA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128}),
						match2NlDotBMasked
					);
					uint8x8_t mergeKillDots2 = vpadd_u8(vget_low_u8(mergeKillDots), vget_high_u8(mergeKillDots));
#else
					uint8x16_t match2NlDotMaskedA = vandq_u8(match2NlDotA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
					uint8x16_t match2NlDotMaskedB = vandq_u8(match2NlDotB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
					uint8x8_t mergeKillDots2 = vpadd_u8(
						vpadd_u8(
							vget_low_u8(match2NlDotMaskedA), vget_high_u8(match2NlDotMaskedA)
						),
						vpadd_u8(
							vget_low_u8(match2NlDotMaskedB), vget_high_u8(match2NlDotMaskedB)
						)
					);
#endif
					mergeKillDots2 = vpadd_u8(mergeKillDots2, mergeKillDots2);
					uint32_t killDots = vget_lane_u32(vreinterpret_u32_u8(mergeKillDots2), 0);
					mask |= (killDots << 2) & 0xffffffff;
					cmpPacked = vreinterpret_u8_u32(vmov_n_u32(mask));
#ifdef __aarch64__
					nextMaskMix = vget_high_u8(match2NlDotBMasked);
					nextMaskMix = vreinterpret_u8_u64(vshr_n_u64(vreinterpret_u64_u8(nextMaskMix), 48+6));
#else
					// this bitiwse trick works because '.'|'\n' == '.'
					lfCompare = vcombine_u8(vorr_u8(
						vand_u8(
							vext_u8(vget_high_u8(match2NlDotB), vdup_n_u8(0), 6),
							vdup_n_u8('.')
						),
						vget_high_u8(lfCompare)
					), vget_high_u8(lfCompare));
#endif
				} else if(searchEnd) {
					uint8x16_t match3EqYA = vandq_u8(match2EqA, vceqq_u8(vextq_u8(dataA, dataB, 3), vdupq_n_u8('y')));
					uint8x16_t match3EqYB = vandq_u8(match2EqB, vceqq_u8(NEXT_DATA(3), vdupq_n_u8('y')));
					if(LIKELIHOOD(0.001, neon_vect_is_nonzero(vorrq_u8(
						match3EqYA, match3EqYB
					)))) {
						uint8x16_t match1LfA = vceqq_u8(vextq_u8(dataA, dataB, 1), vdupq_n_u8('\n'));
						uint8x16_t match1LfB = vceqq_u8(NEXT_DATA(1), vdupq_n_u8('\n'));
						uint8x16_t matchEnd = vorrq_u8(
							vandq_u8(match3EqYA, vandq_u8(match1LfA, cmpCrA)),
							vandq_u8(match3EqYB, vandq_u8(match1LfB, cmpCrB))
						);
						if(LIKELIHOOD(0.001, neon_vect_is_nonzero(matchEnd))) {
							len += i;
							break;
						}
					}
#undef NEXT_DATA
					if(isRaw)
#ifdef __aarch64__
						nextMaskMix = vdup_n_u8(0);
#else
						lfCompare = vcombine_u8(vget_high_u8(lfCompare), vget_high_u8(lfCompare));
#endif
				} else if(isRaw) // no \r_. found
#ifdef __aarch64__
					nextMaskMix = vdup_n_u8(0);
#else
					lfCompare = vcombine_u8(vget_high_u8(lfCompare), vget_high_u8(lfCompare));
#endif
			}
			
			// a spec compliant encoder should never generate sequences: ==, =\n and =\r, but we'll handle them to be spec compliant
			// the yEnc specification requires any character following = to be unescaped, not skipped over, so we'll deal with that
			// firstly, check for invalid sequences of = (we assume that these are rare, as a spec compliant yEnc encoder should not generate these)
			if(LIKELIHOOD(0.0001, (mask & ((maskEq << 1) | escFirst)) != 0)) {
				uint8_t tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
				uint32_t maskEq2 = tmp;
				for(int j=8; j<32; j+=8) {
					tmp = eqFixLUT[((maskEq>>j)&0xff) & ~(tmp>>7)];
					maskEq2 |= tmp<<j;
				}
				maskEq = maskEq2;
				
				// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
				maskEq = (maskEq<<1) | escFirst;
				mask &= ~maskEq;
				cmpPacked = vreinterpret_u8_u32(vmov_n_u32(mask));
				escFirst = tmp>>7;
				
				// unescape chars following `=`
				uint8x8_t maskEqTemp = vreinterpret_u8_u32(vmov_n_u32(maskEq));
#ifdef __aarch64__
				uint8x16_t vMaskEqA = vqtbl1q_u8(
					vcombine_u8(maskEqTemp, vdup_n_u8(0)),
					(uint8x16_t){0,0,0,0,0,0,0,0, 1,1,1,1,1,1,1,1}
				);
				uint8x16_t vMaskEqB = vqtbl1q_u8(
					vcombine_u8(maskEqTemp, vdup_n_u8(0)),
					(uint8x16_t){2,2,2,2,2,2,2,2, 3,3,3,3,3,3,3,3}
				);
#else
				uint8x16_t vMaskEqA = vcombine_u8(
					vdup_lane_u8(maskEqTemp, 0),
					vdup_lane_u8(maskEqTemp, 1)
				);
				uint8x16_t vMaskEqB = vcombine_u8(
					vdup_lane_u8(maskEqTemp, 2),
					vdup_lane_u8(maskEqTemp, 3)
				);
#endif
				vMaskEqA = vtstq_u8(vMaskEqA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
				vMaskEqB = vtstq_u8(vMaskEqB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
				
				dataA = vsubq_u8(
					dataA,
					vbslq_u8(vMaskEqA, vdupq_n_u8(64+42), vdupq_n_u8(42))
				);
				dataB = vsubq_u8(
					dataB,
					vbslq_u8(vMaskEqB, vdupq_n_u8(64+42), vdupq_n_u8(42))
				);
			} else {
				// no invalid = sequences found - we can cut out some things from above
				// this code path is a shortened version of above; it's here because it's faster, and what we'll be dealing with most of the time
				escFirst = (maskEq >> 31);
				
				dataA = vsubq_u8(
					dataA,
					vbslq_u8(
						vextq_u8(vdupq_n_u8(0), cmpEqA, 15),
						vdupq_n_u8(64+42),
						yencOffset
					)
				);
				dataB = vsubq_u8(
					dataB,
					vbslq_u8(
						vextq_u8(cmpEqA, cmpEqB, 15),
						vdupq_n_u8(64+42),
						vdupq_n_u8(42)
					)
				);
			}
			yencOffset[0] = (escFirst << 6) | 42;
			
			// all that's left is to 'compress' the data (skip over masked chars)
			uint32_t counts = 0x08080808 - vget_lane_u32(vreinterpret_u32_u8(vcnt_u8(cmpPacked)), 0);
#ifdef __aarch64__
			counts += counts >> 8;
			vst1q_u8(p, vqtbl1q_u8(
				dataA,
				vld1q_u8_align((uint8_t*)(compactLUT + (mask&0x7fff)), 16)
			));
			p += counts & 0xff;
			mask >>= 16;
			vst1q_u8(p, vqtbl1q_u8(
				dataB,
				vld1q_u8_align((uint8_t*)(compactLUT + (mask&0x7fff)), 16)
			));
			p += (counts>>16) & 0xff;
#else
			// lookup compress masks and shuffle
			vst1_u8(p, vtbl1_u8(
				vget_low_u8(dataA),
				vld1_u8_align((uint8_t*)(compactLUT + (mask&0xff)), 8)
			));
			p += counts & 0xff;
			mask >>= 8;
			vst1_u8(p, vtbl1_u8(
				vget_high_u8(dataA),
				vld1_u8_align((uint8_t*)(compactLUT + (mask&0xff)), 8)
			));
			p += (counts>>8) & 0xff;
			mask >>= 8;
			vst1_u8(p, vtbl1_u8(
				vget_low_u8(dataB),
				vld1_u8_align((uint8_t*)(compactLUT + (mask&0xff)), 8)
			));
			p += (counts>>16) & 0xff;
			mask >>= 8;
			vst1_u8(p, vtbl1_u8(
				vget_high_u8(dataB),
				vld1_u8_align((uint8_t*)(compactLUT + (mask&0xff)), 8)
			));
			p += (counts>>24) & 0xff;
			
#endif
			
		} else {
			dataA = vsubq_u8(dataA, yencOffset);
			dataB = vsubq_u8(dataB, vdupq_n_u8(42));
			vst1q_u8(p, dataA);
			vst1q_u8(p+sizeof(uint8x16_t), dataB);
			p += sizeof(uint8x16_t)*2;
			escFirst = 0;
#ifdef __aarch64__
			yencOffset = vdupq_n_u8(42);
#else
			yencOffset = vcombine_u8(vdup_n_u8(42), vget_high_u8(yencOffset));
#endif
		}
	}
#ifdef __aarch64__
	nextMask = vget_lane_u16(vreinterpret_u16_u8(nextMaskMix), 0);
	nextMask += nextMask >> 8;
	nextMask &= 3;
#else
	if(lfCompare[0] == '.')
		nextMask = 1;
	else if(lfCompare[1] == '.')
		nextMask = 2;
	else
		nextMask = 	0;
#endif
}

void decoder_set_neon_funcs() {
	decoder_init_lut(eqFixLUT, compactLUT);
	_do_decode = &do_decode_simd<false, false, sizeof(uint8x16_t)*2, do_decode_neon<false, false> >;
	_do_decode_raw = &do_decode_simd<true, false, sizeof(uint8x16_t)*2, do_decode_neon<true, false> >;
	_do_decode_end = &do_decode_simd<false, true, sizeof(uint8x16_t)*2, do_decode_neon<false, true> >;
	_do_decode_end_raw = &do_decode_simd<true, true, sizeof(uint8x16_t)*2, do_decode_neon<true, true> >;
}
#else
void decoder_set_neon_funcs() {}
#endif
