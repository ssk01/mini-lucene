#pragma once

#include <string>
#include <vector>

namespace minilucene {
namespace store {
class Directory;
}

namespace index {

class SegmentMerger {
public:
    SegmentMerger(store::Directory& dir, const std::vector<std::string>& segments,
                  const std::string& merged_segment);
    void Merge();

private:
    store::Directory& dir_;
    std::vector<std::string> segments_;
    std::string merged_segment_;
};

}  // namespace index
}  // namespace minilucene
