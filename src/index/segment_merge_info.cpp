#include "minilucene/index/segment_merge_info.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/util/bit_vector.h"

namespace minilucene {
namespace index {

SegmentMergeInfo::SegmentMergeInfo(int base, std::unique_ptr<TermEnum> term_enum,
                                   SegmentReader* reader)
    : base_(base), reader_(reader), term_enum_(std::move(term_enum)) {
    if (term_enum_->Next()) {
        term_ = term_enum_->Current();
    }
    postings_ = reader->Positions(term_);

    // Build doc_map for deleted docs
    // Not yet implemented — requires access to deleted docs from reader
}

SegmentMergeInfo::~SegmentMergeInfo() {
    Close();
}

bool SegmentMergeInfo::Next() {
    if (term_enum_->Next()) {
        term_ = term_enum_->Current();
        return true;
    }
    return false;
}

void SegmentMergeInfo::Close() {
    if (term_enum_) { term_enum_->Close(); term_enum_.reset(); }
    if (postings_) { postings_->Close(); postings_.reset(); }
}

}  // namespace index
}  // namespace minilucene
