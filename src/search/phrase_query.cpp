#include "minilucene/search/phrase_query.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/term_positions.h"
#include "minilucene/search/scorer.h"
#include "minilucene/search/similarity.h"

#include <algorithm>
#include <climits>
#include <vector>

namespace minilucene {
namespace search {

namespace {

class PhraseScorer : public Scorer {
public:
    PhraseScorer(std::vector<std::unique_ptr<index::TermPositions>> tps,
                 float idf_sum, index::IndexReader& reader,
                 int field_number, int slop)
        : idf_sum_(idf_sum)
        , reader_(reader), field_number_(field_number), slop_(slop) {
        for (auto& tp : tps) {
            if (!tp->Next()) continue;
            tps_.push_back(std::move(tp));
        }
    }

    bool Next() override {
        while (true) {
            if (tps_.size() < 2) return false;

            int target = FindTarget();
            if (target < 0) return false;

            if (!AlignAll(target)) continue;
            if (!AllMatch(target)) continue;

            CollectPositions(target);
            int matches = CountMatches();
            if (matches > 0) {
                current_doc_ = target;
                freq_ = matches;
                return true;
            }

            AdvancePast(target);
        }
    }

    int Doc() const override { return current_doc_; }

    float Score() override {
        Similarity sim;
        float tf = sim.Tf(freq_);
        float norm = reader_.Norm(current_doc_, field_number_);
        return tf * idf_sum_ * idf_sum_ * norm;
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
            try {
                for (int j = 0; j < tps_[i]->Freq(); ++j) {
                    all_pos_[i].push_back(tps_[i]->NextPosition());
                }
            } catch (...) {
                break;
            }
        }
    }

    int CountMatches() {
        if (all_pos_.empty()) return 0;
        int m = 0;
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
                for (size_t i = 1; i < all_pos_.size(); ++i) {
                    for (int p : all_pos_[i]) {
                        if (std::abs(p - p0 - static_cast<int>(i)) <= slop_) {
                            ++m; break;
                        }
                    }
                }
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
    int current_doc_ = -1;
    int freq_ = 0;
};

}  // namespace

void PhraseQuery::Add(const index::Term& term) {
    terms_.push_back(term);
}

std::unique_ptr<Scorer> PhraseQuery::CreateScorer(index::IndexReader& reader) const {
    if (terms_.empty()) return nullptr;

    std::vector<std::unique_ptr<index::TermPositions>> tps;
    float idf_sum = 0.0f;
    Similarity sim;

    for (const auto& term : terms_) {
        auto tp = reader.Positions(term);
        if (!tp) return nullptr;
        int doc_freq = reader.DocFreq(term);
        idf_sum += sim.Idf(doc_freq, reader.NumDocs());
        tps.push_back(std::move(tp));
    }

    if (tps.empty()) return nullptr;
    int field_number = terms_[0].FieldNumber();

    return std::make_unique<PhraseScorer>(
        std::move(tps), idf_sum, reader, field_number, slop_);
}

std::string PhraseQuery::ToString() const {
    std::string result = "\"";
    for (size_t i = 0; i < terms_.size(); ++i) {
        if (i > 0) result += " ";
        result += terms_[i].Text();
    }
    result += "\"";
    return result;
}

}  // namespace search
}  // namespace minilucene
