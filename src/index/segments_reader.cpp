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
    int acc = 0;
    for (const auto& si : seg_infos->Segments()) {
        doc_starts_.push_back(acc);
        acc += si.doc_count;
        readers_.push_back(std::make_unique<SegmentReader>(dir, si.name));
    }
}

SegmentsReader::~SegmentsReader() { Close(); }

int SegmentsReader::NumDocs() const {
    int count = 0;
    for (auto& r : readers_) count += r->NumDocs();
    return count;
}

int SegmentsReader::MaxDoc() const {
    int count = 0;
    for (auto& r : readers_) count += r->MaxDoc();
    return count;
}

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

std::unique_ptr<TermEnum> SegmentsReader::Terms(const Term& term) {
    // Was: forwarded only to readers_[0], dropping any prefix-matching
    // terms living in segments 1..N. PrefixQuery iterates this enum to
    // collect candidate terms, so the bug surfaced as "PrefixQuery misses
    // hits whose terms are not in the first segment."
    //
    // Fix: union every segment's Terms(term) starting point, then dedupe
    // and sort so the enumeration is monotone (PrefixQuery walks while
    // term.Text().starts_with(prefix), which requires sorted order).
    struct SetEnum : TermEnum {
        std::vector<Term> terms_;
        std::vector<int>  doc_freqs_;
        int pos_ = -1;
        bool Next() override {
            ++pos_;
            return pos_ < static_cast<int>(terms_.size());
        }
        const Term& Current() const override { return terms_[pos_]; }
        int DocFreq() const override {
            if (pos_ < 0 || pos_ >= static_cast<int>(doc_freqs_.size())) return 0;
            return doc_freqs_[pos_];
        }
        void Close() override {}
    };

    if (readers_.empty()) return nullptr;

    std::map<Term, int> merged;  // map gives sorted iteration + dedup
    for (auto& r : readers_) {
        auto te = r->Terms(term);
        if (!te) continue;
        while (te->Next()) {
            const Term& cur = te->Current();
            // Stop scanning this segment once we pass the field — saves
            // walking unrelated suffix terms in the same segment.
            if (cur.FieldNumber() != term.FieldNumber()) break;
            merged[cur] += r->DocFreq(cur);
        }
        te->Close();
    }

    auto result = std::make_unique<SetEnum>();
    for (auto& [t, df] : merged) {
        result->terms_.push_back(t);
        result->doc_freqs_.push_back(df);
    }
    return result;
}

int SegmentsReader::SegIdx(int doc_id) const {
    // Underflow guard: readers_.size() - 1 wraps to SIZE_MAX when empty,
    // which spun SegIdx forever and OOB-indexed doc_starts_ in the
    // process. Defined behavior on an empty reader: return -1 so callers
    // can early-out instead of dereferencing a nonexistent segment.
    if (readers_.empty()) return -1;
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
        std::vector<int> doc_ids, freqs, positions, pos_starts;
        size_t pos = 0;
        int doc_ = -1, freq_ = 0;

        bool Next() override {
            if (pos >= doc_ids.size()) return false;
            doc_ = doc_ids[pos];
            freq_ = freqs[pos];
            ++pos;
            return true;
        }
        int Doc() const override { return doc_; }
        int Freq() const override { return freq_; }
        int NextPosition() override {
            return positions[pos_starts[pos - 1] + freqs[pos - 1] - (freq_--)];
        }
        void Close() override {}
    };

    auto result = std::make_unique<SimplePositions>();
    int pos_offset = 0;
    for (size_t si = 0; si < readers_.size(); ++si) {
        auto tp = readers_[si]->Positions(term);
        if (!tp) continue;
        while (tp->Next()) {
            result->doc_ids.push_back(tp->Doc() + doc_starts_[si]);
            result->freqs.push_back(tp->Freq());
            result->pos_starts.push_back(pos_offset);
            for (int i = 0; i < tp->Freq(); ++i) {
                result->positions.push_back(tp->NextPosition());
                ++pos_offset;
            }
        }
    }
    return result;
}

float SegmentsReader::Norm(int doc, int field_number) {
    int si = SegIdx(doc);
    if (si < 0) return 0.0f;
    return readers_[si]->Norm(LocalDoc(doc, si), field_number);
}

std::unique_ptr<document::Document> SegmentsReader::Document(int doc_id) {
    int si = SegIdx(doc_id);
    if (si < 0) return nullptr;
    return readers_[si]->Document(LocalDoc(doc_id, si));
}

void SegmentsReader::Delete(int doc_id) {
    int si = SegIdx(doc_id);
    if (si < 0) return;
    readers_[si]->Delete(LocalDoc(doc_id, si));
}

bool SegmentsReader::IsDeleted(int doc_id) const {
    int si = SegIdx(doc_id);
    if (si < 0) return false;
    return readers_[si]->IsDeleted(LocalDoc(doc_id, si));
}

void SegmentsReader::Close() {
    for (auto& r : readers_) r->Close();
    readers_.clear();
}

}  // namespace index
}  // namespace minilucene
