#pragma once

#include "minilucene/search/boolean_clause.h"
#include "minilucene/search/query.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace minilucene {
namespace search {

class BooleanQuery : public Query {
public:
    static const int MAX_CLAUSE_COUNT = 32;

    void Add(std::unique_ptr<Query> query, Occur occur);
    std::unique_ptr<Scorer> CreateScorer(index::IndexReader& reader) const override;
    std::string ToString() const override;

    const std::vector<BooleanClause>& Clauses() const { return clauses_; }

private:
    std::vector<BooleanClause> clauses_;
};

class TooManyClauses : public std::logic_error {
public:
    TooManyClauses() : std::logic_error("max 32 clauses") {}
};

}  // namespace search
}  // namespace minilucene
