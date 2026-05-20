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
