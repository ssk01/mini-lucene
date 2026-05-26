#include "minilucene/search/phrase_query.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/term_positions.h"
#include "minilucene/search/scorer.h"
#include "minilucene/search/similarity.h"

#include <algorithm>
#include <climits>
#include <stdexcept>
#include <vector>

namespace minilucene {
namespace search {

namespace {

class PhraseScorer : public Scorer {
public:
    PhraseScorer(std::vector<std::unique_ptr<index::TermPositions>> tps,
                 float idf_sum, index::IndexReader& reader,
                 int field_number, int slop, float boost)
        : idf_sum_(idf_sum)
        , reader_(reader), field_number_(field_number), slop_(slop)
        , boost_(boost) {
        for (auto& tp : tps) {
            if (!tp->Next()) continue;
            tps_.push_back(std::move(tp));
        }
    }

    bool Next() override {
        while (true) {
            if (tps_.empty()) return false;
            if (tps_.size() == 1) {
                current_doc_ = tps_[0]->Doc();
                freq_ = tps_[0]->Freq();
                if (!tps_[0]->Next()) tps_.clear();
                return true;
            }

            int target = FindTarget();
            if (target < 0) return false;

            if (!AlignAll(target)) continue;
            if (!AllMatch(target)) continue;

            CollectPositions(target);
            int matches = CountMatches();
            AdvancePast(target);
            if (matches > 0) {
                current_doc_ = target;
                freq_ = matches;
                return true;
            }
        }
    }

    int Doc() const override { return current_doc_; }

    float Score() override {
        Similarity sim;
        float tf = sim.Tf(freq_);
        float norm = reader_.Norm(current_doc_, field_number_);
        return tf * idf_sum_ * idf_sum_ * norm * boost_;
    }

private:
    int FindTarget() {
        int t = tps_[0]->Doc();
        for (size_t i = 1; i < tps_.size(); ++i) {
            if (tps_[i]->Doc() > t) t = tps_[i]->Doc();
        }
        return t;
    }

    bool AlignAll(int target) {
        for (auto& tp : tps_) {
            while (tp->Doc() < target) {
                if (!tp->Next()) return false;
            }
        }
        return true;
    }

    bool AllMatch(int target) {
        for (auto& tp : tps_) {
            if (tp->Doc() != target) return false;
        }
        return true;
    }

    void CollectPositions(int target) {
        all_pos_.resize(tps_.size());
        for (size_t i = 0; i < tps_.size(); ++i) {
            all_pos_[i].clear();
            for (int j = 0; j < tps_[i]->Freq(); ++j) {
                all_pos_[i].push_back(tps_[i]->NextPosition());
            }
        }
    }

    int CountMatches() {
        if (all_pos_.empty()) return 0;
        int m = 0;
        // For each anchor p0 (position of term 0), count ONE phrase
        // instance iff the rest of the phrase can be aligned within the
        // total slop budget. The previous implementation incremented per
        // matching subsequent term, inflating freq by (n_terms - 1) when
        // the whole phrase fit — that's why a doc with the phrase
        // appearing once would score as if it appeared n times.
        for (int p0 : all_pos_[0]) {
            if (slop_ == 0) {
                bool ok = true;
                for (size_t i = 1; i < all_pos_.size(); ++i) {
                    if (std::find(all_pos_[i].begin(), all_pos_[i].end(),
                                  p0 + static_cast<int>(i)) == all_pos_[i].end()) {
                        ok = false; break;
                    }
                }
                if (ok) ++m;
            } else {
                // Greedy per-anchor: for each subsequent term, pick the
                // position closest to the ideal (p0 + i). Sum absolute
                // deviations. If total <= slop, count one phrase instance.
                // (Greedy is exact when positions are independent; matches
                // Lucene 1.0.1's SloppyPhraseScorer semantics for the
                // common 2- and 3-term cases this port targets.)
                int total_deviation = 0;
                for (size_t i = 1; i < all_pos_.size(); ++i) {
                    int target = p0 + static_cast<int>(i);
                    int best = -1;
                    for (int p : all_pos_[i]) {
                        int dev = std::abs(p - target);
                        if (best < 0 || dev < best) best = dev;
                    }
                    if (best < 0) { total_deviation = slop_ + 1; break; }
                    total_deviation += best;
                    if (total_deviation > slop_) break;
                }
                if (total_deviation <= slop_) ++m;
            }
        }
        return m;
    }

    void AdvancePast(int target) {
        for (auto& tp : tps_) {
            if (tp->Doc() == target) {
                if (!tp->Next()) tp.reset();
            }
        }
        tps_.erase(std::remove(tps_.begin(), tps_.end(), nullptr), tps_.end());
    }

    std::vector<std::unique_ptr<index::TermPositions>> tps_;
    std::vector<std::vector<int>> all_pos_;
    float idf_sum_;
    index::IndexReader& reader_;
    int field_number_;
    int slop_;
    float boost_;
    int current_doc_ = -1;
    int freq_ = 0;
};

}  // namespace

void PhraseQuery::Add(const index::Term& term) {
    if (!terms_.empty() &&
        term.FieldNumber() != terms_.front().FieldNumber()) {
        throw std::invalid_argument(
            "PhraseQuery::Add: all terms must share the same field number. "
            "Mixing fields in a phrase is not supported by Lucene 1.0.1's "
            "PhraseScorer (positions are stored per-field, so cross-field "
            "position deltas are meaningless).");
    }
    terms_.push_back(term);
}

std::unique_ptr<Scorer> PhraseQuery::CreateScorer(index::IndexReader& reader) const {
    if (terms_.empty()) return nullptr;

    std::vector<std::unique_ptr<index::TermPositions>> tps;
    float idf_sum = 0.0f;
    Similarity sim;

    for (const auto& term : terms_) {
        int doc_freq = reader.DocFreq(term);
        if (doc_freq == 0) {
            // A required phrase term is absent from the index — the whole
            // phrase is unmatchable. Bail before building a doomed scorer.
            return nullptr;
        }
        auto tp = reader.Positions(term);
        if (!tp) return nullptr;
        idf_sum += sim.Idf(doc_freq, reader.NumDocs());
        tps.push_back(std::move(tp));
    }

    if (tps.empty()) return nullptr;
    int field_number = terms_[0].FieldNumber();

    return std::make_unique<PhraseScorer>(
        std::move(tps), idf_sum, reader, field_number, slop_, boost_);
}

std::string PhraseQuery::ToString() const {
    std::string result;
    if (!terms_.empty()) {
        result = FieldDisplay(terms_[0].FieldNumber()) + ":";
    }
    result += "\"";
    for (size_t i = 0; i < terms_.size(); ++i) {
        if (i > 0) result += " ";
        result += terms_[i].Text();
    }
    result += "\"";
    if (slop_ != 0) result += "~" + std::to_string(slop_);
    if (boost_ != 1.0f) result += "^" + std::to_string(boost_);
    return result;
}

}  // namespace search
}  // namespace minilucene
