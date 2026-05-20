#pragma once

#include "minilucene/index/term.h"
#include "minilucene/search/filtered_term_enum.h"

namespace minilucene {
namespace search {

class WildcardTermEnum : public FilteredTermEnum {
public:
    WildcardTermEnum(index::IndexReader& reader, const index::Term& term);
protected:
    bool TermMatch(const index::Term& term) override;

private:
    bool MatchText(const std::string& text) const;
    std::string pattern_;
};

}  // namespace search
}  // namespace minilucene
