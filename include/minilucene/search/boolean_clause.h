#pragma once

#include <memory>

namespace minilucene {
namespace search {

class Query;

enum class Occur { SHOULD, MUST, MUST_NOT };

struct BooleanClause {
    std::unique_ptr<Query> query;
    Occur occur;
};

}  // namespace search
}  // namespace minilucene
