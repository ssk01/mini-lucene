#pragma once

#include "minilucene/search/top_docs.h"

#include <memory>

namespace minilucene {
namespace index {
class IndexReader;
}

namespace search {

class TermQuery;

class IndexSearcher {
public:
    explicit IndexSearcher(index::IndexReader& reader);
    TopDocs Search(const TermQuery& query, int top_k) const;

private:
    index::IndexReader* reader_;
};

}  // namespace search
}  // namespace minilucene
