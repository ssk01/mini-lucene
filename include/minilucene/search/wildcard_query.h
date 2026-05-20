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

class WildcardQuery : public Query {
public:
    WildcardQuery(index::Term term);
    std::unique_ptr<Scorer> CreateScorer(index::IndexReader& reader) const override;
    std::string ToString() const override;

private:
    bool Match(const std::string& text) const;
    index::Term term_;
    std::string pattern_;
};

}  // namespace search
}  // namespace minilucene
