#include "minilucene/index/term_infos_reader.h"
#include "minilucene/store/directory.h"
#include "minilucene/store/index_input.h"

#include <algorithm>

namespace minilucene {
namespace index {

TermInfosReader::TermInfosReader(store::Directory& dir, const std::string& segment) {
    auto tii = dir.OpenInput(segment + ".tii");
    while (true) {
        try {
            TiiEntry entry;
            int field_num = tii->ReadVInt();
            std::string text = tii->ReadString();
            entry.term = Term(field_num, text);
            entry.tis_offset = tii->ReadVLong();
            tii_entries_.push_back(entry);
        } catch (...) {
            break;
        }
    }
    tii->Close();

    tis_ = dir.OpenInput(segment + ".tis");
}

TermInfosReader::~TermInfosReader() {
    Close();
}

TermInfo TermInfosReader::ReadTisEntry(store::IndexInput& tis) {
    tis.ReadVInt();
    tis.ReadString();
    TermInfo ti;
    ti.doc_freq = tis.ReadVInt();
    ti.freq_pointer = tis.ReadVLong();
    ti.prox_pointer = tis.ReadVLong();
    return ti;
}

TermInfo TermInfosReader::Get(const Term& target) {
    if (tii_entries_.empty()) {
        TermInfo ti;
        ti.doc_freq = 0;
        return ti;
    }

    auto it = std::upper_bound(tii_entries_.begin(), tii_entries_.end(), target,
        [](const Term& t, const TiiEntry& e) {
            return t < e.term;
        });
    if (it != tii_entries_.begin()) --it;

    tis_->Seek(it->tis_offset);
    for (int i = 0; i < 128; ++i) {
        TermInfo ti;
        try {
            int field_num = tis_->ReadVInt();
            std::string text = tis_->ReadString();
            Term current(field_num, text);
            ti.doc_freq = tis_->ReadVInt();
            ti.freq_pointer = tis_->ReadVLong();
            ti.prox_pointer = tis_->ReadVLong();

            if (current == target) {
                return ti;
            }

            if (!(current < target)) {
                TermInfo not_found;
                not_found.doc_freq = 0;
                return not_found;
            }
        } catch (...) {
            break;
        }
    }

    TermInfo not_found;
    not_found.doc_freq = 0;
    return not_found;
}

void TermInfosReader::Close() {
    if (tis_) {
        tis_->Close();
        tis_.reset();
    }
}

}  // namespace index
}  // namespace minilucene
