#pragma once

#include "minilucene/util/priority_queue.h"

namespace minilucene {
namespace index {

class SegmentMergeInfo;

class SegmentMergeQueue : public util::PriorityQueue<SegmentMergeInfo*> {
public:
    explicit SegmentMergeQueue(int size);
    void Close();

protected:
    bool LessThan(SegmentMergeInfo* const& a, SegmentMergeInfo* const& b) const override;
};

}  // namespace index
}  // namespace minilucene
