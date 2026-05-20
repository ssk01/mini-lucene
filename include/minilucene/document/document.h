#pragma once

#include "minilucene/document/field.h"

#include <string>
#include <vector>

namespace minilucene {
namespace document {

class Document {
public:
    void Add(Field field);
    const Field* GetField(const std::string& name) const;
    const std::vector<Field>& Fields() const { return fields_; }

private:
    std::vector<Field> fields_;
};

}  // namespace document
}  // namespace minilucene
