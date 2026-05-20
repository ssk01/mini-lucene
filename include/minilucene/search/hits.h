#pragma once

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
    Hits(index::IndexReader& reader, int total_hits,
         std::vector<int> doc_ids, std::vector<float> scores);

    int Length() const { return total_hits_; }
    float Score(int n) const { return scores_[n]; }
    int Id(int n) const { return doc_ids_[n]; }
    std::unique_ptr<document::Document> Doc(int n);

private:
    index::IndexReader& reader_;
    int total_hits_;
    std::vector<int> doc_ids_;
    std::vector<float> scores_;
};

}  // namespace search
}  // namespace minilucene
