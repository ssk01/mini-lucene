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

    uint8_t EncodeNorm(float norm) const {
        if (norm >= 1.0f) return 255;
        if (norm <= 0.0f) return 0;
        return static_cast<uint8_t>(255.0f * norm);
    }
};

}  // namespace search
}  // namespace minilucene
