#include "minilucene/document/field.h"

namespace minilucene {
namespace document {

Field::Field(std::string name, std::string value, bool stored, bool indexed, bool tokenized)
    : name_(std::move(name))
    , value_(std::move(value))
    , stored_(stored)
    , indexed_(indexed)
    , tokenized_(tokenized) {}

Field Field::Keyword(const std::string& name, const std::string& value) {
    return Field(name, value, true, true, false);
}

Field Field::Text(const std::string& name, const std::string& value) {
    return Field(name, value, true, true, true);
}

Field Field::UnIndexed(const std::string& name, const std::string& value) {
    return Field(name, value, true, false, false);
}

Field Field::UnStored(const std::string& name, const std::string& value) {
    return Field(name, value, false, true, true);
}

}  // namespace document
}  // namespace minilucene
