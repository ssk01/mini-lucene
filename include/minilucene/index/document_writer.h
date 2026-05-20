#pragma once

#include <string>

namespace minilucene {
namespace document {
class Document;
}

namespace store {
class Directory;
}

namespace analysis {
class Analyzer;
}

namespace index {

class DocumentWriter {
public:
    DocumentWriter(store::Directory& dir, analysis::Analyzer& analyzer);

    void AddDocument(const std::string& segment, const document::Document& doc);

private:
    store::Directory& dir_;
    analysis::Analyzer& analyzer_;
};

}  // namespace index
}  // namespace minilucene
