#pragma once

#include "minilucene/search/searcher.h"

#include <memory>
#include <vector>

namespace minilucene {
namespace search {

class MultiSearcher : public Searcher {
public:
    MultiSearcher();
    void AddSearcher(std::unique_ptr<Searcher> searcher);
    std::unique_ptr<Hits> Search(const Query& query) override;
    void Close() override;
    int MaxDoc() const { return total_docs_; }

private:
    std::vector<std::unique_ptr<Searcher>> searchers_;
    std::vector<int> doc_starts_;
    int total_docs_ = 0;
};

}  // namespace search
}  // namespace minilucene
