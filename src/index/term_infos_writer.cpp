#include "minilucene/index/term_infos_writer.h"
#include "minilucene/store/directory.h"
#include "minilucene/store/index_output.h"

#include <stdexcept>

namespace minilucene {
namespace index {

static const int TII_INTERVAL = 128;

TermInfosWriter::TermInfosWriter(store::Directory& dir, const std::string& segment) {
    tis_ = dir.CreateOutput(segment + ".tis");
    tii_ = dir.CreateOutput(segment + ".tii");
}

TermInfosWriter::~TermInfosWriter() {
    if (!closed_) Close();
}

void TermInfosWriter::Add(const Term& term, const TermInfo& ti) {
    if (num_terms_ % TII_INTERVAL == 0) {
        tii_->WriteVInt(term.FieldNumber());
        tii_->WriteString(term.Text());
        tii_->WriteVLong(tis_offset_);
    }

    tis_->WriteVInt(term.FieldNumber());
    tis_->WriteString(term.Text());
    tis_->WriteVInt(ti.doc_freq);
    tis_->WriteVLong(ti.freq_pointer);
    tis_->WriteVLong(ti.prox_pointer);

    tis_offset_ = tis_->FilePointer();
    ++num_terms_;
}

void TermInfosWriter::Close() {
    if (closed_) return;
    closed_ = true;
    tis_->Close();
    tii_->Close();
}

}  // namespace index
}  // namespace minilucene
