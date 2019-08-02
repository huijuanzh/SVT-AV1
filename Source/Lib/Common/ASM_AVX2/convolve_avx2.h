/*
 * Copyright (c) 2018, Alliance for Open Media. All rights reserved
 *
 * This source code is subject to the terms of the BSD 2 Clause License and
 * the Alliance for Open Media Patent License 1.0. If the BSD 2 Clause License
 * was not distributed with this source code in the LICENSE file, you can
 * obtain it at www.aomedia.org/license/software. If the Alliance for Open
 * Media Patent License 1.0 was not distributed with this source code in the
 * PATENTS file, you can obtain it at www.aomedia.org/license/patent.
 */

#ifndef AOM_DSP_X86_CONVOLVE_AVX2_H_
#define AOM_DSP_X86_CONVOLVE_AVX2_H_

#include "convolve.h"
#include "EbInterPrediction.h"

 // filters for 16
DECLARE_ALIGNED(32, static const uint8_t, filt1_global_avx2[32]) = {
  0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8,
  0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8
};

DECLARE_ALIGNED(32, static const uint8_t, filt2_global_avx2[32]) = {
  2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10,
  2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10
};

DECLARE_ALIGNED(32, static const uint8_t, filt3_global_avx2[32]) = {
  4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12,
  4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12
};

DECLARE_ALIGNED(32, static const uint8_t, filt4_global_avx2[32]) = {
  6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14,
  6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14
};

static INLINE EbBool is_convolve_2tap(const int16_t *const filter) {
    return (EbBool)((InterpKernel *)filter == bilinear_filters);
}

static INLINE EbBool is_convolve_4tap(const int16_t *const filter) {
    return (EbBool)(((InterpKernel *)filter == sub_pel_filters_4) ||
        ((InterpKernel *)filter == sub_pel_filters_4smooth));
}

static INLINE int32_t get_convolve_tap(const int16_t *const filter) {
    if (is_convolve_2tap(filter)) return 2;
    else if (is_convolve_4tap(filter)) return 4;
    else return 8;
}

static INLINE void prepare_coeffs_lowbd(
    const InterpFilterParams *const filter_params, const int32_t subpel_q4,
    __m256i *const coeffs /* [4] */) {
    const int16_t *const filter = av1_get_interp_filter_subpel_kernel(
        *filter_params, subpel_q4 & SUBPEL_MASK);
    const __m128i coeffs_8 = _mm_loadu_si128((__m128i *)filter);
    const __m256i filter_coeffs = _mm256_broadcastsi128_si256(coeffs_8);

    // right shift all filter co-efficients by 1 to reduce the bits required.
    // This extra right shift will be taken care of at the end while rounding
    // the result.
    // Since all filter co-efficients are even, this change will not affect the
    // end result
    assert(_mm_test_all_zeros(_mm_and_si128(coeffs_8, _mm_set1_epi16(1)),
        _mm_set1_epi16((short)0xffff)));

    const __m256i coeffs_1 = _mm256_srai_epi16(filter_coeffs, 1);

    // coeffs 0 1 0 1 0 1 0 1
    coeffs[0] = _mm256_shuffle_epi8(coeffs_1, _mm256_set1_epi16(0x0200u));
    // coeffs 2 3 2 3 2 3 2 3
    coeffs[1] = _mm256_shuffle_epi8(coeffs_1, _mm256_set1_epi16(0x0604u));
    // coeffs 4 5 4 5 4 5 4 5
    coeffs[2] = _mm256_shuffle_epi8(coeffs_1, _mm256_set1_epi16(0x0a08u));
    // coeffs 6 7 6 7 6 7 6 7
    coeffs[3] = _mm256_shuffle_epi8(coeffs_1, _mm256_set1_epi16(0x0e0cu));
}

static INLINE void prepare_coeffs_lowbd_2tap_ssse3(
    const InterpFilterParams *const filter_params, const int32_t subpel_q4,
    __m128i *const coeffs /* [4] */) {
    const int16_t *const filter = av1_get_interp_filter_subpel_kernel(
        *filter_params, subpel_q4 & SUBPEL_MASK);
    const __m128i coeffs_8 = _mm_cvtsi32_si128(*(const int32_t *)(filter + 3));

    // right shift all filter co-efficients by 1 to reduce the bits required.
    // This extra right shift will be taken care of at the end while rounding
    // the result.
    // Since all filter co-efficients are even, this change will not affect the
    // end result
    assert(_mm_test_all_zeros(_mm_and_si128(coeffs_8, _mm_set1_epi16(1)),
        _mm_set1_epi16((short)0xffff)));

    const __m128i coeffs_1 = _mm_srai_epi16(coeffs_8, 1);

    // coeffs 3 4 3 4 3 4 3 4
    *coeffs = _mm_shuffle_epi8(coeffs_1, _mm_set1_epi16(0x0200u));
}

static INLINE void prepare_coeffs_lowbd_2tap_avx2(
    const InterpFilterParams *const filter_params, const int32_t subpel_q4,
    __m256i *const coeffs /* [4] */) {
    const int16_t *const filter = av1_get_interp_filter_subpel_kernel(
        *filter_params, subpel_q4 & SUBPEL_MASK);
    const __m128i coeffs_8 = _mm_loadu_si128((__m128i *)filter);
    const __m256i filter_coeffs = _mm256_broadcastsi128_si256(coeffs_8);

    // right shift all filter co-efficients by 1 to reduce the bits required.
    // This extra right shift will be taken care of at the end while rounding
    // the result.
    // Since all filter co-efficients are even, this change will not affect the
    // end result
    assert(_mm_test_all_zeros(_mm_and_si128(coeffs_8, _mm_set1_epi16(1)),
        _mm_set1_epi16((short)0xffff)));

    const __m256i coeffs_1 = _mm256_srai_epi16(filter_coeffs, 1);

    // coeffs 3 4 3 4 3 4 3 4
    *coeffs = _mm256_shuffle_epi8(coeffs_1, _mm256_set1_epi16(0x0806u));
}

static INLINE void prepare_coeffs(const InterpFilterParams *const filter_params,
    const int32_t subpel_q4,
    __m256i *const coeffs /* [4] */) {
    const int16_t *filter = av1_get_interp_filter_subpel_kernel(
        *filter_params, subpel_q4 & SUBPEL_MASK);

    const __m128i coeff_8 = _mm_loadu_si128((__m128i *)filter);
    const __m256i coeff = _mm256_broadcastsi128_si256(coeff_8);

    // coeffs 0 1 0 1 0 1 0 1
    coeffs[0] = _mm256_shuffle_epi32(coeff, 0x00);
    // coeffs 2 3 2 3 2 3 2 3
    coeffs[1] = _mm256_shuffle_epi32(coeff, 0x55);
    // coeffs 4 5 4 5 4 5 4 5
    coeffs[2] = _mm256_shuffle_epi32(coeff, 0xaa);
    // coeffs 6 7 6 7 6 7 6 7
    coeffs[3] = _mm256_shuffle_epi32(coeff, 0xff);
}

static INLINE void prepare_coeffs_2tap(const InterpFilterParams *const filter_params,
    const int32_t subpel_q4,
    __m256i *const coeffs /* [4] */) {
    const int16_t *filter = av1_get_interp_filter_subpel_kernel(
        *filter_params, subpel_q4 & SUBPEL_MASK);

    const __m128i coeff_8 = _mm_loadu_si128((__m128i *)(filter + 3));
    const __m256i coeff = _mm256_broadcastsi128_si256(coeff_8);

    // coeffs 3 4 3 4 3 4 3 4
    coeffs[0] = _mm256_shuffle_epi32(coeff, 0x00);
}

static INLINE __m256i convolve_lowbd(const __m256i *const s,
    const __m256i *const coeffs) {
    const __m256i res_01 = _mm256_maddubs_epi16(s[0], coeffs[0]);
    const __m256i res_23 = _mm256_maddubs_epi16(s[1], coeffs[1]);
    const __m256i res_45 = _mm256_maddubs_epi16(s[2], coeffs[2]);
    const __m256i res_67 = _mm256_maddubs_epi16(s[3], coeffs[3]);

    // order: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
    const __m256i res = _mm256_add_epi16(_mm256_add_epi16(res_01, res_45),
        _mm256_add_epi16(res_23, res_67));

    return res;
}

static INLINE __m256i convolve_lowbd_4tap(const __m256i *const s,
    const __m256i *const coeffs) {
    const __m256i res_23 = _mm256_maddubs_epi16(s[0], coeffs[0]);
    const __m256i res_45 = _mm256_maddubs_epi16(s[1], coeffs[1]);

    // order: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
    const __m256i res = _mm256_add_epi16(res_45, res_23);

    return res;
}

static INLINE __m128i convolve_lowbd_2tap_ssse3(const __m128i *const s,
    const __m128i *const coeffs) {
    return _mm_maddubs_epi16(s[0], coeffs[0]);
}

static INLINE __m256i convolve_lowbd_2tap(const __m256i *const s,
    const __m256i *const coeffs) {
    return _mm256_maddubs_epi16(s[0], coeffs[0]);
}

static INLINE __m256i convolve(const __m256i *const s,
    const __m256i *const coeffs) {
    const __m256i res_0 = _mm256_madd_epi16(s[0], coeffs[0]);
    const __m256i res_1 = _mm256_madd_epi16(s[1], coeffs[1]);
    const __m256i res_2 = _mm256_madd_epi16(s[2], coeffs[2]);
    const __m256i res_3 = _mm256_madd_epi16(s[3], coeffs[3]);

    const __m256i res = _mm256_add_epi32(_mm256_add_epi32(res_0, res_1),
        _mm256_add_epi32(res_2, res_3));

    return res;
}

static INLINE __m256i convolve_4tap(const __m256i *const s,
    const __m256i *const coeffs) {
    const __m256i res_1 = _mm256_madd_epi16(s[0], coeffs[0]);
    const __m256i res_2 = _mm256_madd_epi16(s[1], coeffs[1]);

    const __m256i res = _mm256_add_epi32(res_1, res_2);
    return res;
}

static INLINE __m256i convolve_2tap(const __m256i *const s,
    const __m256i *const coeffs) {
    return _mm256_madd_epi16(s[0], coeffs[0]);
}

static INLINE __m256i convolve_lowbd_x(const __m256i data,
    const __m256i *const coeffs,
    const __m256i *const filt) {
    __m256i s[4];

    s[0] = _mm256_shuffle_epi8(data, filt[0]);
    s[1] = _mm256_shuffle_epi8(data, filt[1]);
    s[2] = _mm256_shuffle_epi8(data, filt[2]);
    s[3] = _mm256_shuffle_epi8(data, filt[3]);

    return convolve_lowbd(s, coeffs);
}

static INLINE __m256i convolve_lowbd_x_4tap(const __m256i data,
    const __m256i *const coeffs,
    const __m256i *const filt) {
    __m256i s[2];

    s[0] = _mm256_shuffle_epi8(data, filt[0]);
    s[1] = _mm256_shuffle_epi8(data, filt[1]);

    return convolve_lowbd_4tap(s, coeffs);
}

static INLINE __m256i convolve_lowbd_x_2tap(const __m256i data,
    const __m256i *const coeffs,
    const __m256i *const filt) {
    const __m256i s = _mm256_shuffle_epi8(data, filt[0]);

    return convolve_lowbd_2tap(&s, coeffs);
}

static INLINE __m128i convolve_round_sse2(const __m128i src) {
    __m128i dst;
    const __m128i round = _mm_set1_epi16(34);
    dst = _mm_add_epi16(src, round);
    dst = _mm_srai_epi16(dst, 6);
    return dst;
}

static INLINE __m256i convolve_round_avx2(const __m256i src) {
    __m256i dst;
    const __m256i round = _mm256_set1_epi16(34);
    dst = _mm256_add_epi16(src, round);
    dst = _mm256_srai_epi16(dst, 6);
    return dst;
}

static INLINE __m128i convolve_lowbd_x_32_2tap_kernel_ssse3(const __m128i src,
    const __m128i *const coeffs) {
    __m128i res_16b;

    res_16b = convolve_lowbd_2tap_ssse3(&src, coeffs);
    res_16b = convolve_round_sse2(res_16b);
    return _mm_packus_epi16(res_16b, res_16b);
}

static INLINE __m256i convolve_lowbd_x_32_2tap_kernel_avx2(const __m256i src[2],
    const __m256i *const coeffs) {
    const __m256i s0 = _mm256_unpacklo_epi8(src[0], src[1]);
    const __m256i s1 = _mm256_unpackhi_epi8(src[0], src[1]);
    __m256i res_16b[2];

    res_16b[0] = convolve_lowbd_2tap(&s0, coeffs);
    res_16b[1] = convolve_lowbd_2tap(&s1, coeffs);
    res_16b[0] = convolve_round_avx2(res_16b[0]);
    res_16b[1] = convolve_round_avx2(res_16b[1]);
    return _mm256_packus_epi16(res_16b[0], res_16b[1]);
}

static INLINE void convolve_lowbd_x_32_2tap(const uint8_t *const src,
    const __m256i *const coeffs, uint8_t *const dst) {
    __m256i s[2];

    s[0] = _mm256_loadu_si256((__m256i *)src);
    s[1] = _mm256_loadu_si256((__m256i *)(src + 1));
    const __m256i d = convolve_lowbd_x_32_2tap_kernel_avx2(s, coeffs);
    _mm256_storeu_si256((__m256i *)dst, d);
}

static INLINE void convolve_lowbd_x_32_2tap_avg(const uint8_t *const src,
    const __m256i *const coeffs, uint8_t *const dst) {
    __m256i s[2];

    s[0] = _mm256_loadu_si256((__m256i *)src);
    s[1] = _mm256_loadu_si256((__m256i *)(src + 1));
    const __m256i d = _mm256_avg_epu8(s[0], s[1]);
    _mm256_storeu_si256((__m256i *)dst, d);
}

static INLINE void add_store_aligned_256(ConvBufType *const dst,
    const __m256i *const res,
    const int32_t do_average) {
    __m256i d;
    if (do_average) {
        d = _mm256_load_si256((__m256i *)dst);
        d = _mm256_add_epi32(d, *res);
        d = _mm256_srai_epi32(d, 1);
    }
    else
        d = *res;
    _mm256_storeu_si256((__m256i *)dst, d);
}

static INLINE __m256i comp_avg(const __m256i *const data_ref_0,
    const __m256i *const res_unsigned,
    const __m256i *const wt,
    const int32_t use_jnt_comp_avg) {
    __m256i res;
    if (use_jnt_comp_avg) {
        const __m256i data_lo = _mm256_unpacklo_epi16(*data_ref_0, *res_unsigned);
        const __m256i data_hi = _mm256_unpackhi_epi16(*data_ref_0, *res_unsigned);

        const __m256i wt_res_lo = _mm256_madd_epi16(data_lo, *wt);
        const __m256i wt_res_hi = _mm256_madd_epi16(data_hi, *wt);

        const __m256i res_lo = _mm256_srai_epi32(wt_res_lo, DIST_PRECISION_BITS);
        const __m256i res_hi = _mm256_srai_epi32(wt_res_hi, DIST_PRECISION_BITS);

        res = _mm256_packs_epi32(res_lo, res_hi);
    }
    else {
        const __m256i wt_res = _mm256_add_epi16(*data_ref_0, *res_unsigned);
        res = _mm256_srai_epi16(wt_res, 1);
    }
    return res;
}

static INLINE __m256i convolve_rounding(const __m256i *const res_unsigned,
    const __m256i *const offset_const,
    const __m256i *const round_const,
    const int32_t round_shift) {
    const __m256i res_signed = _mm256_sub_epi16(*res_unsigned, *offset_const);
    const __m256i res_round = _mm256_srai_epi16(
        _mm256_add_epi16(res_signed, *round_const), round_shift);
    return res_round;
}

static INLINE __m256i highbd_comp_avg(const __m256i *const data_ref_0,
    const __m256i *const res_unsigned,
    const __m256i *const wt0,
    const __m256i *const wt1,
    const int32_t use_jnt_comp_avg) {
    __m256i res;
    if (use_jnt_comp_avg) {
        const __m256i wt0_res = _mm256_mullo_epi32(*data_ref_0, *wt0);
        const __m256i wt1_res = _mm256_mullo_epi32(*res_unsigned, *wt1);
        const __m256i wt_res = _mm256_add_epi32(wt0_res, wt1_res);
        res = _mm256_srai_epi32(wt_res, DIST_PRECISION_BITS);
    }
    else {
        const __m256i wt_res = _mm256_add_epi32(*data_ref_0, *res_unsigned);
        res = _mm256_srai_epi32(wt_res, 1);
    }
    return res;
}

static INLINE __m256i highbd_convolve_rounding(
    const __m256i *const res_unsigned, const __m256i *const offset_const,
    const __m256i *const round_const, const int32_t round_shift) {
    const __m256i res_signed = _mm256_sub_epi32(*res_unsigned, *offset_const);
    const __m256i res_round = _mm256_srai_epi32(
        _mm256_add_epi32(res_signed, *round_const), round_shift);

    return res_round;
}

#define CONVOLVE_SR_HORIZONTAL_FILTER_2TAP                                     \
    for (i = 0; i < (im_h - 2); i += 2) {                                      \
        __m256i data = _mm256_castsi128_si256(                                 \
            _mm_loadu_si128((__m128i *)&src_ptr[(i * src_stride) + j]));       \
                                                                               \
        data = _mm256_inserti128_si256(                                        \
            data,                                                              \
            _mm_loadu_si128(                                                   \
            (__m128i *)&src_ptr[(i * src_stride) + j + src_stride]),           \
            1);                                                                \
        __m256i res = convolve_lowbd_x_2tap(data, coeffs_h, filt);             \
                                                                               \
        res = _mm256_sra_epi16(_mm256_add_epi16(res, round_const_h),           \
            round_shift_h);                                                    \
        _mm256_store_si256((__m256i *)&im_block[i * im_stride], res);          \
    }                                                                          \
                                                                               \
    __m256i data_1 = _mm256_castsi128_si256(                                   \
        _mm_loadu_si128((__m128i *)&src_ptr[(i * src_stride) + j]));           \
                                                                               \
    __m256i res = convolve_lowbd_x_2tap(data_1, coeffs_h, filt);               \
    res =                                                                      \
        _mm256_sra_epi16(_mm256_add_epi16(res, round_const_h), round_shift_h); \
    _mm256_store_si256((__m256i *)&im_block[i * im_stride], res);

#define CONVOLVE_SR_HORIZONTAL_FILTER_4TAP                                     \
    for (i = 0; i < (im_h - 2); i += 2) {                                      \
        __m256i data = _mm256_castsi128_si256(                                 \
            _mm_loadu_si128((__m128i *)&src_ptr[(i * src_stride) + j]));       \
                                                                               \
        data = _mm256_inserti128_si256(                                        \
            data,                                                              \
            _mm_loadu_si128(                                                   \
            (__m128i *)&src_ptr[(i * src_stride) + j + src_stride]),           \
            1);                                                                \
        __m256i res = convolve_lowbd_x_4tap(data, coeffs_h + 1, filt);         \
                                                                               \
        res = _mm256_sra_epi16(_mm256_add_epi16(res, round_const_h),           \
            round_shift_h);                                                    \
        _mm256_store_si256((__m256i *)&im_block[i * im_stride], res);          \
    }                                                                          \
                                                                               \
    __m256i data_1 = _mm256_castsi128_si256(                                   \
        _mm_loadu_si128((__m128i *)&src_ptr[(i * src_stride) + j]));           \
                                                                               \
    __m256i res = convolve_lowbd_x_4tap(data_1, coeffs_h + 1, filt);           \
    res =                                                                      \
        _mm256_sra_epi16(_mm256_add_epi16(res, round_const_h), round_shift_h); \
    _mm256_store_si256((__m256i *)&im_block[i * im_stride], res);

#define CONVOLVE_SR_HORIZONTAL_FILTER_8TAP                                     \
  for (i = 0; i < (im_h - 2); i += 2) {                                        \
    __m256i data = _mm256_castsi128_si256(                                     \
        _mm_loadu_si128((__m128i *)&src_ptr[(i * src_stride) + j]));           \
    data = _mm256_inserti128_si256(                                            \
        data,                                                                  \
        _mm_loadu_si128(                                                       \
            (__m128i *)&src_ptr[(i * src_stride) + j + src_stride]),           \
        1);                                                                    \
                                                                               \
    __m256i res = convolve_lowbd_x(data, coeffs_h, filt);                      \
    res =                                                                      \
        _mm256_sra_epi16(_mm256_add_epi16(res, round_const_h), round_shift_h); \
    _mm256_store_si256((__m256i *)&im_block[i * im_stride], res);              \
  }                                                                            \
                                                                               \
  __m256i data_1 = _mm256_castsi128_si256(                                     \
      _mm_loadu_si128((__m128i *)&src_ptr[(i * src_stride) + j]));             \
                                                                               \
  __m256i res = convolve_lowbd_x(data_1, coeffs_h, filt);                      \
                                                                               \
  res = _mm256_sra_epi16(_mm256_add_epi16(res, round_const_h), round_shift_h); \
                                                                               \
  _mm256_store_si256((__m256i *)&im_block[i * im_stride], res);

#endif
