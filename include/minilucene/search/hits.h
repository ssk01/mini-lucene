#pragma once

#include <functional>
#include <memory>
#include <vector>

namespace minilucene {
namespace document {
class Document;
}

namespace index {
class IndexReader;
}

namespace search {

class Hits {
public:
    // Backed by a single IndexReader. Standard single-index Search path.
    Hits(index::IndexReader& reader, int total_hits,
         std::vector<int> doc_ids, std::vector<float> scores);

    // Backed by an arbitrary fetcher — used by MultiSearcher where the
    // doc id is a synthetic global id that must be mapped to a sub-reader.
    using DocFetcher = std::function<std::unique_ptr<document::Document>(int)>;
    Hits(DocFetcher fetcher, int total_hits,
         std::vector<int> doc_ids, std::vector<float> scores);

    int Length() const { return total_hits_; }
    float Score(int n) const { return scores_[n]; }
    int Id(int n) const { return doc_ids_[n]; }
    std::unique_ptr<document::Document> Doc(int n);

private:
    index::IndexReader* reader_ = nullptr;
    DocFetcher fetcher_;
    int total_hits_;
    std::vector<int> doc_ids_;
    std::vector<float> scores_;
};

}  // namespace search
}  // namespace minilucene
