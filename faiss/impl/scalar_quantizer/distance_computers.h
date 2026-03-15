/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstring>
#include <vector>

#if defined(__AVX512BF16__)
#include <immintrin.h>
#endif

#if defined(__AVX512BF16__) && defined(FAISS_USE_AMX)
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <faiss/impl/ScalarQuantizer.h>
#include <faiss/impl/scalar_quantizer/quantizers.h>
#include <faiss/impl/scalar_quantizer/similarities.h>
#include <faiss/impl/simdlib/simdlib_dispatch.h>
#include <faiss/utils/bf16.h>
#include <faiss/utils/simd_levels.h>

namespace faiss {

namespace scalar_quantizer {

using SQDistanceComputer = ScalarQuantizer::SQDistanceComputer;

/*******************************************************************
 * DistanceComputer: combines a similarity and a quantizer to do
 * code-to-vector or code-to-code comparisons
 *******************************************************************/

template <class Quantizer, class Similarity, SIMDLevel SL>
struct DCTemplate : SQDistanceComputer {};

#if defined(__AVX512BF16__)

#if defined(FAISS_USE_AMX)

namespace detail {

constexpr unsigned long kArchGetXCompPerm = 0x1022;
constexpr unsigned long kArchReqXCompPerm = 0x1023;
constexpr unsigned long kXFeatureXTileData = 18;
constexpr unsigned long kXFeatureMaskXTileData = 1UL << kXFeatureXTileData;

static inline bool enable_amx_for_thread_once() {
#if defined(__AMX_TILE__) && defined(__AMX_BF16__) && defined(__linux__)
    static thread_local int state = 0;
    if (state != 0) {
        return state > 0;
    }

    unsigned long bitmask = 0;
    long status = syscall(
            SYS_arch_prctl, kArchGetXCompPerm, reinterpret_cast<unsigned long>(&bitmask));
    if (status == 0 && (bitmask & kXFeatureMaskXTileData)) {
        state = 1;
        return true;
    }

    status = syscall(SYS_arch_prctl, kArchReqXCompPerm, kXFeatureXTileData);
    if (status != 0) {
        state = -1;
        return false;
    }

    bitmask = 0;
    status = syscall(
            SYS_arch_prctl, kArchGetXCompPerm, reinterpret_cast<unsigned long>(&bitmask));
    state = (status == 0 && (bitmask & kXFeatureMaskXTileData)) ? 1 : -1;
    return state > 0;
#else
    return false;
#endif
}

#if defined(__AMX_TILE__) && defined(__AMX_BF16__)
static inline float dot_bf16_tail_scalar(
        const uint16_t* a,
        const uint16_t* b,
        uint32_t count) {
    float acc = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
        acc += decode_bf16(a[i]) * decode_bf16(b[i]);
    }
    return acc;
}

static inline int amx_bf16_ip_batch_x16_single_query(
        const uint16_t* library_bf16,
        const uint16_t* query_bf16,
        size_t dim,
        size_t row_count,
        float* out) {
    if (row_count == 0 || row_count > 16) {
        return -1;
    }
    if (!enable_amx_for_thread_once()) {
        return -1;
    }

    alignas(64) static thread_local unsigned char cfg[64];
    static thread_local int prev_rows = -1;
    const int rows = static_cast<int>(row_count);
    if (prev_rows != rows) {
        std::memset(cfg, 0, sizeof(cfg));
        cfg[0] = 1;

        cfg[16] = 64;
        cfg[48] = static_cast<unsigned char>(rows);

        cfg[18] = 4;
        cfg[49] = 16;

        cfg[20] = 4;
        cfg[50] = static_cast<unsigned char>(rows);

        cfg[22] = 64;
        cfg[51] = static_cast<unsigned char>(rows);
        cfg[24] = 4;
        cfg[52] = 16;

        cfg[26] = 64;
        cfg[53] = static_cast<unsigned char>(rows);
        cfg[28] = 4;
        cfg[54] = 16;

        _tile_loadconfig(cfg);
        prev_rows = rows;
    }

    constexpr int kBlock = 32;
    const size_t block_count = dim / kBlock;
    const size_t tail_count = dim % kBlock;
    const int library_stride_bytes = static_cast<int>(dim * sizeof(uint16_t));
    const char* library = reinterpret_cast<const char*>(library_bf16);
    const char* query = reinterpret_cast<const char*>(query_bf16);

    _tile_zero(2);

    size_t block = 0;
    for (; block + 2 < block_count; block += 3) {
        _tile_loadd(0, library + (block + 0) * kBlock * sizeof(uint16_t), library_stride_bytes);
        _tile_loadd(1, query + (block + 0) * kBlock * sizeof(uint16_t), 4);
        _tile_loadd(3, library + (block + 1) * kBlock * sizeof(uint16_t), library_stride_bytes);
        _tile_loadd(4, query + (block + 1) * kBlock * sizeof(uint16_t), 4);
        _tile_loadd(5, library + (block + 2) * kBlock * sizeof(uint16_t), library_stride_bytes);
        _tile_loadd(6, query + (block + 2) * kBlock * sizeof(uint16_t), 4);

        _tile_dpbf16ps(2, 0, 1);
        _tile_dpbf16ps(2, 3, 4);
        _tile_dpbf16ps(2, 5, 6);
    }

    for (; block < block_count; ++block) {
        _tile_loadd(0, library + block * kBlock * sizeof(uint16_t), library_stride_bytes);
        _tile_loadd(1, query + block * kBlock * sizeof(uint16_t), 4);
        _tile_dpbf16ps(2, 0, 1);
    }

    _tile_stored(2, out, 4);
    _tile_zero(2);

    if (tail_count != 0) {
        const size_t base = block_count * kBlock;
        for (size_t row = 0; row < row_count; ++row) {
            out[row] += dot_bf16_tail_scalar(
                    library_bf16 + row * dim + base,
                    query_bf16 + base,
                    static_cast<uint32_t>(tail_count));
        }
    }

    return 0;
}
#endif

} // namespace detail

#endif

// Fast path for QT_bf16 + IP on CPUs with AVX512_BF16.
template <SIMDLevel SL>
struct DCBF16IPDpbf16 : SQDistanceComputer {
    using Sim = SimilarityIP<SL>;

    QuantizerBF16<SL> quant;
    std::vector<uint16_t> qbf16;

    DCBF16IPDpbf16(size_t d, const std::vector<float>& trained)
            : quant(d, trained), qbf16(d) {}

    void set_query(const float* x) final {
        q = x;
        for (size_t i = 0; i < quant.d; ++i) {
            qbf16[i] = encode_bf16(x[i]);
        }
    }

    FAISS_ALWAYS_INLINE float compute_code_ip_bf16(
            const uint16_t* a,
            const uint16_t* b) const {
        __m512 acc = _mm512_setzero_ps();
        size_t i = 0;
        for (; i + 31 < quant.d; i += 32) {
            const __m512i va = _mm512_loadu_si512(reinterpret_cast<const void*>(a + i));
            const __m512i vb = _mm512_loadu_si512(reinterpret_cast<const void*>(b + i));
            acc = _mm512_dpbf16_ps(acc, (__m512bh)va, (__m512bh)vb);
        }

        float sum = _mm512_reduce_add_ps(acc);
        for (; i < quant.d; ++i) {
            sum += decode_bf16(a[i]) * decode_bf16(b[i]);
        }
        return sum;
    }

    float symmetric_dis(idx_t i, idx_t j) override {
        const auto* code1 = reinterpret_cast<const uint16_t*>(codes + i * code_size);
        const auto* code2 = reinterpret_cast<const uint16_t*>(codes + j * code_size);
        return compute_code_ip_bf16(code1, code2);
    }

    float query_to_code(const uint8_t* code) const final {
        return compute_code_ip_bf16(qbf16.data(), reinterpret_cast<const uint16_t*>(code));
    }
};

#if defined(FAISS_USE_AMX)

// AMX path for QT_bf16 + IP on Sapphire Rapids. Query-to-single-code keeps
// using AVX512_BF16; the AMX kernel is reserved for batch scoring.
template <SIMDLevel SL>
struct DCBF16IPAmx : DCBF16IPDpbf16<SL> {
    std::vector<uint16_t> packed_codes;

    DCBF16IPAmx(size_t d, const std::vector<float>& trained)
            : DCBF16IPDpbf16<SL>(d, trained), packed_codes(16 * d) {}

    void distances_batch_16(
            const idx_t* idx,
            size_t count,
            float* dis) override {
        if (count == 0) {
            return;
        }
#if defined(__AMX_TILE__) && defined(__AMX_BF16__)
        for (size_t i = 0; i < count; ++i) {
            const auto* src = reinterpret_cast<const uint16_t*>(
                    this->codes + idx[i] * this->code_size);
            std::memcpy(
                    packed_codes.data() + i * this->quant.d,
                    src,
                    this->quant.d * sizeof(uint16_t));
        }

        if (detail::amx_bf16_ip_batch_x16_single_query(
                    packed_codes.data(),
                    this->qbf16.data(),
                    this->quant.d,
                    count,
                    dis) == 0) {
            return;
        }
#endif
        for (size_t i = 0; i < count; ++i) {
            dis[i] = this->query_to_code(this->codes + idx[i] * this->code_size);
        }
    }
};

#endif

#endif

template <class Quantizer, class Similarity>
struct DCTemplate<Quantizer, Similarity, SIMDLevel::NONE> : SQDistanceComputer {
    using Sim = Similarity;

    Quantizer quant;

    DCTemplate(size_t d, const std::vector<float>& trained)
            : quant(d, trained) {}

    float compute_distance(const float* x, const uint8_t* code) const {
        Similarity sim(x);
        sim.begin();
        for (size_t i = 0; i < quant.d; i++) {
            float xi = quant.reconstruct_component(code, i);
            sim.add_component(xi);
        }
        return sim.result();
    }

    float compute_code_distance(const uint8_t* code1, const uint8_t* code2)
            const {
        Similarity sim(nullptr);
        sim.begin();
        for (size_t i = 0; i < quant.d; i++) {
            float x1 = quant.reconstruct_component(code1, i);
            float x2 = quant.reconstruct_component(code2, i);
            sim.add_component_2(x1, x2);
        }
        return sim.result();
    }

    void set_query(const float* x) final {
        q = x;
    }

    float symmetric_dis(idx_t i, idx_t j) override {
        return compute_code_distance(
                codes + i * code_size, codes + j * code_size);
    }

    float query_to_code(const uint8_t* code) const final {
        return compute_distance(q, code);
    }
};

/*******************************************************************
 * DistanceComputerByte: computes distances in the integer domain
 *******************************************************************/

template <class Similarity, SIMDLevel SL>
struct DistanceComputerByte : SQDistanceComputer {};

template <class Similarity>
struct DistanceComputerByte<Similarity, SIMDLevel::NONE> : SQDistanceComputer {
    using Sim = Similarity;

    int d;
    std::vector<uint8_t> tmp;

    DistanceComputerByte(int d, const std::vector<float>&) : d(d), tmp(d) {}

    int compute_code_distance(const uint8_t* code1, const uint8_t* code2)
            const {
        int accu = 0;
        for (int i = 0; i < d; i++) {
            if (Sim::metric_type == METRIC_INNER_PRODUCT) {
                accu += int(code1[i]) * code2[i];
            } else {
                int diff = int(code1[i]) - code2[i];
                accu += diff * diff;
            }
        }
        return accu;
    }

    void set_query(const float* x) final {
        for (int i = 0; i < d; i++) {
            tmp[i] = int(x[i]);
        }
    }

    int compute_distance(const float* x, const uint8_t* code) {
        set_query(x);
        return compute_code_distance(tmp.data(), code);
    }

    float symmetric_dis(idx_t i, idx_t j) override {
        return compute_code_distance(
                codes + i * code_size, codes + j * code_size);
    }

    float query_to_code(const uint8_t* code) const final {
        return compute_code_distance(tmp.data(), code);
    }
};

/*******************************************************************
 * Selection function
 *******************************************************************/

template <SIMDLevel SL>
SQDistanceComputer* sq_select_distance_computer(
        MetricType metric,
        ScalarQuantizer::QuantizerType qtype,
        size_t d,
        const std::vector<float>& trained);

} // namespace scalar_quantizer
} // namespace faiss
