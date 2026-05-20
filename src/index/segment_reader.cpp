#include "minilucene/index/segment_reader.h"
#include "minilucene/index/field_infos.h"
#include "minilucene/index/fields_reader.h"
#include "minilucene/index/term_infos_reader.h"
#include "minilucene/document/document.h"
#include "minilucene/store/directory.h"
#include "minilucene/store/index_input.h"
#include "minilucene/store/index_output.h"
#include "minilucene/util/bit_vector.h"

#include <stdexcept>

namespace minilucene {
namespace index {

SegmentTermEnum::SegmentTermEnum(store::IndexInput* tis)
    : tis_(tis) {}

SegmentTermEnum::~SegmentTermEnum() {
    Close();
}

bool SegmentTermEnum::Next() {
    if (ended_) return false;
    try {
        int field_num = tis_->ReadVInt();
        std::string text = tis_->ReadString();
        term_ = Term(field_num, text);
        doc_freq_ = tis_->ReadVInt();
        freq_pointer_ = tis_->ReadVLong();
        prox_pointer_ = tis_->ReadVLong();
        return true;
    } catch (...) {
        ended_ = true;
        return false;
    }
}

void SegmentTermEnum::Close() {
    if (tis_) {
        tis_->Close();
        tis_.reset();
    }
}

SegmentTermDocs::SegmentTermDocs(std::unique_ptr<store::IndexInput> frq,
                                 int64_t freq_pointer, int doc_freq)
    : frq_(std::move(frq)), remaining_(doc_freq) {
    frq_->Seek(freq_pointer);
}

SegmentTermDocs::SegmentTermDocs(std::unique_ptr<store::IndexInput> frq,
                                 int64_t freq_pointer, int doc_freq,
                                 util::BitVector* deleted_docs)
    : frq_(std::move(frq)), remaining_(doc_freq), deleted_docs_(deleted_docs) {
    frq_->Seek(freq_pointer);
}

SegmentTermDocs::~SegmentTermDocs() {
    Close();
}

bool SegmentTermDocs::Next() {
    while (remaining_ > 0) {
        try {
            int delta = frq_->ReadVInt();
            doc_ += delta;
            freq_ = frq_->ReadVInt();
            --remaining_;
            if (!deleted_docs_ || !deleted_docs_->Get(doc_)) return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

void SegmentTermDocs::Close() {
    if (frq_) {
        frq_->Close();
        frq_.reset();
    }
}

SegmentTermPositions::SegmentTermPositions(std::unique_ptr<store::IndexInput> frq,
                                           std::unique_ptr<store::IndexInput> prx,
                                           int64_t freq_pointer, int64_t prox_pointer,
                                           int doc_freq)
    : frq_(std::move(frq)), prx_(std::move(prx))
    , remaining_(doc_freq), remaining_positions_(0) {
    frq_->Seek(freq_pointer);
    prx_->Seek(prox_pointer);
}

SegmentTermPositions::SegmentTermPositions(std::unique_ptr<store::IndexInput> frq,
                                           std::unique_ptr<store::IndexInput> prx,
                                           int64_t freq_pointer, int64_t prox_pointer,
                                           int doc_freq, util::BitVector* deleted_docs)
    : frq_(std::move(frq)), prx_(std::move(prx))
    , deleted_docs_(deleted_docs)
    , remaining_(doc_freq), remaining_positions_(0) {
    frq_->Seek(freq_pointer);
    prx_->Seek(prox_pointer);
}

SegmentTermPositions::~SegmentTermPositions() {
    Close();
}

bool SegmentTermPositions::Next() {
    while (remaining_ > 0) {
        try {
            int delta = frq_->ReadVInt();
            doc_ += delta;
            freq_ = frq_->ReadVInt();
            --remaining_;
            if (!deleted_docs_ || !deleted_docs_->Get(doc_)) {
                remaining_positions_ = freq_;
                // prx position for this doc is at the right offset already
                return true;
            }
            // Skip positions for deleted doc
            for (int i = 0; i < freq_; ++i) prx_->ReadVInt();
        } catch (...) {
            return false;
        }
    }
    return false;
}

int SegmentTermPositions::NextPosition() {
    --remaining_positions_;
    return prx_->ReadVInt();
}

void SegmentTermPositions::Close() {
    if (frq_) frq_->Close();
    if (prx_) prx_->Close();
}

SegmentReader::SegmentReader(store::Directory& dir, const std::string& segment)
    : dir_(dir), segment_(segment) {
    field_infos_ = FieldInfos::Read(dir_, segment_);
    term_infos_ = std::make_unique<TermInfosReader>(dir_, segment_);
    if (dir_.FileExists(segment_ + ".fdt")) {
        fields_reader_ = std::make_unique<FieldsReader>(dir_, segment_, *field_infos_);
    }
    if (dir_.FileExists(segment_ + ".nrm")) {
        nrm_ = dir_.OpenInput(segment_ + ".nrm");
        if (field_infos_->Size() > 0) {
            num_docs_ = static_cast<int>(nrm_->Length() / field_infos_->Size());
        }
    }
    if (dir_.FileExists(segment_ + ".del")) {
        auto del_in = dir_.OpenInput(segment_ + ".del");
        deleted_docs_ = util::BitVector::Read(*del_in);
        del_in->Close();
    }
}

std::unique_ptr<document::Document> SegmentReader::Document(int doc_id) {
    if (!fields_reader_) return nullptr;
    return fields_reader_->Document(doc_id);
}

void SegmentReader::Delete(int doc_id) {
    if (!deleted_docs_) {
        deleted_docs_ = std::make_unique<util::BitVector>(num_docs_);
    }
    deleted_docs_->Set(doc_id);
}

int SegmentReader::NumDocs() const {
    if (deleted_docs_) return num_docs_ - deleted_docs_->Count();
    return num_docs_;
}

SegmentReader::~SegmentReader() {
    Close();
}

float SegmentReader::Norm(int doc, int field_number) {
    if (!nrm_) return 1.0f;
    nrm_->Seek(static_cast<int64_t>(doc) * field_infos_->Size() + field_number);
    uint8_t b = nrm_->ReadByte();
    return static_cast<float>(b) / 255.0f;
}

std::unique_ptr<TermEnum> SegmentReader::Terms() {
    auto tis = dir_.OpenInput(segment_ + ".tis");
    return std::make_unique<SegmentTermEnum>(tis.release());
}

std::unique_ptr<TermDocs> SegmentReader::Docs(const Term& term) {
    TermInfo ti = term_infos_->Get(term);
    if (ti.doc_freq == 0) return nullptr;
    auto frq = dir_.OpenInput(segment_ + ".frq");
    return std::make_unique<SegmentTermDocs>(std::move(frq), ti.freq_pointer, ti.doc_freq,
                                             deleted_docs_.get());
}

std::unique_ptr<TermPositions> SegmentReader::Positions(const Term& term) {
    TermInfo ti = term_infos_->Get(term);
    if (ti.doc_freq == 0) return nullptr;
    auto frq = dir_.OpenInput(segment_ + ".frq");
    auto prx = dir_.OpenInput(segment_ + ".prx");
    return std::make_unique<SegmentTermPositions>(
        std::move(frq), std::move(prx), ti.freq_pointer, ti.prox_pointer, ti.doc_freq,
        deleted_docs_.get());
}

int SegmentReader::DocFreq(const Term& term) {
    TermInfo ti = term_infos_->Get(term);
    return ti.doc_freq;
}

void SegmentReader::Close() {
    if (deleted_docs_ && deleted_docs_->Count() > 0) {
        auto del_out = dir_.CreateOutput(segment_ + ".del");
        deleted_docs_->Write(*del_out);
        del_out->Close();
    }
    if (nrm_) {
        nrm_->Close();
        nrm_.reset();
    }
    if (term_infos_) {
        term_infos_->Close();
        term_infos_.reset();
    }
}

}  // namespace index
}  // namespace minilucene
