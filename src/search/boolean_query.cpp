#include "minilucene/search/boolean_query.h"
#include "minilucene/search/boolean_scorer.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/search/scorer.h"
#include "minilucene/search/similarity.h"

#include <algorithm>
#include <climits>

namespace minilucene {
namespace search {

void BooleanQuery::Add(std::unique_ptr<Query> q, Occur occur) {
    if (clauses_.size() >= static_cast<size_t>(MAX_CLAUSE_COUNT)) {
        throw TooManyClauses();
    }
    clauses_.push_back({std::move(q), occur});
}

std::unique_ptr<Scorer> BooleanQuery::CreateScorer(index::IndexReader& reader) const {
    std::vector<std::unique_ptr<Scorer>> must;
    std::vector<std::unique_ptr<Scorer>> should;
    std::vector<std::unique_ptr<Scorer>> must_not;

    for (const auto& clause : clauses_) {
        auto s = clause.query->CreateScorer(reader);
        if (!s) {
            if (clause.occur == Occur::MUST) return nullptr;
            continue;
        }
        switch (clause.occur) {
            case Occur::MUST:    must.push_back(std::move(s)); break;
            case Occur::SHOULD:  should.push_back(std::move(s)); break;
            case Occur::MUST_NOT: must_not.push_back(std::move(s)); break;
        }
    }

    if (must.empty() && should.empty()) return nullptr;

    int overlap_max = static_cast<int>(must.size() + should.size() + 1);

    return std::make_unique<BooleanScorer>(
        std::move(must), std::move(should), std::move(must_not),
        overlap_max);
}

std::string BooleanQuery::ToString() const {
    std::string result;
    for (size_t i = 0; i < clauses_.size(); ++i) {
        if (i > 0) result += " ";
        const auto& c = clauses_[i];
        switch (c.occur) {
            case Occur::MUST:    result += "+"; break;
            case Occur::MUST_NOT: result += "-"; break;
            default: break;
        }
        result += c.query->ToString();
    }
    return result;
}

}  // namespace search
}  // namespace minilucene
