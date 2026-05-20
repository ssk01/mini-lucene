#pragma once

#include <memory>
#include <string>

namespace minilucene {
namespace index {
class IndexReader;
}

namespace search {

class Scorer;

class Query {
public:
    virtual ~Query() = default;
    virtual std::unique_ptr<Scorer> CreateScorer(index::IndexReader& reader) const = 0;
    virtual std::string ToString() const = 0;
};

}  // namespace search
}  // namespace minilucene
