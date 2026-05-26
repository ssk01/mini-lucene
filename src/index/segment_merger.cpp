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
    if (segments_.empty()) return;

    // Load every source segment + its own FieldInfos. The previous
    // implementation only loaded segment[0]'s FieldInfos and reused it
    // as the merged schema — that lost any field introduced after the
    // first segment, and also OOB-read .nrm/.fdt for any source whose
    // field count or order differed from the first segment.
    std::vector<std::unique_ptr<SegmentReader>> readers;
    std::vector<std::unique_ptr<FieldInfos>>    src_fis;
    readers.reserve(segments_.size());
    src_fis.reserve(segments_.size());
    for (const auto& seg : segments_) {
        readers.push_back(std::make_unique<SegmentReader>(dir_, seg));
        src_fis.push_back(FieldInfos::Read(dir_, seg));
    }

    // Merged schema = union of every source segment's FieldInfos. Field
    // numbers are reassigned in first-seen order (matches Java behavior).
    auto merged_fis = std::make_unique<FieldInfos>();
    for (auto& fi : src_fis) {
        if (fi) merged_fis->Merge(*fi);
    }
    merged_fis->Write(dir_, merged_segment_);
    const int merged_num_fields = merged_fis->Size();

    // Per-source remap: src_field_number -> merged_field_number.
    // Also the inverse (merged -> src or -1 if absent) for .nrm copy.
    std::vector<std::vector<int>> src_to_merged(src_fis.size());
    std::vector<std::vector<int>> merged_to_src(src_fis.size());
    for (size_t si = 0; si < src_fis.size(); ++si) {
        const int sn = src_fis[si] ? src_fis[si]->Size() : 0;
        src_to_merged[si].assign(sn, -1);
        merged_to_src[si].assign(merged_num_fields, -1);
        for (int sf = 0; sf < sn; ++sf) {
            const auto* fi = src_fis[si]->FieldByNumber(sf);
            if (!fi) continue;
            int mf = merged_fis->FieldNumber(fi->Name());
            src_to_merged[si][sf]  = mf;
            if (mf >= 0) merged_to_src[si][mf] = sf;
        }
    }

    // Build old→new global doc ID mapping per segment (skip deletes).
    std::vector<std::vector<int>> seg_doc_map;
    seg_doc_map.reserve(readers.size());
    int total_live_docs = 0;
    for (auto& r : readers) {
        std::vector<int> mapping;
        mapping.reserve(r->MaxDoc());
        for (int d = 0; d < r->MaxDoc(); ++d) {
            mapping.push_back(r->IsDeleted(d) ? -1 : total_live_docs++);
        }
        seg_doc_map.push_back(std::move(mapping));
    }

    // Stored fields (.fdt/.fdx): decode each source with its OWN
    // FieldInfos so the per-doc field-number stream maps to the right
    // names; re-encode through the merged FieldInfos so field numbers
    // are remapped.
    if (dir_.FileExists(segments_[0] + ".fdt")) {
        FieldsWriter fw(dir_, merged_segment_, *merged_fis);
        for (size_t si = 0; si < readers.size(); ++si) {
            if (!src_fis[si]) continue;
            FieldsReader fr(dir_, segments_[si], *src_fis[si]);
            for (int d = 0; d < readers[si]->MaxDoc(); ++d) {
                if (seg_doc_map[si][d] < 0) continue;
                auto doc = fr.Document(d);
                if (doc) fw.AddDocument(*doc);
            }
            fr.Close();
        }
        fw.Close();
    }

    // Collect terms keyed by MERGED field number — sources may have
    // assigned different field numbers to the same field name, so
    // grouping by raw source Term would split postings of one field
    // across two merged entries.
    std::set<Term> all_terms;
    for (size_t si = 0; si < readers.size(); ++si) {
        auto te = readers[si]->Terms();
        while (te->Next()) {
            const Term& src = te->Current();
            if (src.FieldNumber() < 0 ||
                src.FieldNumber() >= static_cast<int>(src_to_merged[si].size())) {
                continue;
            }
            int mf = src_to_merged[si][src.FieldNumber()];
            if (mf < 0) continue;
            all_terms.insert(Term(mf, src.Text()));
        }
        te->Close();
    }

    // Postings: merged order is doc-id-ascending, so iterate by source
    // (sources are already in global-id order via seg_doc_map).
    auto frq = dir_.CreateOutput(merged_segment_ + ".frq");
    auto prx = dir_.CreateOutput(merged_segment_ + ".prx");
    std::vector<std::pair<Term, TermInfo>> merged_terms;
    merged_terms.reserve(all_terms.size());

    for (const auto& term : all_terms) {
        TermInfo ti;
        ti.freq_pointer = frq->FilePointer();
        ti.prox_pointer = prx->FilePointer();
        ti.doc_freq = 0;
        int prev_doc = 0;

        for (size_t si = 0; si < readers.size(); ++si) {
            int src_f = merged_to_src[si][term.FieldNumber()];
            if (src_f < 0) continue;
            Term src_term(src_f, term.Text());
            auto tp = readers[si]->Positions(src_term);
            if (!tp) continue;
            while (tp->Next()) {
                int local_doc = tp->Doc();
                int new_doc = seg_doc_map[si][local_doc];
                if (new_doc < 0) {
                    // Still must drain positions to keep tp aligned for
                    // subsequent docs in this source.
                    for (int i = 0; i < tp->Freq(); ++i) (void)tp->NextPosition();
                    continue;
                }

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

    TermInfosWriter tis_writer(dir_, merged_segment_);
    for (const auto& [term, ti] : merged_terms) tis_writer.Add(term, ti);
    tis_writer.Close();

    // Norms: merged_num_fields bytes per live doc. Source .nrm has
    // src_num_fields bytes per doc, indexed by source's own field
    // numbering. For each merged field f_m, look up the corresponding
    // source field f_s (or -1 if this source lacks that field) and copy
    // the byte from the right offset. Fields absent in source → 0.
    auto nrm = dir_.CreateOutput(merged_segment_ + ".nrm");
    for (size_t si = 0; si < readers.size(); ++si) {
        if (!src_fis[si]) continue;
        const int src_num_fields = src_fis[si]->Size();
        auto src_nrm = dir_.OpenInput(segments_[si] + ".nrm");
        for (int d = 0; d < readers[si]->MaxDoc(); ++d) {
            if (seg_doc_map[si][d] < 0) continue;
            for (int mf = 0; mf < merged_num_fields; ++mf) {
                int sf = merged_to_src[si][mf];
                if (sf < 0) {
                    nrm->WriteByte(0);  // field absent in this source
                } else {
                    src_nrm->Seek(
                        static_cast<int64_t>(d) * src_num_fields + sf);
                    nrm->WriteByte(src_nrm->ReadByte());
                }
            }
        }
        src_nrm->Close();
    }
    nrm->Close();
}

}  // namespace index
}  // namespace minilucene
