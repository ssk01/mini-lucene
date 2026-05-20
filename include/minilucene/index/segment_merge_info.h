#pragma once

#include "minilucene/index/term.h"
#include "minilucene/index/term_enum.h"

#include <memory>

namespace minilucene {
namespace index {

class SegmentMergeInfo {
public:
    SegmentMergeInfo();
    ~SegmentMergeInfo();
    bool Next();
    Term term_;
    int64_t base_;
    std::unique_ptr<TermEnum> terms_;
};

}  // namespace index
}  // namespace minilucene
