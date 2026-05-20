#include "minilucene/search/date_filter.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/index/term_docs.h"
#include "minilucene/index/term_enum.h"
#include "minilucene/util/bit_vector.h"

namespace minilucene {
namespace search {

DateFilter::DateFilter(const std::string& field, const std::string& start, const std::string& end)
    : field_(field), start_(start), end_(end) {}

util::BitVector DateFilter::Bits(index::IndexReader& reader) {
    util::BitVector bits(reader.MaxDoc());
    auto terms = reader.Terms();
    if (!terms) return bits;

    while (terms->Next()) {
        auto& t = terms->Current();
        if (t.Text() < start_) continue;
        if (t.Text() > end_) continue;

        auto docs = reader.Docs(t);
        if (!docs) continue;
        while (docs->Next()) {
            bits.Set(docs->Doc());
        }
    }
    terms->Close();
    return bits;
}

}  // namespace search
}  // namespace minilucene
