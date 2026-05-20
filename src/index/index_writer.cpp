#include "minilucene/index/index_writer.h"
#include "minilucene/index/document_writer.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/analysis/analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/store/directory.h"

namespace minilucene {
namespace index {

IndexWriter::IndexWriter(store::Directory& dir, std::unique_ptr<analysis::Analyzer> analyzer)
    : dir_(dir), analyzer_(std::move(analyzer)) {}

IndexWriter::~IndexWriter() {
    Close();
}

void IndexWriter::AddDocument(const document::Document& doc) {
    std::string seg_name = "_" + std::to_string(segment_counter_++);
    DocumentWriter writer(dir_, *analyzer_);
    writer.AddDocument(seg_name, doc);
    segment_infos_.Add(seg_name, 1);
    segment_infos_.Write(dir_);
}

void IndexWriter::Close() {}

}  // namespace index
}  // namespace minilucene
