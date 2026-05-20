#pragma once

#include "minilucene/util/bit_vector.h"

namespace minilucene {
namespace index {
class IndexReader;
}

namespace search {

class Filter {
public:
    virtual ~Filter() = default;
    virtual util::BitVector Bits(index::IndexReader& reader) = 0;
};

}  // namespace search
}  // namespace minilucene
