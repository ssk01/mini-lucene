#pragma once

#include "minilucene/index/field_info.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace minilucene {
namespace document {
class Field;
}

namespace store {
class Directory;
}

namespace index {

class FieldInfos {
public:
    FieldInfos() = default;

    void AddField(const document::Field& field);
    // Union-merge: every field from `other` not already present is added
    // with its flags preserved. Existing fields keep their original
    // number (and their flags — i.e. a field is identified by name only,
    // flags do not promote from less-permissive to more-permissive).
    // Used by SegmentMerger to build a merged-segment schema that is the
    // union of all source-segment schemas.
    void Merge(const FieldInfos& other);
    void Write(store::Directory& dir, const std::string& segment);
    static std::unique_ptr<FieldInfos> Read(store::Directory& dir, const std::string& segment);

    int FieldNumber(const std::string& name) const;
    const FieldInfo* FieldByName(const std::string& name) const;
    const FieldInfo* FieldByNumber(int number) const;
    int Size() const { return static_cast<int>(fields_.size()); }

private:
    std::vector<FieldInfo> fields_;
    std::map<std::string, int> name_to_num_;
};

}  // namespace index
}  // namespace minilucene
