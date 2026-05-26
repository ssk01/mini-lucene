#pragma once

#include <cstdint>
#include <cmath>

namespace minilucene {
namespace search {

class Similarity {
public:
    Similarity() = default;
    virtual ~Similarity() = default;

    virtual float Tf(int freq) const {
        return std::sqrt(static_cast<float>(freq));
    }

    virtual float Idf(int doc_freq, int max_doc) const {
        return std::log(static_cast<float>(max_doc) / (doc_freq + 1)) + 1.0f;
    }

    virtual float DecodeNorm(uint8_t b) const {
        return static_cast<float>(b) / 255.0f;
    }

    virtual float Coord(int overlap, int max_overlap) const {
        return static_cast<float>(overlap) / max_overlap;
    }

    float LengthNorm(int num_tokens) const {
        return (num_tokens > 0) ? (1.0f / std::sqrt(static_cast<float>(num_tokens))) : 0.0f;
    }

    // Encode a unit-interval norm via the legacy linear scheme. Used only
    // when callers already hold a float they want stored verbatim. New
    // index-time code should call EncodeLengthNorm(num_tokens) instead so
    // it matches Java Lucene 1.0.1 byte-for-byte.
    uint8_t EncodeNorm(float norm) const {
        if (norm >= 1.0f) return 255;
        if (norm <= 0.0f) return 0;
        return static_cast<uint8_t>(255.0f * norm);
    }

    // Byte-compatible with Java Lucene 1.0.1 Similarity.norm(int numTerms):
    //   norm_byte = ceil(255.0 / sqrt(numTerms))
    // The ceil + non-zero floor guards against very long docs collapsing
    // to byte 0 (which the legacy floor-then-truncate path did for any
    // numTerms > ~1000, silently making long docs unscoreable).
    uint8_t EncodeLengthNorm(int num_tokens) const {
        if (num_tokens <= 0) return 0;  // explicit zero for empty/deleted
        double v = std::ceil(255.0 / std::sqrt(static_cast<double>(num_tokens)));
        if (v < 1.0)   v = 1.0;    // Java's guard: never zero for non-empty
        if (v > 255.0) v = 255.0;
        return static_cast<uint8_t>(v);
    }
};

}  // namespace search
}  // namespace minilucene
