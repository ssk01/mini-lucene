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
        : has_must_(!must.empty())
        , must_(std::move(must))
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
        if (current_doc_ >= 0) {
            AdvancePast(must_, current_doc_);
            for (auto& s : should_) {
                if (s && s->Doc() <= current_doc_) {
                    while (s->Doc() <= current_doc_) {
                        if (!s->Next()) { s.reset(); break; }
                    }
                }
            }
            current_doc_ = -1;
        }
        while (true) {
            if (has_must_ && !HasActive(must_)) return false;

            int target = FindTarget();
            if (target < 0) return false;

            AdvanceAllTo(must_, target);

            bool has_must_now = HasActive(must_);
            if (has_must_now && !AllAt(must_, target)) continue;

            if (MustNotMatch(target)) continue;

            AdvanceAllTo(should_, target);
            overlap_ = CountActive(must_) + CountAt(should_, target);

            if (overlap_ > 0) {
                current_doc_ = target;
                return true;
            }

            AdvancePast(must_, target);
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
        int max_must = -1;
        for (auto& m : must_) {
            if (m && m->Doc() > max_must) max_must = m->Doc();
        }
        if (max_must >= 0) return max_must;

        int min_should = INT_MAX;
        for (auto& s : should_) {
            if (s && s->Doc() < min_should) min_should = s->Doc();
        }
        return (min_should < INT_MAX) ? min_should : -1;
    }

    bool HasActive(const std::vector<std::unique_ptr<Scorer>>& v) {
        for (auto& s : v) if (s) return true;
        return false;
    }

    void AdvanceAllTo(std::vector<std::unique_ptr<Scorer>>& v, int target) {
        for (auto& s : v) {
            if (!s) continue;
            while (s->Doc() < target) {
                if (!s->Next()) { s.reset(); break; }
            }
        }
    }

    bool AllAt(std::vector<std::unique_ptr<Scorer>>& v, int target) {
        for (auto& s : v) {
            if (s && s->Doc() != target) return false;
        }
        return true;
    }

    int CountActive(const std::vector<std::unique_ptr<Scorer>>& v) {
        int c = 0;
        for (auto& s : v) if (s) ++c;
        return c;
    }

    int CountAt(const std::vector<std::unique_ptr<Scorer>>& v, int target) {
        int c = 0;
        for (auto& s : v) if (s && s->Doc() == target) ++c;
        return c;
    }

    bool MustNotMatch(int target) {
        for (auto& mn : must_not_) {
            if (!mn) continue;
            while (mn->Doc() < target) {
                if (!mn->Next()) { mn.reset(); break; }
            }
            if (mn && mn->Doc() == target) return true;
        }
        return false;
    }

    void AdvancePast(std::vector<std::unique_ptr<Scorer>>& v, int target) {
        for (auto& s : v) {
            if (s && s->Doc() == target) {
                if (!s->Next()) s.reset();
            }
        }
    }

    bool has_must_;
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
