#pragma once

#include "minilucene/index/term.h"
#include "minilucene/search/query.h"

namespace minilucene {
namespace index {
class IndexReader;
}

namespace search {

class FilteredTermEnum;

class MultiTermQuery : public Query {
public:
    explicit MultiTermQuery(const index::Term& term);
    std::unique_ptr<Scorer> CreateScorer(index::IndexReader& reader) const override;
    std::string ToString() const override;

protected:
    virtual FilteredTermEnum* GetEnum(index::IndexReader& reader) const = 0;
    index::Term term_;
};

}  // namespace search
}  // namespace minilucene
