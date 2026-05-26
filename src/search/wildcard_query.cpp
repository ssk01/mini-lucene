#include "minilucene/search/wildcard_query.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/term_enum.h"
#include "minilucene/search/boolean_query.h"
#include "minilucene/search/boolean_clause.h"
#include "minilucene/search/scorer.h"
#include "minilucene/search/term_query.h"

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

WildcardQuery::WildcardQuery(index::Term term)
    : term_(std::move(term)), pattern_(term_.Text()) {}

bool WildcardQuery::Match(const std::string& text) const {
    return MatchText(text, 0, pattern_, 0);
}

std::unique_ptr<Scorer> WildcardQuery::CreateScorer(index::IndexReader& reader) const {
    auto terms = reader.Terms();
    if (!terms) return nullptr;

    BooleanQuery combined;
    int count = 0;

    while (terms->Next()) {
        const auto& t = terms->Current();
        if (t.FieldNumber() != term_.FieldNumber()) continue;
        if (!Match(t.Text())) continue;
        combined.Add(std::make_unique<TermQuery>(t), Occur::SHOULD);
        ++count;
    }
    terms->Close();

    if (count == 0) return nullptr;
    return combined.CreateScorer(reader);
}

std::string WildcardQuery::ToString() const {
    std::string s = FieldDisplay(term_.FieldNumber()) + ":" + pattern_;
    if (boost_ != 1.0f) s += "^" + std::to_string(boost_);
    return s;
}

}  // namespace search
}  // namespace minilucene
