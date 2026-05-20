#pragma once

#include "minilucene/index/term.h"

#include <memory>
#include <string>

namespace minilucene {
namespace index {
class IndexReader;
}

namespace search {

class Scorer;

class TermQuery {
public:
    explicit TermQuery(index::Term term);

    std::unique_ptr<Scorer> Scorer(index::IndexReader& reader) const;

    const index::Term& Term() const { return term_; }
    std::string ToString() const;

private:
    index::Term term_;
};

}  // namespace search
}  // namespace minilucene
