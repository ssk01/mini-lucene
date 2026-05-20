#include "minilucene/search/boolean_query.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/search/scorer.h"
#include "minilucene/search/similarity.h"

#include <algorithm>
#include <climits>

namespace minilucene {
namespace search {

namespace {

class BooleanScorer : public Scorer {
public:
    BooleanScorer(std::vector<std::unique_ptr<Scorer>> must,
                  std::vector<std::unique_ptr<Scorer>> should,
                  std::vector<std::unique_ptr<Scorer>> must_not,
                  int overlap_max)
        : must_(std::move(must))
        , should_(std::move(should))
        , must_not_(std::move(must_not))
        , overlap_max_(overlap_max) {
        for (auto& m : must_) {
            if (!m->Next()) m.reset();
        }
        for (auto& s : should_) {
            if (!s->Next()) s.reset();
        }
        for (auto& mn : must_not_) {
            if (!mn->Next()) mn.reset();
        }
    }

    bool Next() override {
        while (true) {
            int target = FindTarget();
            if (target < 0) return false;

            if (!AlignMust(target)) return false;
            if (HasMust() && !AllMustAt(target)) continue;

            if (MustNotAt(target)) continue;

            AlignShoulTo(target);
            overlap_ = CountMust() + CountShouldAt(target);

            if (overlap_ > 0) {
                current_doc_ = target;
                return true;
            }

            AdvanceMustPast(target);
        }
    }

    int Doc() const override { return current_doc_; }

    float Score() override {
        Similarity sim;
        float coord = sim.Coord(overlap_, overlap_max_);
        float total = 0.0f;
        for (auto& m : must_) {
            if (m && m->Doc() == current_doc_) total += m->Score();
        }
        for (auto& s : should_) {
            if (s && s->Doc() == current_doc_) total += s->Score();
        }
        return coord * total;
    }

private:
    int FindTarget() {
        if (HasMust()) return MaxMustDoc();
        if (HasShould()) return MinShouldDoc();
        return -1;
    }

    bool HasMust() const {
        for (auto& m : must_) if (m) return true;
        return false;
    }

    bool HasShould() const {
        for (auto& s : should_) if (s) return true;
        return false;
    }

    int MaxMustDoc() {
        for (auto& m : must_) if (m) return m->Doc();
        return -1;
    }

    int MinShouldDoc() {
        int min = INT_MAX;
        for (auto& s : should_) {
            if (s && s->Doc() < min) min = s->Doc();
        }
        return (min == INT_MAX) ? -1 : min;
    }

    bool AlignMust(int target) {
        for (auto& m : must_) {
            if (!m) continue;
            while (m->Doc() < target) {
                if (!m->Next()) { m.reset(); break; }
            }
        }
        return HasMust();
    }

    bool AllMustAt(int target) {
        for (auto& m : must_) {
            if (m && m->Doc() != target) return false;
        }
        return true;
    }

    int CountMust() {
        int c = 0;
        for (auto& m : must_) if (m) ++c;
        return c;
    }

    int CountShouldAt(int target) {
        int c = 0;
        for (auto& s : should_) if (s && s->Doc() == target) ++c;
        return c;
    }

    bool MustNotAt(int target) {
        for (auto& mn : must_not_) {
            if (!mn) continue;
            while (mn->Doc() < target) {
                if (!mn->Next()) { mn.reset(); break; }
            }
            if (mn && mn->Doc() == target) return true;
        }
        return false;
    }

    void AdvanceMustPast(int target) {
        for (auto& m : must_) {
            if (m && m->Doc() == target) {
                if (!m->Next()) m.reset();
            }
        }
    }

    void AlignShoulTo(int target) {
        for (auto& s : should_) {
            if (!s) continue;
            while (s->Doc() < target) {
                if (!s->Next()) { s.reset(); break; }
            }
        }
    }

    std::vector<std::unique_ptr<Scorer>> must_;
    std::vector<std::unique_ptr<Scorer>> should_;
    std::vector<std::unique_ptr<Scorer>> must_not_;
    Similarity similarity_;
    int current_doc_ = -1;
    int overlap_ = 0;
    int overlap_max_;
};

}  // namespace

void BooleanQuery::Add(std::unique_ptr<Query> q, Occur occur) {
    if (clauses_.size() >= static_cast<size_t>(MAX_CLAUSE_COUNT)) {
        throw TooManyClauses();
    }
    clauses_.push_back({std::move(q), occur});
}

std::unique_ptr<Scorer> BooleanQuery::CreateScorer(index::IndexReader& reader) const {
    std::vector<std::unique_ptr<Scorer>> must;
    std::vector<std::unique_ptr<Scorer>> should;
    std::vector<std::unique_ptr<Scorer>> must_not;

    for (const auto& clause : clauses_) {
        auto s = clause.query->CreateScorer(reader);
        if (!s) {
            if (clause.occur == Occur::MUST) return nullptr;
            continue;
        }
        switch (clause.occur) {
            case Occur::MUST:    must.push_back(std::move(s)); break;
            case Occur::SHOULD:  should.push_back(std::move(s)); break;
            case Occur::MUST_NOT: must_not.push_back(std::move(s)); break;
        }
    }

    if (must.empty() && should.empty()) return nullptr;

    int overlap_max = static_cast<int>(must.size() + should.size());

    return std::make_unique<BooleanScorer>(
        std::move(must), std::move(should), std::move(must_not),
        overlap_max);
}

std::string BooleanQuery::ToString() const {
    std::string result;
    for (size_t i = 0; i < clauses_.size(); ++i) {
        if (i > 0) result += " ";
        const auto& c = clauses_[i];
        switch (c.occur) {
            case Occur::MUST:    result += "+"; break;
            case Occur::MUST_NOT: result += "-"; break;
            default: break;
        }
        result += c.query->ToString();
    }
    return result;
}

}  // namespace search
}  // namespace minilucene
