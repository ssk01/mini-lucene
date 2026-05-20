#pragma once

#include "minilucene/search/scorer.h"

#include <memory>
#include <vector>

namespace minilucene {
namespace index {
class IndexReader;
}

namespace search {

class BooleanScorer : public Scorer {
public:
    BooleanScorer(std::vector<std::unique_ptr<Scorer>> must,
                  std::vector<std::unique_ptr<Scorer>> should,
                  std::vector<std::unique_ptr<Scorer>> must_not,
                  int overlap_max);
    bool Next() override;
    int Doc() const override { return current_doc_; }
    float Score() override;

private:
    int FindTarget();
    bool AlignMust(int target);
    bool AllMustAt(int target);
    int CountMust();
    int CountShouldAt(int target);
    bool MustNotMatch(int target);
    void AdvanceMustPast(int target);
    void AlignShould(int target);

    std::vector<std::unique_ptr<Scorer>> must_;
    std::vector<std::unique_ptr<Scorer>> should_;
    std::vector<std::unique_ptr<Scorer>> must_not_;
    int current_doc_ = -1;
    int overlap_ = 0;
    int overlap_max_;
};

}  // namespace search
}  // namespace minilucene
