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
    std::string pattern_;
    int field_number_;
};

}  // namespace search
}  // namespace minilucene
