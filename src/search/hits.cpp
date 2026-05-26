#include "minilucene/search/hits.h"
#include "minilucene/document/document.h"
#include "minilucene/index/index_reader.h"

namespace minilucene {
namespace search {

Hits::Hits(index::IndexReader& reader, int total_hits,
           std::vector<int> doc_ids, std::vector<float> scores)
    : reader_(&reader)
    , total_hits_(total_hits)
    , doc_ids_(std::move(doc_ids))
    , scores_(std::move(scores)) {}

Hits::Hits(DocFetcher fetcher, int total_hits,
           std::vector<int> doc_ids, std::vector<float> scores)
    : fetcher_(std::move(fetcher))
    , total_hits_(total_hits)
    , doc_ids_(std::move(doc_ids))
    , scores_(std::move(scores)) {}

std::unique_ptr<document::Document> Hits::Doc(int n) {
    if (n < 0 || n >= total_hits_) return nullptr;
    int id = doc_ids_[n];
    if (fetcher_) return fetcher_(id);
    if (reader_)  return reader_->Document(id);
    return nullptr;
}

}  // namespace search
}  // namespace minilucene
