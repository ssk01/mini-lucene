#include "minilucene/search/multi_searcher.h"
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
    std::vector<int> all_docs;
    std::vector<float> all_scores;

    for (size_t i = 0; i < searchers_.size(); ++i) {
        auto hits = searchers_[i]->Search(query);
        if (!hits) continue;
        for (int j = 0; j < hits->Length(); ++j) {
            all_docs.push_back(hits->Id(j) + doc_starts_[i]);
            all_scores.push_back(hits->Score(j));
        }
    }

    // Sort by score descending
    std::vector<size_t> idx(all_docs.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(),
        [&](size_t a, size_t b) { return all_scores[a] > all_scores[b]; });

    std::vector<int> sorted_docs;
    std::vector<float> sorted_scores;
    for (auto i : idx) {
        sorted_docs.push_back(all_docs[i]);
        sorted_scores.push_back(all_scores[i]);
    }

    // MultiSearcher doesn't have a single IndexReader for Hits
    // Return nullptr until proper multi-reader support is added
    (void)sorted_docs; (void)sorted_scores;
    return nullptr;
}

void MultiSearcher::Close() {
    for (auto& s : searchers_) s->Close();
    searchers_.clear();
}

}  // namespace search
}  // namespace minilucene
