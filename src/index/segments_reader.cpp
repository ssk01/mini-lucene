#include "minilucene/index/segments_reader.h"
#include "minilucene/document/document.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/segment_merge_queue.h"
#include "minilucene/index/term_enum.h"
#include "minilucene/index/term_docs.h"
#include "minilucene/index/term_positions.h"
#include "minilucene/store/directory.h"
#include <map>

namespace minilucene {
namespace index {

SegmentsReader::SegmentsReader(store::Directory& dir) {
    auto seg_infos = SegmentInfos::Read(dir);
    for (const auto& si : seg_infos->Segments()) {
        doc_starts_.push_back(total_docs_);
        total_docs_ += si.doc_count;
        readers_.push_back(std::make_unique<SegmentReader>(dir, si.name));
    }
}

SegmentsReader::~SegmentsReader() { Close(); }

std::unique_ptr<TermEnum> SegmentsReader::Terms() {
    struct SetEnum : TermEnum {
        std::vector<Term> terms_;
        int pos_ = -1;
        bool Next() override {
            ++pos_;
            return pos_ < static_cast<int>(terms_.size());
        }
        const Term& Current() const override { return terms_[pos_]; }
        int DocFreq() const override { return 0; }
        void Close() override {}
    };

    auto result = std::make_unique<SetEnum>();
    std::map<Term, int> all_terms;
    for (auto& r : readers_) {
        auto te = r->Terms();
        while (te->Next()) {
            all_terms[te->Current()] += r->DocFreq(te->Current());
        }
        te->Close();
    }
    for (auto& [t, df] : all_terms) {
        (void)df;
        result->terms_.push_back(t);
    }
    return result;
}

int SegmentsReader::SegIdx(int doc_id) const {
    for (size_t i = readers_.size() - 1; i > 0; --i) {
        if (doc_id >= doc_starts_[i]) return static_cast<int>(i);
    }
    return 0;
}

int SegmentsReader::LocalDoc(int doc_id, int seg_idx) const {
    return doc_id - doc_starts_[seg_idx];
}

int SegmentsReader::DocFreq(const Term& term) {
    int total = 0;
    for (auto& r : readers_) total += r->DocFreq(term);
    return total;
}

std::unique_ptr<TermDocs> SegmentsReader::Docs(const Term& term) {
    // Collect TermDocs from all segments with global doc IDs
    std::vector<std::pair<int, std::unique_ptr<TermDocs>>> all;
    for (size_t si = 0; si < readers_.size(); ++si) {
        auto docs = readers_[si]->Docs(term);
        if (!docs) continue;
        while (docs->Next()) {
            all.push_back({docs->Doc() + doc_starts_[si], nullptr});
        }
    }
    // For simplicity, use a simple in-memory structure
    struct SimpleDocs : TermDocs {
        std::vector<std::pair<int, int>> entries;
        size_t pos = 0;
        int doc_ = -1, freq_ = 0;
        bool Next() override {
            if (pos >= entries.size()) return false;
            doc_ = entries[pos].first;
            freq_ = entries[pos].second;
            ++pos;
            return true;
        }
        int Doc() const override { return doc_; }
        int Freq() const override { return freq_; }
        void Close() override {}
    };

    auto result = std::make_unique<SimpleDocs>();
    for (size_t si = 0; si < readers_.size(); ++si) {
        auto docs = readers_[si]->Docs(term);
        if (!docs) continue;
        while (docs->Next()) {
            result->entries.push_back({docs->Doc() + doc_starts_[si], docs->Freq()});
        }
    }
    std::sort(result->entries.begin(), result->entries.end());
    return result;
}

std::unique_ptr<TermPositions> SegmentsReader::Positions(const Term& term) {
    struct SimplePositions : TermPositions {
        std::vector<int> doc_ids, freqs, positions;
        size_t pos = 0;
        size_t ppos = 0;
        int doc_ = -1, freq_ = 0;
        bool Next() override {
            if (pos >= doc_ids.size()) return false;
            doc_ = doc_ids[pos];
            freq_ = freqs[pos];
            ppos = 0;
            ++pos;
            return true;
        }
        int Doc() const override { return doc_; }
        int Freq() const override { return freq_; }
        int NextPosition() override {
            return positions[ppos++];
        }
        void Close() override {}
    };

    auto result = std::make_unique<SimplePositions>();
    for (size_t si = 0; si < readers_.size(); ++si) {
        auto tp = readers_[si]->Positions(term);
        if (!tp) continue;
        while (tp->Next()) {
            result->doc_ids.push_back(tp->Doc() + doc_starts_[si]);
            result->freqs.push_back(tp->Freq());
            for (int i = 0; i < tp->Freq(); ++i) {
                result->positions.push_back(tp->NextPosition());
            }
        }
    }
    return result;
}

float SegmentsReader::Norm(int doc, int field_number) {
    int si = SegIdx(doc);
    return readers_[si]->Norm(LocalDoc(doc, si), field_number);
}

std::unique_ptr<document::Document> SegmentsReader::Document(int doc_id) {
    int si = SegIdx(doc_id);
    return readers_[si]->Document(LocalDoc(doc_id, si));
}

void SegmentsReader::Delete(int doc_id) {
    int si = SegIdx(doc_id);
    readers_[si]->Delete(LocalDoc(doc_id, si));
}

void SegmentsReader::Close() {
    for (auto& r : readers_) r->Close();
    readers_.clear();
}

}  // namespace index
}  // namespace minilucene
