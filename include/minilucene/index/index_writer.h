#pragma once

#include "minilucene/index/segment_infos.h"

#include <memory>
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

class IndexWriter {
public:
    IndexWriter(store::Directory& dir, std::unique_ptr<analysis::Analyzer> analyzer);
    ~IndexWriter();

    void AddDocument(const document::Document& doc);
    void Close();

private:
    store::Directory& dir_;
    std::unique_ptr<analysis::Analyzer> analyzer_;
    SegmentInfos segment_infos_;
    int segment_counter_ = 0;
};

}  // namespace index
}  // namespace minilucene
