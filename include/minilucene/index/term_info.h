#pragma once

#include <cstdint>

namespace minilucene {
namespace index {

struct TermInfo {
    int doc_freq = 0;
    int64_t freq_pointer = 0;
    int64_t prox_pointer = 0;
};

}  // namespace index
}  // namespace minilucene
