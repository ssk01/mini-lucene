#include "minilucene/index/field_info.h"

namespace minilucene {
namespace index {

FieldInfo::FieldInfo(std::string name, int number, bool stored, bool indexed, bool tokenized)
    : name_(std::move(name))
    , number_(number)
    , stored_(stored)
    , indexed_(indexed)
    , tokenized_(tokenized) {}

}  // namespace index
}  // namespace minilucene
