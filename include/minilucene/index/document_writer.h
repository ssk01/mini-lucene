#pragma once

#include "minilucene/index/field_infos.h"
#include "minilucene/index/term.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

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

    void AddDocument(const document::Document& doc);
    void Flush(const std::string& segment);

private:
    struct DocPosting {
        int freq = 0;
        std::vector<int> positions;
    };

    void WriteFieldInfos(const std::string& segment);
    void WritePostings(const std::string& segment);

    store::Directory& dir_;
    analysis::Analyzer& analyzer_;
    std::unique_ptr<FieldInfos> field_infos_;
    std::map<Term, std::vector<DocPosting>> postings_;
    std::vector<std::vector<int>> field_tokens_per_doc_;
    int doc_count_ = 0;
};

}  // namespace index
}  // namespace minilucene
