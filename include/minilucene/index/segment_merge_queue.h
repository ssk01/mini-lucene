#pragma once

#include "minilucene/util/priority_queue.h"

#include <memory>

namespace minilucene {
namespace index {

class SegmentMergeInfo;

class SegmentMergeQueue : public util::PriorityQueue<SegmentMergeInfo*> {
public:
    explicit SegmentMergeQueue(int size);
protected:
    bool LessThan(SegmentMergeInfo* const& a, SegmentMergeInfo* const& b) const override;
};

}  // namespace index
}  // namespace minilucene
