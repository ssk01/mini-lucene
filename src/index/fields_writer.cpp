#include "minilucene/index/fields_writer.h"
#include "minilucene/index/field_infos.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/directory.h"
#include "minilucene/store/index_output.h"

namespace minilucene {
namespace index {

FieldsWriter::FieldsWriter(store::Directory& dir, const std::string& segment,
                           FieldInfos& field_infos)
    : field_infos_(field_infos) {
    fdt_ = dir.CreateOutput(segment + ".fdt");
    fdx_ = dir.CreateOutput(segment + ".fdx");
}

FieldsWriter::~FieldsWriter() {
    Close();
}

void FieldsWriter::AddDocument(const document::Document& doc) {
    fdx_->WriteVLong(fdt_->FilePointer());

    int stored_count = 0;
    for (const auto& field : doc.Fields()) {
        if (field.IsStored()) ++stored_count;
    }
    fdt_->WriteVInt(stored_count);

    for (const auto& field : doc.Fields()) {
        if (!field.IsStored()) continue;
        int field_num = field_infos_.FieldNumber(field.Name());
        fdt_->WriteVInt(field_num);
        uint8_t bits = 0;
        if (field.IsTokenized()) bits |= 1;
        fdt_->WriteByte(bits);
        fdt_->WriteString(field.Value());
    }
}

void FieldsWriter::Close() {
    if (fdt_) { fdt_->Close(); fdt_.reset(); }
    if (fdx_) { fdx_->Close(); fdx_.reset(); }
}

}  // namespace index
}  // namespace minilucene
