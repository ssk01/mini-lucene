#include "minilucene/search/fuzzy_query.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/term_enum.h"
#include "minilucene/search/boolean_query.h"
#include "minilucene/search/boolean_clause.h"
#include "minilucene/search/scorer.h"
#include "minilucene/search/term_query.h"

#include <algorithm>
#include <vector>

namespace minilucene {
namespace search {

FuzzyQuery::FuzzyQuery(index::Term term, int max_edits)
    : term_(std::move(term)), max_edits_(max_edits) {}

int FuzzyQuery::EditDistance(const std::string& a, const std::string& b) {
    size_t m = a.size(), n = b.size();
    std::vector<int> dp(n + 1);
    for (size_t j = 0; j <= n; ++j) dp[j] = static_cast<int>(j);

    for (size_t i = 1; i <= m; ++i) {
        int prev = dp[0];
        dp[0] = static_cast<int>(i);
        for (size_t j = 1; j <= n; ++j) {
            int temp = dp[j];
            if (a[i - 1] == b[j - 1]) {
                dp[j] = prev;
            } else {
                dp[j] = 1 + std::min({prev, dp[j], dp[j - 1]});
            }
            prev = temp;
        }
    }
    return dp[n];
}

std::unique_ptr<Scorer> FuzzyQuery::CreateScorer(index::IndexReader& reader) const {
    auto terms = reader.Terms();
    if (!terms) return nullptr;

    BooleanQuery combined;
    int count = 0;

    while (terms->Next()) {
        const auto& t = terms->Current();
        if (t.FieldNumber() != term_.FieldNumber()) continue;
        if (EditDistance(t.Text(), term_.Text()) > max_edits_) continue;
        combined.Add(std::make_unique<TermQuery>(t), Occur::SHOULD);
        ++count;
    }
    terms->Close();

    if (count == 0) return nullptr;
    return combined.CreateScorer(reader);
}

std::string FuzzyQuery::ToString() const {
    std::string s = FieldDisplay(term_.FieldNumber()) + ":" +
                    term_.Text() + "~";
    if (boost_ != 1.0f) s += "^" + std::to_string(boost_);
    return s;
}

}  // namespace search
}  // namespace minilucene
