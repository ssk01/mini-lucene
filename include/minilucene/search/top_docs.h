#pragma once

#include <cstdint>
#include <vector>

namespace minilucene {
namespace search {

struct ScoreDoc {
    int doc = 0;
    float score = 0.0f;
};

struct TopDocs {
    int total_hits = 0;
    std::vector<ScoreDoc> score_docs;
};

}  // namespace search
}  // namespace minilucene
