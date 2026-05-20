#include "minilucene/index/index_writer.h"
#include "minilucene/index/document_writer.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/analysis/analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/store/directory.h"

namespace minilucene {
namespace index {

IndexWriter::IndexWriter(store::Directory& dir, std::unique_ptr<analysis::Analyzer> analyzer)
    : dir_(dir), analyzer_(std::move(analyzer)) {
    writer_ = std::make_unique<DocumentWriter>(dir_, *analyzer_);
}

IndexWriter::~IndexWriter() {
    Close();
}

void IndexWriter::AddDocument(const document::Document& doc) {
    writer_->AddDocument(doc);
}

void IndexWriter::Close() {
    if (!closed_) {
        std::string seg_name = "_" + std::to_string(segment_counter_++);
        writer_->Flush(seg_name);
        segment_infos_.Add(seg_name, 1);
        segment_infos_.Write(dir_);
        closed_ = true;
    }
}

}  // namespace index
}  // namespace minilucene
