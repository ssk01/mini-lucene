#include "minilucene/search/multi_term_query.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/term_enum.h"
#include "minilucene/search/boolean_query.h"
#include "minilucene/search/filtered_term_enum.h"
#include "minilucene/search/boolean_clause.h"
#include "minilucene/search/scorer.h"
#include "minilucene/search/term_query.h"

namespace minilucene {
namespace search {

MultiTermQuery::MultiTermQuery(const index::Term& term) : term_(term) {}

std::unique_ptr<Scorer> MultiTermQuery::CreateScorer(index::IndexReader& reader) const {
    auto te = GetEnum(reader);
    if (!te) return nullptr;

    BooleanQuery bq;
    int count = 0;
    while (te->Next()) {
        bq.Add(std::make_unique<TermQuery>(te->Current()), Occur::SHOULD);
        ++count;
    }
    te->Close();
    if (count == 0) return nullptr;
    return bq.CreateScorer(reader);
}

std::string MultiTermQuery::ToString() const {
    std::string s = FieldDisplay(term_.FieldNumber()) + ":" +
                    term_.Text() + "*";
    if (boost_ != 1.0f) s += "^" + std::to_string(boost_);
    return s;
}

}  // namespace search
}  // namespace minilucene
