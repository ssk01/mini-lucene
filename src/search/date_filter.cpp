#include "minilucene/search/date_filter.h"
#include "minilucene/index/index_reader.h"

namespace minilucene {
namespace search {

DateFilter::DateFilter(const std::string& field, const std::string& start, const std::string& end)
    : field_(field), start_(start), end_(end) {}

util::BitVector DateFilter::Bits(index::IndexReader& reader) {
    // STUB: returns no bits set
    return util::BitVector(reader.MaxDoc());
}

}  // namespace search
}  // namespace minilucene
