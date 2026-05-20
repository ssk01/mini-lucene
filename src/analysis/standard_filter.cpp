#include "minilucene/analysis/standard_filter.h"

namespace minilucene {
namespace analysis {

bool StandardFilter::Next(Token* token) {
    // STUB: passes through unchanged
    return input_->Next(token);
}

}  // namespace analysis
}  // namespace minilucene
