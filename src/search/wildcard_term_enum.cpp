#include "minilucene/search/wildcard_term_enum.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/term_enum.h"

namespace minilucene {
namespace search {

WildcardTermEnum::WildcardTermEnum(index::IndexReader& reader, const index::Term& term)
    : pattern_(term.Text()) {
    auto terms = reader.Terms();
    if (terms) SetEnum(std::move(terms));
}

bool WildcardTermEnum::TermMatch(const index::Term& term) {
    return MatchText(term.Text());
}

bool WildcardTermEnum::MatchText(const std::string& text) const {
    return true;
}

}  // namespace search
}  // namespace minilucene
