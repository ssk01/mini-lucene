#pragma once

#include "minilucene/search/searcher.h"

#include <memory>
#include <vector>

namespace minilucene {
namespace search {

class MultiSearcher : public Searcher {
public:
    MultiSearcher();
    std::unique_ptr<Hits> Search(const Query& query) override;
    void Close() override;
};

}  // namespace search
}  // namespace minilucene
