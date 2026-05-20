#pragma once

#include "minilucene/index/term.h"
#include "minilucene/search/query.h"

#include <memory>
#include <string>

namespace minilucene {
namespace index {
class IndexReader;
}

namespace search {

class FuzzyQuery : public Query {
public:
    explicit FuzzyQuery(index::Term term, int max_edits = 2);
    std::unique_ptr<Scorer> CreateScorer(index::IndexReader& reader) const override;
    std::string ToString() const override;

private:
    static int EditDistance(const std::string& a, const std::string& b);
    index::Term term_;
    int max_edits_;
};

}  // namespace search
}  // namespace minilucene
