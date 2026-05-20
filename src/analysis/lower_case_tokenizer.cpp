#include "minilucene/analysis/lower_case_tokenizer.h"
#include "minilucene/analysis/token.h"

#include <cctype>

namespace minilucene {
namespace analysis {

bool LowerCaseTokenizer::Next(Token* token) {
    if (!LetterTokenizer::Next(token)) return false;
    std::string t;
    for (char c : token->Text()) {
        t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    token->SetText(t);
    return true;
}

}  // namespace analysis
}  // namespace minilucene
