#pragma once

#include <memory>
#include <string>
#include <vector>

namespace minilucene {
namespace store {
class Directory;
}

namespace index {

struct SegmentInfo {
    std::string name;
    int doc_count = 0;
};

class SegmentInfos {
public:
    SegmentInfos() = default;

    void Add(const std::string& name, int doc_count);
    void Write(store::Directory& dir);
    static std::unique_ptr<SegmentInfos> Read(store::Directory& dir);

    const std::vector<SegmentInfo>& Segments() const { return segments_; }
    int Size() const { return static_cast<int>(segments_.size()); }

private:
    std::vector<SegmentInfo> segments_;
};

}  // namespace index
}  // namespace minilucene
