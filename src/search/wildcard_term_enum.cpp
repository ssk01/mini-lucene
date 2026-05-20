#include "minilucene/search/wildcard_term_enum.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/term_enum.h"

namespace minilucene {
namespace search {

namespace {

bool MatchText(const std::string& text, size_t ti,
               const std::string& pattern, size_t pi) {
    while (pi < pattern.size()) {
        if (pattern[pi] == '*') {
            ++pi;
            if (pi == pattern.size()) return true;
            for (size_t i = ti; i <= text.size(); ++i) {
                if (MatchText(text, i, pattern, pi)) return true;
            }
            return false;
        }
        if (ti >= text.size()) return false;
        if (pattern[pi] != '?' && pattern[pi] != text[ti]) return false;
        ++pi; ++ti;
    }
    return ti == text.size();
}

}

WildcardTermEnum::WildcardTermEnum(index::IndexReader& reader, const index::Term& term)
    : pattern_(term.Text()), field_number_(term.FieldNumber()) {
    auto terms = reader.Terms();
    if (terms) SetEnum(std::move(terms));
}

bool WildcardTermEnum::TermMatch(const index::Term& term) {
    if (term.FieldNumber() != field_number_) return false;
    return MatchText(term.Text(), 0, pattern_, 0);
}

}  // namespace search
}  // namespace minilucene
