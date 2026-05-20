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

class PrefixQuery : public Query {
public:
    PrefixQuery(index::Term prefix);
    std::unique_ptr<Scorer> CreateScorer(index::IndexReader& reader) const override;
    std::string ToString() const override;

private:
    index::Term prefix_;
};

}  // namespace search
}  // namespace minilucene
