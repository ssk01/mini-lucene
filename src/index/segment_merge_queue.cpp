#include "minilucene/index/segment_merge_queue.h"

namespace minilucene {
namespace index {

SegmentMergeQueue::SegmentMergeQueue(int size) { Initialize(size); }

bool SegmentMergeQueue::LessThan(SegmentMergeInfo* const& a, SegmentMergeInfo* const& b) const {
    return false;
}

}  // namespace index
}  // namespace minilucene
