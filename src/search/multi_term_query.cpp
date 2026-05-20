#include "minilucene/search/multi_term_query.h"

namespace minilucene {
namespace search {

MultiTermQuery::MultiTermQuery(const index::Term& term) : term_(term) {}

std::unique_ptr<Scorer> MultiTermQuery::CreateScorer(index::IndexReader& reader) const {
    return nullptr;
}

std::string MultiTermQuery::ToString() const { return term_.Text(); }

}  // namespace search
}  // namespace minilucene
