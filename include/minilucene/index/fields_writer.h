#pragma once

#include <memory>
#include <string>

namespace minilucene {
namespace store {
class Directory;
class IndexOutput;
}

namespace document {
class Document;
}

namespace index {

class FieldInfos;

class FieldsWriter {
public:
    FieldsWriter(store::Directory& dir, const std::string& segment, FieldInfos& field_infos);
    ~FieldsWriter();
    void AddDocument(const document::Document& doc);
    void Close();

private:
    FieldInfos& field_infos_;
    std::unique_ptr<store::IndexOutput> fdt_;
    std::unique_ptr<store::IndexOutput> fdx_;
};

}  // namespace index
}  // namespace minilucene
