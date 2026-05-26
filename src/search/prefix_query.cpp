#include "minilucene/search/prefix_query.h"
#include "minilucene/index/index_reader.h"
#include "minilucene/index/term_enum.h"
#include "minilucene/search/boolean_query.h"
#include "minilucene/search/boolean_clause.h"
#include "minilucene/search/scorer.h"
#include "minilucene/search/term_query.h"

namespace minilucene {
namespace search {

PrefixQuery::PrefixQuery(index::Term prefix) : prefix_(std::move(prefix)) {}

std::unique_ptr<Scorer> PrefixQuery::CreateScorer(index::IndexReader& reader) const {
    auto terms = reader.Terms(prefix_);
    if (!terms) return nullptr;

    BooleanQuery combined;
    int count = 0;

    while (terms->Next()) {
        const auto& t = terms->Current();
        if (t.FieldNumber() != prefix_.FieldNumber()) break;
        if (t.Text().compare(0, prefix_.Text().size(), prefix_.Text()) != 0) {
            if (t.Text() < prefix_.Text()) continue;
            break;
        }
        combined.Add(std::make_unique<TermQuery>(t), Occur::SHOULD);
        ++count;
    }
    terms->Close();

    if (count == 0) return nullptr;
    return combined.CreateScorer(reader);
}

std::string PrefixQuery::ToString() const {
    std::string s = FieldDisplay(prefix_.FieldNumber()) + ":" +
                    prefix_.Text() + "*";
    if (boost_ != 1.0f) s += "^" + std::to_string(boost_);
    return s;
}

}  // namespace search
}  // namespace minilucene
