#include "minilucene/search/index_searcher.h"
#include "minilucene/document/document.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/segments_reader.h"
#include "minilucene/search/hits.h"
#include "minilucene/search/query.h"
#include "minilucene/search/scorer.h"
#include "minilucene/store/directory.h"
#include "minilucene/store/fs_directory.h"
#include "minilucene/util/priority_queue.h"

#include <algorithm>

namespace minilucene {
namespace search {

namespace {

class HitQueue : public util::PriorityQueue<ScoreDoc> {
public:
    explicit HitQueue(int top_k) { Initialize(top_k); }
protected:
    bool LessThan(const ScoreDoc& a, const ScoreDoc& b) const override {
        return a.score < b.score;
    }
};

}  // namespace

IndexSearcher::IndexSearcher(const std::string& path) {
    owned_dir_ = std::make_unique<store::FSDirectory>(path);
    auto seg_infos = index::SegmentInfos::Read(*owned_dir_);
    if (seg_infos->Segments().empty()) return;
    if (seg_infos->Segments().size() == 1) {
        reader_ = new index::SegmentReader(*owned_dir_, seg_infos->Segments()[0].name);
    } else {
        reader_ = new index::SegmentsReader(*owned_dir_);
    }
    owns_reader_ = true;
}

IndexSearcher::IndexSearcher(store::Directory& dir) {
    auto seg_infos = index::SegmentInfos::Read(dir);
    if (seg_infos->Segments().empty()) return;
    if (seg_infos->Segments().size() == 1) {
        reader_ = new index::SegmentReader(dir, seg_infos->Segments()[0].name);
    } else {
        reader_ = new index::SegmentsReader(dir);
    }
    owns_reader_ = true;
}

IndexSearcher::IndexSearcher(index::IndexReader& reader)
    : reader_(&reader), owns_reader_(false) {}

IndexSearcher::~IndexSearcher() {
    Close();
}

std::unique_ptr<Hits> IndexSearcher::Search(const Query& query) {
    if (!reader_) return nullptr;
    auto scorer = query.CreateScorer(*reader_);
    if (!scorer) {
        return std::make_unique<Hits>(*reader_, 0, std::vector<int>(), std::vector<float>());
    }

    std::vector<int> doc_ids;
    std::vector<float> scores;
    while (scorer->Next()) {
        doc_ids.push_back(scorer->Doc());
        scores.push_back(scorer->Score());
    }

    std::vector<size_t> idx(doc_ids.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(),
        [&](size_t a, size_t b) { return scores[a] > scores[b]; });

    std::vector<int> sorted_ids;
    std::vector<float> sorted_scores;
    for (auto i : idx) {
        sorted_ids.push_back(doc_ids[i]);
        sorted_scores.push_back(scores[i]);
    }

    return std::make_unique<Hits>(*reader_, static_cast<int>(sorted_ids.size()),
                                  std::move(sorted_ids), std::move(sorted_scores));
}

TopDocs IndexSearcher::Search(const Query& query, int top_k) const {
    if (!reader_) return TopDocs{};
    auto scorer = query.CreateScorer(*reader_);
    if (!scorer) return TopDocs{};

    HitQueue queue(top_k);
    int total_hits = 0;

    while (scorer->Next()) {
        ++total_hits;
        ScoreDoc sd;
        sd.doc = scorer->Doc();
        sd.score = scorer->Score();
        queue.Put(sd);
    }

    TopDocs result;
    result.total_hits = total_hits;
    while (queue.Size() > 0) {
        result.score_docs.push_back(queue.Top());
        queue.Pop();
    }
    std::reverse(result.score_docs.begin(), result.score_docs.end());
    return result;
}

void IndexSearcher::Close() {
    if (owns_reader_ && reader_) {
        reader_->Close();
        delete reader_;
        reader_ = nullptr;
    }
    owned_dir_.reset();
}

int IndexSearcher::MaxDoc() const {
    return reader_->MaxDoc();
}

std::unique_ptr<document::Document> IndexSearcher::Document(int doc_id) const {
    if (!reader_) return nullptr;
    return reader_->Document(doc_id);
}

}  // namespace search
}  // namespace minilucene
