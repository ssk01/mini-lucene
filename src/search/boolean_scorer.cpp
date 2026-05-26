#include "minilucene/search/boolean_scorer.h"
#include "minilucene/search/similarity.h"

#include <algorithm>
#include <climits>

namespace minilucene {
namespace search {

BooleanScorer::BooleanScorer(std::vector<std::unique_ptr<Scorer>> must,
                             std::vector<std::unique_ptr<Scorer>> should,
                             std::vector<std::unique_ptr<Scorer>> must_not,
                             int overlap_max, float boost)
    : must_(std::move(must))
    , should_(std::move(should))
    , must_not_(std::move(must_not))
    , overlap_max_(overlap_max)
    , boost_(boost) {
    for (auto& m : must_) { if (m && !m->Next()) m.reset(); }
    for (auto& s : should_) { if (s && !s->Next()) s.reset(); }
    for (auto& mn : must_not_) { if (mn && !mn->Next()) mn.reset(); }
}

bool BooleanScorer::Next() {
    if (current_doc_ >= 0) {
        AdvanceMustPast(current_doc_);
        for (auto& s : should_) {
            if (s && s->Doc() <= current_doc_) {
                while (s->Doc() <= current_doc_) {
                    if (!s->Next()) { s.reset(); break; }
                }
            }
        }
        current_doc_ = -1;
    }
    
    // Check if any must clauses existed at construction time
    bool has_must_originally = false;
    for (auto& m : must_) { (void)m; has_must_originally = true; break; }

    while (true) {
        if (has_must_originally) {
            bool any_active = false;
            for (auto& m : must_) if (m) { any_active = true; break; }
            if (!any_active) return false;
        }

        int target = FindTarget();
        if (target < 0) return false;

        for (auto& m : must_) {
            if (!m) continue;
            while (m->Doc() < target) {
                if (!m->Next()) { m.reset(); break; }
            }
        }

        if (has_must_originally) {
            // The previous `continue` here was buggy: it `continue`d the
            // inner range-for, not the outer while(true), so a misaligned
            // MUST scorer would fall through to the overlap/return path
            // and emit a doc that doesn't actually satisfy every MUST.
            // Fix: break out and re-loop.
            bool need_retry = false;
            for (auto& m : must_) {
                if (!m) return false;  // exhausted MUST -> no more matches
                if (m->Doc() != target) {
                    for (auto& am : must_) {
                        if (am && am->Doc() == target) {
                            if (!am->Next()) am.reset();
                        }
                    }
                    need_retry = true;
                    break;
                }
            }
            if (need_retry) continue;  // restart the outer while(true)
        }

        if (MustNotMatch(target)) {
            AdvanceMustPast(target);
            for (auto& s : should_) {
                if (s && s->Doc() == target) {
                    if (!s->Next()) s.reset();
                }
            }
            continue;
        }

        AlignShould(target);

        overlap_ = 0;
        for (auto& m : must_) if (m) ++overlap_;
        for (auto& s : should_) if (s && s->Doc() == target) ++overlap_;

        if (overlap_ > 0) {
            current_doc_ = target;
            return true;
        }

        AdvanceMustPast(target);
    }
}

float BooleanScorer::Score() {
    Similarity sim;
    float coord = sim.Coord(overlap_, overlap_max_);
    float total = 0.0f;
    for (auto& m : must_) {
        if (m && m->Doc() == current_doc_) total += m->Score();
    }
    for (auto& s : should_) {
        if (s && s->Doc() == current_doc_) total += s->Score();
    }
    return coord * total * boost_;
}

int BooleanScorer::FindTarget() {
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

bool BooleanScorer::AlignMust(int target) {
    for (auto& m : must_) {
        if (!m) continue;
        while (m->Doc() < target) {
            if (!m->Next()) { m.reset(); break; }
        }
    }
    for (auto& m : must_) if (m) return true;
    return false;
}

bool BooleanScorer::AllMustAt(int target) {
    for (auto& m : must_) {
        if (m && m->Doc() != target) return false;
    }
    return true;
}

int BooleanScorer::CountMust() {
    int c = 0;
    for (auto& m : must_) if (m) ++c;
    return c;
}

int BooleanScorer::CountShouldAt(int target) {
    int c = 0;
    for (auto& s : should_) if (s && s->Doc() == target) ++c;
    return c;
}

bool BooleanScorer::MustNotMatch(int target) {
    for (auto& mn : must_not_) {
        if (!mn) continue;
        while (mn->Doc() < target) {
            if (!mn->Next()) { mn.reset(); break; }
        }
        if (mn && mn->Doc() == target) return true;
    }
    return false;
}

void BooleanScorer::AdvanceMustPast(int target) {
    for (auto& m : must_) {
        if (m && m->Doc() == target) {
            if (!m->Next()) m.reset();
        }
    }
}

void BooleanScorer::AlignShould(int target) {
    for (auto& s : should_) {
        if (!s) continue;
        while (s->Doc() < target) {
            if (!s->Next()) { s.reset(); break; }
        }
    }
}

}  // namespace search
}  // namespace minilucene
