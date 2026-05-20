#include "minilucene/search/fuzzy_term_enum.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/term_enum.h"
#include <algorithm>
#include <vector>

namespace minilucene {
namespace search {

FuzzyTermEnum::FuzzyTermEnum(index::IndexReader& reader, const index::Term& term, int max_edits)
    : search_term_(term), max_edits_(max_edits) {
    auto terms = reader.Terms();
    if (terms) SetEnum(std::move(terms));
}

bool FuzzyTermEnum::TermMatch(const index::Term& term) {
    if (term.FieldNumber() != search_term_.FieldNumber()) return false;
    return EditDistance(term.Text(), search_term_.Text()) <= max_edits_;
}

int FuzzyTermEnum::EditDistance(const std::string& a, const std::string& b) {
    size_t m = a.size(), n = b.size();
    if (m < n) return EditDistance(b, a);
    if (n == 0) return static_cast<int>(m);
    std::vector<int> dp(n + 1);
    for (size_t j = 0; j <= n; ++j) dp[j] = static_cast<int>(j);
    for (size_t i = 1; i <= m; ++i) {
        int prev = dp[0];
        dp[0] = static_cast<int>(i);
        for (size_t j = 1; j <= n; ++j) {
            int temp = dp[j];
            dp[j] = std::min({prev + (a[i-1] == b[j-1] ? 0 : 1),
                              dp[j] + 1, dp[j-1] + 1});
            prev = temp;
        }
    }
    return dp[n];
}

}  // namespace search
}  // namespace minilucene
