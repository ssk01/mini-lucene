#pragma once

#include "minilucene/index/term.h"
#include "minilucene/search/query.h"

#include <memory>
#include <string>
#include <vector>

namespace minilucene {
namespace index {
class IndexReader;
}

namespace search {

class Scorer;

class PhraseQuery : public Query {
public:
    void Add(const index::Term& term);
    void SetSlop(int slop) { slop_ = slop; }
    int GetSlop() const { return slop_; }

    std::unique_ptr<Scorer> CreateScorer(index::IndexReader& reader) const override;
    std::string ToString() const override;

    const std::vector<index::Term>& Terms() const { return terms_; }

private:
    std::vector<index::Term> terms_;
    int slop_ = 0;
};

}  // namespace search
}  // namespace minilucene
