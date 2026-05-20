#include "minilucene/search/term_query.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/term_docs.h"
#include "minilucene/search/scorer.h"
#include "minilucene/search/similarity.h"

namespace minilucene {
namespace search {

namespace {

class TermScorer : public Scorer {
public:
    TermScorer(std::unique_ptr<index::TermDocs> docs, float idf, float query_weight,
               index::IndexReader& reader, int field_number)
        : docs_(std::move(docs)), idf_(idf), query_weight_(query_weight)
        , reader_(reader), field_number_(field_number) {}

    bool Next() override { return docs_->Next(); }
    int Doc() const override { return docs_->Doc(); }
    float Score() override {
        Similarity sim;
        float tf = sim.Tf(docs_->Freq());
        float norm = reader_.Norm(docs_->Doc(), field_number_);
        return tf * idf_ * idf_ * query_weight_ * norm;
    }

private:
    std::unique_ptr<index::TermDocs> docs_;
    float idf_;
    float query_weight_;
    index::IndexReader& reader_;
    int field_number_;
};

}  // namespace

TermQuery::TermQuery(index::Term term) : term_(std::move(term)) {}

std::unique_ptr<Scorer> TermQuery::Scorer(index::IndexReader& reader) const {
    int doc_freq = reader.DocFreq(term_);
    if (doc_freq == 0) return nullptr;

    Similarity sim;
    float idf = sim.Idf(doc_freq, reader.NumDocs());

    auto docs = reader.Docs(term_);
    if (!docs) return nullptr;

    float query_norm = 1.0f / std::sqrt(1.0f);  // normalize single term

    return std::make_unique<TermScorer>(std::move(docs), idf, query_norm,
                                        reader, term_.FieldNumber());
}

std::string TermQuery::ToString() const {
    return "body:" + term_.Text();
}

}  // namespace search
}  // namespace minilucene
