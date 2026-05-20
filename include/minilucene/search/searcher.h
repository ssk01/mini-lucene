#pragma once

#include <memory>

namespace minilucene {
namespace index {
class Term;
}

namespace search {

class Hits;
class Query;

class Searcher {
public:
    virtual ~Searcher() = default;
    virtual std::unique_ptr<Hits> Search(const Query& query) = 0;
    virtual void Close() = 0;
};

}  // namespace search
}  // namespace minilucene
