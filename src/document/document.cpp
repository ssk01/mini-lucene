#include "minilucene/document/document.h"

namespace minilucene {
namespace document {

void Document::Add(Field field) {
    fields_.push_back(std::move(field));
}

const Field* Document::GetField(const std::string& name) const {
    for (const auto& f : fields_) {
        if (f.Name() == name) return &f;
    }
    return nullptr;
}

}  // namespace document
}  // namespace minilucene
