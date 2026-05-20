#include "minilucene/search/fuzzy_term_enum.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/term_enum.h"

namespace minilucene {
namespace search {

FuzzyTermEnum::FuzzyTermEnum(index::IndexReader& reader, const index::Term& term, int max_edits)
    : search_term_(term), max_edits_(max_edits) {
    auto terms = reader.Terms();
    if (terms) SetEnum(std::move(terms));
}

bool FuzzyTermEnum::TermMatch(const index::Term& term) {
    return EditDistance(term.Text(), search_term_.Text()) <= max_edits_;
}

int FuzzyTermEnum::EditDistance(const std::string& a, const std::string& b) {
    return 999;
}

}  // namespace search
}  // namespace minilucene
