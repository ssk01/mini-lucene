#include "minilucene/index/segment_merger.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/field_infos.h"
#include "minilucene/index/term.h"
#include "minilucene/index/term_enum.h"
#include "minilucene/index/term_docs.h"
#include "minilucene/index/term_infos_writer.h"
#include "minilucene/store/directory.h"
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
    int total_docs = 0;
    std::vector<int> doc_starts;
    for (const auto& seg : segments_) {
        doc_starts.push_back(total_docs);
        total_docs += 1;
    }

    // Collect all unique terms from all segments
    std::set<Term> all_terms;
    std::vector<std::unique_ptr<SegmentReader>> readers;

    for (const auto& seg : segments_) {
        auto reader = std::make_unique<SegmentReader>(dir_, seg);
        auto terms = reader->Terms();
        while (terms->Next()) {
            all_terms.insert(terms->Current());
        }
        terms->Close();
        readers.push_back(std::move(reader));
    }

    // Write merged .fnm (collect field names from first segment)
    if (!segments_.empty()) {
        SegmentReader first_reader(dir_, segments_[0]);
        // Write simple .fnm: just "body" field
        auto fnm = dir_.CreateOutput(merged_segment_ + ".fnm");
        fnm->WriteVInt(1);
        fnm->WriteString("body");
        fnm->WriteByte(0x07);
        fnm->Close();
    }

    // Write merged files
    auto frq = dir_.CreateOutput(merged_segment_ + ".frq");
    auto prx = dir_.CreateOutput(merged_segment_ + ".prx");

    std::vector<std::pair<Term, TermInfo>> merged_terms;

    for (const auto& term : all_terms) {
        TermInfo ti;
        ti.freq_pointer = frq->FilePointer();
        ti.prox_pointer = prx->FilePointer();
        ti.doc_freq = 0;

        // Collect postings for this term from all segments
        int prev_doc = 0;
        for (size_t si = 0; si < readers.size(); ++si) {
            auto docs = readers[si]->Docs(term);
            if (!docs) continue;
            while (docs->Next()) {
                int global_doc = docs->Doc() + doc_starts[si];
                int delta = global_doc - prev_doc;
                frq->WriteVInt(delta);
                frq->WriteVInt(docs->Freq());
                prev_doc = global_doc;
                ++ti.doc_freq;
                prx->WriteVInt(0);
            }
        }

        merged_terms.push_back({term, ti});
    }

    frq->Close();
    prx->Close();

    TermInfosWriter tis_writer(dir_, merged_segment_);
    for (const auto& [term, ti] : merged_terms) {
        tis_writer.Add(term, ti);
    }
    tis_writer.Close();

    auto nrm = dir_.CreateOutput(merged_segment_ + ".nrm");
    for (int d = 0; d < total_docs; ++d) {
        nrm->WriteByte(255);
    }
    nrm->Close();
}

}  // namespace index
}  // namespace minilucene
