#pragma once

#include <memory>
#include <string>
#include <vector>

namespace minilucene {
namespace store {
class Directory;
class IndexInput;
}

namespace document {
class Document;
}

namespace index {

class FieldInfos;

class FieldsReader {
public:
    FieldsReader(store::Directory& dir, const std::string& segment, FieldInfos& field_infos);
    ~FieldsReader();
    std::unique_ptr<document::Document> Document(int doc_id);
    void Close();

private:
    FieldInfos& field_infos_;
    std::unique_ptr<store::IndexInput> fdt_;
    std::unique_ptr<store::IndexInput> fdx_;
    std::vector<int64_t> doc_offsets_;
};

}  // namespace index
}  // namespace minilucene
