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

class Scorer;

class TermQuery : public Query {
public:
    explicit TermQuery(index::Term term);

    std::unique_ptr<Scorer> CreateScorer(index::IndexReader& reader) const override;
    std::string ToString() const override;

    const index::Term& Term() const { return term_; }

private:
    index::Term term_;
};

}  // namespace search
}  // namespace minilucene
