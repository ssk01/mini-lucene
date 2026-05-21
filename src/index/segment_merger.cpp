#include "minilucene/index/segment_merger.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/field_infos.h"
#include "minilucene/index/fields_writer.h"
#include "minilucene/index/fields_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/index/term_enum.h"
#include "minilucene/index/term_docs.h"
#include "minilucene/index/term_positions.h"
#include "minilucene/index/term_infos_writer.h"
#include "minilucene/document/document.h"
#include "minilucene/store/directory.h"
#include "minilucene/store/index_input.h"
#include "minilucene/store/index_output.h"

#include <map>
#include <set>
#include <vector>

namespace minilucene {
namespace index {

SegmentMerger::SegmentMerger(store::Directory& dir,
                             const std::vector<std::string>& segments,
                             const std::string& merged_segment)
    : dir_(dir), segments_(segments), merged_segment_(merged_segment) {}

void SegmentMerger::Merge() {
    std::vector<std::unique_ptr<SegmentReader>> readers;
    std::unique_ptr<FieldInfos> merged_fis;

    for (const auto& seg : segments_) {
        auto r = std::make_unique<SegmentReader>(dir_, seg);
        if (!merged_fis) merged_fis = FieldInfos::Read(dir_, seg);
        readers.push_back(std::move(r));
    }
    if (!merged_fis) return;

    merged_fis->Write(dir_, merged_segment_);

    // Build old→new doc ID mapping per segment (skip deleted docs)
    std::vector<std::vector<int>> seg_doc_map;
    int total_live_docs = 0;
    for (auto& r : readers) {
        std::vector<int> mapping;
        for (int d = 0; d < r->MaxDoc(); ++d) {
            mapping.push_back(r->IsDeleted(d) ? -1 : total_live_docs++);
        }
        seg_doc_map.push_back(std::move(mapping));
    }

    // Merge stored fields (.fdt/.fdx), skip deleted docs
    if (dir_.FileExists(segments_[0] + ".fdt")) {
        FieldsWriter fw(dir_, merged_segment_, *merged_fis);
        for (size_t si = 0; si < readers.size(); ++si) {
            FieldsReader fr(dir_, segments_[si], *merged_fis);
            for (int d = 0; d < readers[si]->MaxDoc(); ++d) {
                if (seg_doc_map[si][d] < 0) continue;
                auto doc = fr.Document(d);
                if (doc) fw.AddDocument(*doc);
            }
            fr.Close();
        }
        fw.Close();
    }

    // Collect all unique terms
    std::set<Term> all_terms;
    for (auto& r : readers) {
        auto terms = r->Terms();
        while (terms->Next()) all_terms.insert(terms->Current());
        terms->Close();
    }

    // Write merged .frq, .prx with proper doc delta + position delta encoding
    auto frq = dir_.CreateOutput(merged_segment_ + ".frq");
    auto prx = dir_.CreateOutput(merged_segment_ + ".prx");
    std::vector<std::pair<Term, TermInfo>> merged_terms;

    for (const auto& term : all_terms) {
        TermInfo ti;
        ti.freq_pointer = frq->FilePointer();
        ti.prox_pointer = prx->FilePointer();
        ti.doc_freq = 0;
        int prev_doc = 0;

        for (size_t si = 0; si < readers.size(); ++si) {
            auto tp = readers[si]->Positions(term);
            if (!tp) continue;
            while (tp->Next()) {
                int local_doc = tp->Doc();
                int new_doc = seg_doc_map[si][local_doc];
                if (new_doc < 0) continue;

                frq->WriteVInt(new_doc - prev_doc);
                int freq = tp->Freq();
                frq->WriteVInt(freq);
                prev_doc = new_doc;
                ++ti.doc_freq;

                int prev_pos = 0;
                int abs_pos = 0;
                for (int i = 0; i < freq; ++i) {
                    abs_pos += tp->NextPosition();
                    prx->WriteVInt(abs_pos - prev_pos);
                    prev_pos = abs_pos;
                }
            }
        }
        merged_terms.push_back({term, ti});
    }
    frq->Close();
    prx->Close();

    // Write .tis, .tii
    TermInfosWriter tis_writer(dir_, merged_segment_);
    for (const auto& [term, ti] : merged_terms) tis_writer.Add(term, ti);
    tis_writer.Close();

    // Copy .nrm bytes from source segments for live docs only
    auto nrm = dir_.CreateOutput(merged_segment_ + ".nrm");
    int num_fields = merged_fis->Size();
    for (size_t si = 0; si < readers.size(); ++si) {
        auto& r = readers[si];
        auto src_nrm = dir_.OpenInput(segments_[si] + ".nrm");
        for (int d = 0; d < r->MaxDoc(); ++d) {
            if (r->IsDeleted(d)) continue;
            for (int f = 0; f < num_fields; ++f) {
                src_nrm->Seek((static_cast<int64_t>(d) * num_fields + f));
                nrm->WriteByte(src_nrm->ReadByte());
            }
        }
        src_nrm->Close();
    }
    nrm->Close();
}

}  // namespace index
}  // namespace minilucene
