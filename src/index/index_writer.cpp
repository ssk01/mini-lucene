#include "minilucene/index/index_writer.h"
#include "minilucene/index/document_writer.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/index/segment_merger.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/analysis/analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/store/directory.h"

namespace minilucene {
namespace index {

IndexWriter::IndexWriter(store::Directory& dir, std::unique_ptr<analysis::Analyzer> analyzer)
    : dir_(dir), analyzer_(std::move(analyzer)) {
    writer_ = std::make_unique<DocumentWriter>(dir_, *analyzer_);
    segment_infos_ = *SegmentInfos::Read(dir_);
    for (const auto& seg : segment_infos_.Segments()) {
        int num = std::stoi(seg.name.substr(1));
        if (num >= segment_counter_) segment_counter_ = num + 1;
    }
}

IndexWriter::~IndexWriter() {
    Close();
}

void IndexWriter::AddDocument(const document::Document& doc) {
    writer_->AddDocument(doc);
    ++pending_docs_;
    if (pending_docs_ >= mergeFactor) {
        FlushPending();
    }
}

void IndexWriter::FlushPending() {
    if (pending_docs_ == 0) return;
    std::string seg_name = "_" + std::to_string(segment_counter_++);
    writer_->Flush(seg_name);
    segment_infos_.Add(seg_name, pending_docs_);
    segment_infos_.Write(dir_);
    writer_ = std::make_unique<DocumentWriter>(dir_, *analyzer_);
    pending_docs_ = 0;
}

void IndexWriter::Optimize() {
    FlushPending();
    auto segs = segment_infos_.Segments();
    if (segs.empty()) return;

    // Bail out early only if single segment AND no deletions to compact
    if (segs.size() == 1) {
        auto sr = std::make_unique<SegmentReader>(dir_, segs[0].name);
        bool has_deletions = sr->NumDocs() != sr->MaxDoc();
        sr->Close();
        if (!has_deletions) return;
    }

    std::vector<std::string> seg_names;
    for (const auto& seg : segs) {
        seg_names.push_back(seg.name);
    }

    std::string merged = "_" + std::to_string(segment_counter_++);
    SegmentMerger merger(dir_, seg_names, merged);
    merger.Merge();

    for (const auto& name : seg_names) {
        if (name == merged) continue;
        dir_.DeleteFile(name + ".fnm");
        dir_.DeleteFile(name + ".fdt");
        dir_.DeleteFile(name + ".fdx");
        dir_.DeleteFile(name + ".tis");
        dir_.DeleteFile(name + ".tii");
        dir_.DeleteFile(name + ".frq");
        dir_.DeleteFile(name + ".prx");
        dir_.DeleteFile(name + ".nrm");
        dir_.DeleteFile(name + ".del");
    }

    auto merged_reader = std::make_unique<SegmentReader>(dir_, merged);
    int total_docs = merged_reader->NumDocs();
    merged_reader->Close();

    segment_infos_ = SegmentInfos();
    segment_infos_.Add(merged, total_docs);
    segment_infos_.Write(dir_);
}

void IndexWriter::Close() {
    if (!closed_) {
        FlushPending();
        closed_ = true;
    }
}

}  // namespace index
}  // namespace minilucene
