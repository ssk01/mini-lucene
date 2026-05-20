#include "minilucene/analysis/lower_case_filter.h"
#include "minilucene/analysis/token.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace minilucene {
namespace analysis {

bool LowerCaseFilter::Next(Token* token) {
    if (!input_->Next(token)) return false;
    std::string lower;
    lower.reserve(token->Text().size());
    for (char c : token->Text()) {
        lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    token->SetText(lower);
    return true;
}

}  // namespace analysis
}  // namespace minilucene
