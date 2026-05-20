#pragma once

#include "minilucene/analysis/token_filter.h"

namespace minilucene {
namespace analysis {

class StandardFilter : public TokenFilter {
public:
    using TokenFilter::TokenFilter;
    bool Next(Token* token) override;
};

}  // namespace analysis
}  // namespace minilucene
