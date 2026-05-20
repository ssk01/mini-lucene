#include "minilucene/search/multi_searcher.h"

namespace minilucene {
namespace search {

MultiSearcher::MultiSearcher() {}

std::unique_ptr<Hits> MultiSearcher::Search(const Query& query) {
    return nullptr;
}

void MultiSearcher::Close() {}

}  // namespace search
}  // namespace minilucene
