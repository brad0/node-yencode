#include "common.h"
#ifdef __ARM_NEON

#include "decoder_common.h"

#pragma pack(16)
struct { char bytes[16]; } ALIGN_TO(16, compactLUT[32768]);
#pragma pack()

uint8_t eqFixLUT[256];



static bool neon_vect_is_nonzero(uint8x16_t v) {
	return !!(vget_lane_u64(vreinterpret_u64_u32(vqmovn_u64(vreinterpretq_u64_u8(v))), 0));
}

static HEDLEY_ALWAYS_INLINE uint8x16_t mergeCompares(uint8x16_t a, uint8x16_t b, uint8x16_t c, uint8x16_t d) {
	// constant vectors arbitrarily chosen from ones that can be reused; exact ordering of bits doesn't matter, we just need to mix them in
	return vbslq_u8(
		vdupq_n_u8('='),
		vbslq_u8(vdupq_n_u8('y'), a, b),
		vbslq_u8(vdupq_n_u8('y'), c, d)
	);
}


template<bool isRaw, bool searchEnd>
HEDLEY_ALWAYS_INLINE void do_decode_neon(const uint8_t* HEDLEY_RESTRICT src, long& len, unsigned char* HEDLEY_RESTRICT & p, unsigned char& escFirst, uint16_t& nextMask) {
	HEDLEY_ASSUME(escFirst == 0 || escFirst == 1);
	HEDLEY_ASSUME(nextMask == 0 || nextMask == 1 || nextMask == 2);
	uint8x8_t nextMaskMix = vdup_n_u8(0);
	if(nextMask)
		nextMaskMix[nextMask-1] = nextMask;
	uint8x16_t yencOffset = escFirst ? (uint8x16_t){42+64,42,42,42,42,42,42,42,42,42,42,42,42,42,42,42} : vdupq_n_u8(42);
	for(long i = -len; i; i += sizeof(uint8x16_t)*4) {
		uint8x16_t dataA = vld1q_u8(src+i);
		uint8x16_t dataB = vld1q_u8(src+i+sizeof(uint8x16_t));
		uint8x16_t dataC = vld1q_u8(src+i+sizeof(uint8x16_t)*2);
		uint8x16_t dataD = vld1q_u8(src+i+sizeof(uint8x16_t)*3);
		
		// search for special chars
		uint8x16_t cmpEqA = vceqq_u8(dataA, vdupq_n_u8('=')),
		cmpEqB = vceqq_u8(dataB, vdupq_n_u8('=')),
		cmpEqC = vceqq_u8(dataC, vdupq_n_u8('=')),
		cmpEqD = vceqq_u8(dataD, vdupq_n_u8('=')),
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
		),
		cmpC = vqtbx1q_u8(
			cmpEqC,
			(uint8x16_t){0,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
			dataC
		),
		cmpD = vqtbx1q_u8(
			cmpEqD,
			(uint8x16_t){0,0,0,0,0,0,0,0,0,0,255,0,0,255,0,0},
			dataD
		);
		if(isRaw) cmpA = vorrq_u8(cmpA, vcombine_u8(nextMaskMix, vdup_n_u8(0)));
		
		if (LIKELIHOOD(0.42 /*guess*/, neon_vect_is_nonzero(vorrq_u8(
			vorrq_u8(cmpA, cmpB),
			vorrq_u8(cmpC, cmpD)
		)))) {
			uint8x16_t cmpMerge = vpaddq_u8(
				vpaddq_u8(
					vandq_u8(cmpA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128}),
					vandq_u8(cmpB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128})
				),
				vpaddq_u8(
					vandq_u8(cmpC, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128}),
					vandq_u8(cmpD, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128})
				)
			);
			uint8x16_t cmpEqMerge = vpaddq_u8(
				vpaddq_u8(
					vandq_u8(cmpEqA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128}),
					vandq_u8(cmpEqB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128})
				),
				vpaddq_u8(
					vandq_u8(cmpEqC, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128}),
					vandq_u8(cmpEqD, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128})
				)
			);
			
			uint8x16_t cmpCombined = vpaddq_u8(cmpMerge, cmpEqMerge);
			uint64_t mask = vgetq_lane_u64(vreinterpretq_u64_u8(cmpCombined), 0);
			uint64_t maskEq = vgetq_lane_u64(vreinterpretq_u64_u8(cmpCombined), 1);
			
			// handle \r\n. sequences
			// RFC3977 requires the first dot on a line to be stripped, due to dot-stuffing
			if((isRaw || searchEnd) && LIKELIHOOD(0.15, mask != maskEq)) {
				// vext seems to be a cheap operation on ARM, relative to loads, so only avoid it if there's only one load (isRaw only)
				uint8x16_t tmpData2, nextData;
				if(isRaw && !searchEnd) {
					tmpData2 = vld1q_u8(src+i + 2 + sizeof(uint8x16_t)*3);
				} else {
					nextData = vld1q_u8(src+i + sizeof(uint8x16_t)*4); // only 32-bits needed, but there doesn't appear a nice way to do this via intrinsics: https://stackoverflow.com/questions/46910799/arm-neon-intrinsics-convert-d-64-bit-register-to-low-half-of-q-128-bit-regis
					tmpData2 = vextq_u8(dataD, nextData, 2);
				}
				uint8x16_t cmpCrA = vceqq_u8(dataA, vdupq_n_u8('\r'));
				uint8x16_t cmpCrB = vceqq_u8(dataB, vdupq_n_u8('\r'));
				uint8x16_t cmpCrC = vceqq_u8(dataC, vdupq_n_u8('\r'));
				uint8x16_t cmpCrD = vceqq_u8(dataD, vdupq_n_u8('\r'));
				uint8x16_t match2EqA, match2DotA;
				uint8x16_t match2EqB, match2DotB;
				uint8x16_t match2EqC, match2DotC;
				uint8x16_t match2EqD, match2DotD;
				if(searchEnd) {
					match2EqA = vextq_u8(cmpEqA, cmpEqB, 2);
					match2EqB = vextq_u8(cmpEqB, cmpEqC, 2);
					match2EqC = vextq_u8(cmpEqC, cmpEqD, 2);
					match2EqD = vceqq_u8(tmpData2, vdupq_n_u8('='));
				}
				if(isRaw) {
					match2DotA = vceqq_u8(vextq_u8(dataA, dataB, 2), vdupq_n_u8('.'));
					match2DotB = vceqq_u8(vextq_u8(dataB, dataC, 2), vdupq_n_u8('.'));
					match2DotC = vceqq_u8(vextq_u8(dataC, dataD, 2), vdupq_n_u8('.'));
					match2DotD = vceqq_u8(tmpData2, vdupq_n_u8('.'));
				}
				
				// find patterns of \r_.
				if(isRaw && LIKELIHOOD(0.001, neon_vect_is_nonzero(vorrq_u8(
					vorrq_u8(
						vandq_u8(cmpCrA, match2DotA),
						vandq_u8(cmpCrB, match2DotB)
					), vorrq_u8(
						vandq_u8(cmpCrC, match2DotC),
						vandq_u8(cmpCrD, match2DotD)
					)
				)))) {
					uint8x16_t match1LfA = vceqq_u8(vextq_u8(dataA, dataB, 1), vdupq_n_u8('\n'));
					uint8x16_t match1LfB = vceqq_u8(vextq_u8(dataB, dataC, 1), vdupq_n_u8('\n'));
					uint8x16_t match1LfC = vceqq_u8(vextq_u8(dataC, dataD, 1), vdupq_n_u8('\n'));
					uint8x16_t match1LfD;
					if(searchEnd)
						match1LfD = vceqq_u8(vextq_u8(dataD, nextData, 1), vdupq_n_u8('\n'));
					else
						match1LfD = vceqq_u8(vld1q_u8(src+i + 1+sizeof(uint8x16_t)*3), vdupq_n_u8('\n'));
					uint8x16_t match1NlA = vandq_u8(match1LfA, cmpCrA);
					uint8x16_t match1NlB = vandq_u8(match1LfB, cmpCrB);
					uint8x16_t match1NlC = vandq_u8(match1LfC, cmpCrC);
					uint8x16_t match1NlD = vandq_u8(match1LfD, cmpCrD);
					// merge matches of \r\n with those for .
					uint8x16_t match2NlDotA = vandq_u8(match2DotA, match1NlA);
					uint8x16_t match2NlDotB = vandq_u8(match2DotB, match1NlB);
					uint8x16_t match2NlDotC = vandq_u8(match2DotC, match1NlC);
					uint8x16_t match2NlDotD = vandq_u8(match2DotD, match1NlD);
					if(searchEnd) {
						uint8x16_t tmpData3 = vextq_u8(dataD, nextData, 3);
						uint8x16_t tmpData4 = vextq_u8(dataD, nextData, 4);
						// match instances of \r\n.\r\n and \r\n.=y
						uint8x16_t match3Cr = mergeCompares(
							vextq_u8(cmpCrA, cmpCrB, 3),
							vextq_u8(cmpCrB, cmpCrC, 3),
							vextq_u8(cmpCrC, cmpCrD, 3),
							vceqq_u8(tmpData3, vdupq_n_u8('\r'))
						);
						uint8x16_t match4Lf = mergeCompares(
							vextq_u8(match1LfA, match1LfB, 3),
							vextq_u8(match1LfB, match1LfC, 3),
							vextq_u8(match1LfC, match1LfD, 3),
							vceqq_u8(tmpData4, vdupq_n_u8('\n'))
						);
						uint8x16_t match4EqY = mergeCompares(
							// match with =y
							vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(vextq_u8(dataA, dataB, 4)), vdupq_n_u16(0x793d))),
							vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(vextq_u8(dataB, dataC, 4)), vdupq_n_u16(0x793d))),
							vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(vextq_u8(dataC, dataD, 4)), vdupq_n_u16(0x793d))),
							vreinterpretq_u8_u16(vceqq_u16(vreinterpretq_u16_u8(tmpData4), vdupq_n_u16(0x793d)))
						);
						
						uint8x16_t match3Y = mergeCompares(
							vceqq_u8(vextq_u8(dataA, dataB, 3), vdupq_n_u8('y')),
							vceqq_u8(vextq_u8(dataB, dataC, 3), vdupq_n_u8('y')),
							vceqq_u8(vextq_u8(dataC, dataD, 3), vdupq_n_u8('y')),
							vceqq_u8(tmpData3, vdupq_n_u8('y'))
						);
						uint8x16_t match2Eq = mergeCompares(match2EqA, match2EqB, match2EqC, match2EqD);
						uint8x16_t match3EqY = vandq_u8(match2Eq, match3Y);
						// merge \r\n and =y matches for tmpData4
						uint8x16_t match4End = vorrq_u8(
							vandq_u8(match3Cr, match4Lf),
							vreinterpretq_u8_u16(vsriq_n_u16(vreinterpretq_u16_u8(match4EqY), vreinterpretq_u16_u8(match3EqY), 8))
						);
						// merge with \r\n.
						uint8x16_t match2NlDot = mergeCompares(match2NlDotA, match2NlDotB, match2NlDotC, match2NlDotD);
						match4End = vandq_u8(match4End, match2NlDot);
						// match \r\n=y
						uint8x16_t match1Nl = mergeCompares(match1NlA, match1NlB, match1NlC, match1NlD);
						uint8x16_t match3End = vandq_u8(match3EqY, match1Nl);
						// combine match sequences
						if(LIKELIHOOD(0.001, neon_vect_is_nonzero(vorrq_u8(match4End, match3End)))) {
							// terminator found
							// there's probably faster ways to do this, but reverting to scalar code should be good enough
							len += i;
							break;
						}
					}
					uint8x16_t match2NlDotDMasked = vandq_u8(match2NlDotD, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
					uint8x16_t mergeKillDots = vpaddq_u8(
						vpaddq_u8(
							vandq_u8(match2NlDotA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128}),
							vandq_u8(match2NlDotB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128})
						),
						vpaddq_u8(
							vandq_u8(match2NlDotC, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128}),
							match2NlDotDMasked
						)
					);
					uint8x8_t mergeKillDots2 = vpadd_u8(vget_low_u8(mergeKillDots), vget_high_u8(mergeKillDots));
					uint64_t killDots = vget_lane_u64(vreinterpret_u64_u8(mergeKillDots2), 0);
					mask |= (killDots << 2) & 0xffffffffffffffffULL;
					cmpCombined = vreinterpretq_u8_u64(vcombine_u64(vmov_n_u64(mask), vdup_n_u64(0)));
					nextMaskMix = vget_high_u8(match2NlDotDMasked);
					nextMaskMix = vreinterpret_u8_u64(vshr_n_u64(vreinterpret_u64_u8(nextMaskMix), 48+6));
				} else if(searchEnd) {
					uint8x16_t match3EqYA = vandq_u8(match2EqA, vceqq_u8(vextq_u8(dataA, dataB, 3), vdupq_n_u8('y')));
					uint8x16_t match3EqYB = vandq_u8(match2EqB, vceqq_u8(vextq_u8(dataB, dataC, 3), vdupq_n_u8('y')));
					uint8x16_t match3EqYC = vandq_u8(match2EqC, vceqq_u8(vextq_u8(dataC, dataD, 3), vdupq_n_u8('y')));
					uint8x16_t match3EqYD = vandq_u8(match2EqD, vceqq_u8(vextq_u8(dataD, nextData, 3), vdupq_n_u8('y')));
					if(LIKELIHOOD(0.001, neon_vect_is_nonzero(vorrq_u8(
						vorrq_u8(match3EqYA, match3EqYB),
						vorrq_u8(match3EqYC, match3EqYD)
					)))) {
						uint8x16_t match1LfA = vceqq_u8(vextq_u8(dataA, dataB, 1), vdupq_n_u8('\n'));
						uint8x16_t match1LfB = vceqq_u8(vextq_u8(dataB, dataC, 1), vdupq_n_u8('\n'));
						uint8x16_t match1LfC = vceqq_u8(vextq_u8(dataC, dataD, 1), vdupq_n_u8('\n'));
						uint8x16_t match1LfD = vceqq_u8(vextq_u8(dataD, nextData, 1), vdupq_n_u8('\n'));
						uint8x16_t matchEnd = vorrq_u8(
							vorrq_u8(
								vandq_u8(match3EqYA, vandq_u8(match1LfA, cmpCrA)),
								vandq_u8(match3EqYB, vandq_u8(match1LfB, cmpCrB))
							),
							vorrq_u8(
								vandq_u8(match3EqYC, vandq_u8(match1LfC, cmpCrC)),
								vandq_u8(match3EqYD, vandq_u8(match1LfD, cmpCrD))
							)
						);
						if(LIKELIHOOD(0.001, neon_vect_is_nonzero(matchEnd))) {
							len += i;
							break;
						}
					}
					if(isRaw)
						nextMaskMix = vdup_n_u8(0);
				} else if(isRaw) // no \r_. found
					nextMaskMix = vdup_n_u8(0);
			}
			
			// a spec compliant encoder should never generate sequences: ==, =\n and =\r, but we'll handle them to be spec compliant
			// the yEnc specification requires any character following = to be unescaped, not skipped over, so we'll deal with that
			// firstly, check for invalid sequences of = (we assume that these are rare, as a spec compliant yEnc encoder should not generate these)
			if(LIKELIHOOD(0.0001, (mask & ((maskEq << 1) | escFirst)) != 0)) {
				uint8_t tmp = eqFixLUT[(maskEq&0xff) & ~escFirst];
				uint64_t maskEq2 = tmp;
				for(int j=8; j<64; j+=8) {
					tmp = eqFixLUT[((maskEq>>j)&0xff) & ~(tmp>>7)];
					maskEq2 |= ((uint64_t)tmp)<<j;
				}
				maskEq = maskEq2;
				
				// next, eliminate anything following a `=` from the special char mask; this eliminates cases of `=\r` so that they aren't removed
				maskEq = (maskEq<<1) | escFirst;
				mask &= ~maskEq;
				cmpCombined = vreinterpretq_u8_u64(vcombine_u64(vmov_n_u64(mask), vdup_n_u64(0)));
				escFirst = tmp>>7;
				
				// unescape chars following `=`
				uint8x8_t maskEqTemp = vreinterpret_u8_u64(vmov_n_u64(maskEq));
				uint8x16_t vMaskEqA = vqtbl1q_u8(
					vcombine_u8(maskEqTemp, vdup_n_u8(0)),
					(uint8x16_t){0,0,0,0,0,0,0,0, 1,1,1,1,1,1,1,1}
				);
				maskEqTemp = vext_u8(maskEqTemp, maskEqTemp, 2);
				uint8x16_t vMaskEqB = vqtbl1q_u8(
					vcombine_u8(maskEqTemp, vdup_n_u8(0)),
					(uint8x16_t){0,0,0,0,0,0,0,0, 1,1,1,1,1,1,1,1}
				);
				maskEqTemp = vext_u8(maskEqTemp, maskEqTemp, 2);
				uint8x16_t vMaskEqC = vqtbl1q_u8(
					vcombine_u8(maskEqTemp, vdup_n_u8(0)),
					(uint8x16_t){0,0,0,0,0,0,0,0, 1,1,1,1,1,1,1,1}
				);
				maskEqTemp = vext_u8(maskEqTemp, maskEqTemp, 2);
				uint8x16_t vMaskEqD = vqtbl1q_u8(
					vcombine_u8(maskEqTemp, vdup_n_u8(0)),
					(uint8x16_t){0,0,0,0,0,0,0,0, 1,1,1,1,1,1,1,1}
				);
				vMaskEqA = vtstq_u8(vMaskEqA, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
				vMaskEqB = vtstq_u8(vMaskEqB, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
				vMaskEqC = vtstq_u8(vMaskEqC, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
				vMaskEqD = vtstq_u8(vMaskEqD, (uint8x16_t){1,2,4,8,16,32,64,128, 1,2,4,8,16,32,64,128});
				
				dataA = vsubq_u8(
					dataA,
					vbslq_u8(vMaskEqA, vdupq_n_u8(64+42), vdupq_n_u8(42))
				);
				dataB = vsubq_u8(
					dataB,
					vbslq_u8(vMaskEqB, vdupq_n_u8(64+42), vdupq_n_u8(42))
				);
				dataC = vsubq_u8(
					dataC,
					vbslq_u8(vMaskEqC, vdupq_n_u8(64+42), vdupq_n_u8(42))
				);
				dataD = vsubq_u8(
					dataD,
					vbslq_u8(vMaskEqD, vdupq_n_u8(64+42), vdupq_n_u8(42))
				);
			} else {
				// no invalid = sequences found - we can cut out some things from above
				// this code path is a shortened version of above; it's here because it's faster, and what we'll be dealing with most of the time
				escFirst = (maskEq >> 63);
				
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
				dataC = vsubq_u8(
					dataC,
					vbslq_u8(
						vextq_u8(cmpEqB, cmpEqC, 15),
						vdupq_n_u8(64+42),
						vdupq_n_u8(42)
					)
				);
				dataD = vsubq_u8(
					dataD,
					vbslq_u8(
						vextq_u8(cmpEqC, cmpEqD, 15),
						vdupq_n_u8(64+42),
						vdupq_n_u8(42)
					)
				);
			}
			yencOffset[0] = (escFirst << 6) | 42;
			
			// all that's left is to 'compress' the data (skip over masked chars)
			uint64_t counts = vget_lane_u64(vreinterpret_u64_u8(vcnt_u8(vget_low_u8(cmpCombined))), 0);
			counts = 0x0808080808080808ULL - counts;
			counts += counts>>8;
			
			vst1q_u8(p, vqtbl1q_u8(
				dataA,
				vld1q_u8((uint8_t*)(compactLUT + (mask&0x7fff)))
			));
			p += counts & 0xff;
			mask >>= 16;
			vst1q_u8(p, vqtbl1q_u8(
				dataB,
				vld1q_u8((uint8_t*)(compactLUT + (mask&0x7fff)))
			));
			p += (counts>>16) & 0xff;
			mask >>= 16;
			vst1q_u8(p, vqtbl1q_u8(
				dataC,
				vld1q_u8((uint8_t*)(compactLUT + (mask&0x7fff)))
			));
			p += (counts>>32) & 0xff;
			mask >>= 16;
			vst1q_u8(p, vqtbl1q_u8(
				dataD,
				vld1q_u8((uint8_t*)(compactLUT + (mask&0x7fff)))
			));
			p += (counts>>48) & 0xff;
		} else {
			dataA = vsubq_u8(dataA, yencOffset);
			dataB = vsubq_u8(dataB, vdupq_n_u8(42));
			dataC = vsubq_u8(dataC, vdupq_n_u8(42));
			dataD = vsubq_u8(dataD, vdupq_n_u8(42));
			vst1q_u8(p, dataA);
			vst1q_u8(p+sizeof(uint8x16_t), dataB);
			vst1q_u8(p+sizeof(uint8x16_t)*2, dataC);
			vst1q_u8(p+sizeof(uint8x16_t)*3, dataD);
			p += sizeof(uint8x16_t)*4;
			escFirst = 0;
			yencOffset = vdupq_n_u8(42);
		}
	}
	nextMask = vget_lane_u16(vreinterpret_u16_u8(nextMaskMix), 0);
	nextMask += nextMask >> 8;
	nextMask &= 3;
}

void decoder_set_neon_funcs() {
	decoder_init_lut(eqFixLUT, compactLUT);
	_do_decode = &do_decode_simd<false, false, sizeof(uint8x16_t)*4, do_decode_neon<false, false> >;
	_do_decode_raw = &do_decode_simd<true, false, sizeof(uint8x16_t)*4, do_decode_neon<true, false> >;
	_do_decode_end = &do_decode_simd<false, true, sizeof(uint8x16_t)*4, do_decode_neon<false, true> >;
	_do_decode_end_raw = &do_decode_simd<true, true, sizeof(uint8x16_t)*4, do_decode_neon<true, true> >;
}
#else
void decoder_set_neon_funcs() {}
#endif
