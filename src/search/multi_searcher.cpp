#include "minilucene/search/multi_searcher.h"
#include "minilucene/document/document.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/index/term_docs.h"
#include "minilucene/search/hits.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/query.h"
#include "minilucene/search/scorer.h"

#include <algorithm>
#include <vector>

namespace minilucene {
namespace search {

MultiSearcher::MultiSearcher() {}

void MultiSearcher::AddSearcher(std::unique_ptr<Searcher> searcher) {
    searchers_.push_back(std::move(searcher));
    doc_starts_.push_back(total_docs_);
    if (auto* is = dynamic_cast<IndexSearcher*>(searchers_.back().get())) {
        total_docs_ += is->MaxDoc();
    }
}

std::unique_ptr<Hits> MultiSearcher::Search(const Query& query) {
    if (searchers_.empty()) return nullptr;

    // Per-sub-searcher hit lists, kept aligned with searchers_ so the
    // fetcher can resolve a global id back to (sub, local) and call the
    // right Document().
    std::vector<int>   global_ids;
    std::vector<float> scores;
    std::vector<int>   local_ids;  // parallel to global_ids
    std::vector<int>   sub_index;  // parallel to global_ids

    for (size_t i = 0; i < searchers_.size(); ++i) {
        auto h = searchers_[i]->Search(query);
        if (!h) continue;
        for (int j = 0; j < h->Length(); ++j) {
            int local = h->Id(j);
            global_ids.push_back(local + doc_starts_[i]);
            scores.push_back(h->Score(j));
            local_ids.push_back(local);
            sub_index.push_back(static_cast<int>(i));
        }
    }

    if (global_ids.empty()) {
        return std::make_unique<Hits>(
            Hits::DocFetcher{}, 0,
            std::vector<int>{}, std::vector<float>{});
    }

    // Sort by score descending, with global doc id as deterministic tiebreaker.
    std::vector<size_t> idx(global_ids.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) {
        if (scores[a] != scores[b]) return scores[a] > scores[b];
        return global_ids[a] < global_ids[b];
    });

    std::vector<int>   sorted_global(idx.size());
    std::vector<float> sorted_scores(idx.size());
    std::vector<int>   sorted_local(idx.size());
    std::vector<int>   sorted_sub(idx.size());
    for (size_t i = 0; i < idx.size(); ++i) {
        sorted_global[i] = global_ids[idx[i]];
        sorted_scores[i] = scores[idx[i]];
        sorted_local[i]  = local_ids[idx[i]];
        sorted_sub[i]    = sub_index[idx[i]];
    }

    // The fetcher captures a non-owning pointer to searchers_ + the
    // sorted (global, local, sub) parallel vectors. MultiSearcher MUST
    // outlive the returned Hits (same lifetime contract as the single-
    // reader Hits ctor).
    auto* searchers_ptr = &searchers_;
    auto search_ids = sorted_global;  // copy for capture
    auto fetcher = [searchers_ptr,
                    search_ids,
                    sorted_local,
                    sorted_sub](int global_id) -> std::unique_ptr<document::Document> {
        auto it = std::find(search_ids.begin(), search_ids.end(), global_id);
        if (it == search_ids.end()) return nullptr;
        size_t pos = static_cast<size_t>(it - search_ids.begin());
        int sub   = sorted_sub[pos];
        int local = sorted_local[pos];
        auto* is = dynamic_cast<IndexSearcher*>((*searchers_ptr)[sub].get());
        if (!is) return nullptr;
        return is->Document(local);
    };

    int total = static_cast<int>(sorted_global.size());
    return std::make_unique<Hits>(
        std::move(fetcher),
        total,
        std::move(sorted_global),
        std::move(sorted_scores));
}

void MultiSearcher::Close() {
    for (auto& s : searchers_) s->Close();
    searchers_.clear();
}

}  // namespace search
}  // namespace minilucene
