#include "minilucene/index/fields_reader.h"
#include "minilucene/index/field_infos.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/directory.h"
#include "minilucene/store/index_input.h"

namespace minilucene {
namespace index {

FieldsReader::FieldsReader(store::Directory& dir, const std::string& segment,
                           FieldInfos& field_infos)
    : field_infos_(field_infos) {
    fdt_ = dir.OpenInput(segment + ".fdt");
    fdx_ = dir.OpenInput(segment + ".fdx");
    int64_t fdx_len = fdx_->Length();
    while (fdx_->FilePointer() < fdx_len) {
        doc_offsets_.push_back(fdx_->ReadVLong());
    }
}

FieldsReader::~FieldsReader() {
    Close();
}

std::unique_ptr<document::Document> FieldsReader::Document(int doc_id) {
    if (doc_id < 0 || static_cast<size_t>(doc_id) >= doc_offsets_.size()) return nullptr;

    fdt_->Seek(doc_offsets_[doc_id]);
    int stored_count = fdt_->ReadVInt();
    if (stored_count == 0) return nullptr;

    auto doc = std::make_unique<document::Document>();
    for (int i = 0; i < stored_count; ++i) {
        int field_num = fdt_->ReadVInt();
        uint8_t bits = fdt_->ReadByte();
        std::string value = fdt_->ReadString();
        (void)bits;
        const auto* fi = field_infos_.FieldByNumber(field_num);
        if (fi) {
            // Reconstruct with appropriate factory method
            if (fi->IsStored() && fi->IsIndexed() && fi->IsTokenized()) {
                doc->Add(document::Field::Text(fi->Name(), value));
            } else if (fi->IsStored() && fi->IsIndexed() && !fi->IsTokenized()) {
                doc->Add(document::Field::Keyword(fi->Name(), value));
            } else {
                doc->Add(document::Field::UnIndexed(fi->Name(), value));
            }
        }
    }
    return doc;
}

void FieldsReader::Close() {
    if (fdt_) { fdt_->Close(); fdt_.reset(); }
    if (fdx_) { fdx_->Close(); fdx_.reset(); }
}

}  // namespace index
}  // namespace minilucene
