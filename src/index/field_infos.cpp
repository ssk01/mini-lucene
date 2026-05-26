#include "minilucene/index/field_infos.h"
#include "minilucene/document/field.h"
#include "minilucene/store/directory.h"
#include "minilucene/store/index_input.h"
#include "minilucene/store/index_output.h"

#include <stdexcept>

namespace minilucene {
namespace index {

void FieldInfos::AddField(const document::Field& field) {
    if (name_to_num_.find(field.Name()) != name_to_num_.end()) return;
    int number = Size();
    fields_.emplace_back(field.Name(), number, field.IsStored(),
                         field.IsIndexed(), field.IsTokenized());
    name_to_num_[field.Name()] = number;
}

void FieldInfos::Merge(const FieldInfos& other) {
    for (const auto& src : other.fields_) {
        if (name_to_num_.find(src.Name()) != name_to_num_.end()) continue;
        int number = Size();
        fields_.emplace_back(src.Name(), number, src.IsStored(),
                             src.IsIndexed(), src.IsTokenized());
        name_to_num_[src.Name()] = number;
    }
}

void FieldInfos::Write(store::Directory& dir, const std::string& segment) {
    auto out = dir.CreateOutput(segment + ".fnm");
    out->WriteVInt(Size());
    for (int i = 0; i < Size(); ++i) {
        const auto& fi = fields_[i];
        out->WriteString(fi.Name());
        uint8_t bits = 0;
        if (fi.IsStored())    bits |= 0x01;
        if (fi.IsIndexed())   bits |= 0x02;
        if (fi.IsTokenized()) bits |= 0x04;
        out->WriteByte(bits);
    }
    out->Close();
}

std::unique_ptr<FieldInfos> FieldInfos::Read(store::Directory& dir, const std::string& segment) {
    auto in = dir.OpenInput(segment + ".fnm");
    auto fis = std::make_unique<FieldInfos>();
    int num = in->ReadVInt();
    for (int i = 0; i < num; ++i) {
        std::string name = in->ReadString();
        uint8_t bits = in->ReadByte();
        bool stored    = bits & 0x01;
        bool indexed   = bits & 0x02;
        bool tokenized = bits & 0x04;
        fis->fields_.emplace_back(name, i, stored, indexed, tokenized);
        fis->name_to_num_[name] = i;
    }
    in->Close();
    return fis;
}

int FieldInfos::FieldNumber(const std::string& name) const {
    auto it = name_to_num_.find(name);
    if (it == name_to_num_.end()) return -1;
    return it->second;
}

const FieldInfo* FieldInfos::FieldByName(const std::string& name) const {
    auto it = name_to_num_.find(name);
    if (it == name_to_num_.end()) return nullptr;
    return &fields_[it->second];
}

const FieldInfo* FieldInfos::FieldByNumber(int number) const {
    if (number < 0 || number >= Size()) return nullptr;
    return &fields_[number];
}

}  // namespace index
}  // namespace minilucene
