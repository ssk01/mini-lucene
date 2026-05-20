#pragma once

#include "minilucene/search/filter.h"

#include <string>

namespace minilucene {
namespace index {
class IndexReader;
}

namespace search {

class DateFilter : public Filter {
public:
    DateFilter(const std::string& field, const std::string& start, const std::string& end);
    util::BitVector Bits(index::IndexReader& reader) override;

private:
    std::string field_;
    std::string start_;
    std::string end_;
};

}  // namespace search
}  // namespace minilucene
