#pragma once

#include "minilucene/index/term.h"
#include "minilucene/search/filtered_term_enum.h"

namespace minilucene {
namespace search {

class FuzzyTermEnum : public FilteredTermEnum {
public:
    FuzzyTermEnum(index::IndexReader& reader, const index::Term& term, int max_edits = 2);
protected:
    bool TermMatch(const index::Term& term) override;

private:
    static int EditDistance(const std::string& a, const std::string& b);
    index::Term search_term_;
    int max_edits_;
};

}  // namespace search
}  // namespace minilucene
