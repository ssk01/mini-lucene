#include "minilucene/search/hits.h"
#include "minilucene/document/document.h"
#include "minilucene/index/index_reader.h"

namespace minilucene {
namespace search {

Hits::Hits(index::IndexReader& reader, int total_hits,
           std::vector<int> doc_ids, std::vector<float> scores)
    : reader_(reader)
    , total_hits_(total_hits)
    , doc_ids_(std::move(doc_ids))
    , scores_(std::move(scores)) {}

std::unique_ptr<document::Document> Hits::Doc(int n) {
    if (n < 0 || n >= total_hits_) return nullptr;
    return reader_.Document(doc_ids_[n]);
}

}  // namespace search
}  // namespace minilucene
