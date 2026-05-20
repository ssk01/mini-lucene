#include "minilucene/analysis/letter_tokenizer.h"
#include "minilucene/analysis/token.h"

#include <cctype>

namespace minilucene {
namespace analysis {

bool LetterTokenizer::Next(Token* token) {
    while (true) {
        int c = input_.get();
        if (c == EOF) return false;

        int current_pos = static_cast<int>(pos_);
        pos_++;

        if (std::isalpha(static_cast<unsigned char>(c))) {
            int start = current_pos;
            std::string text;
            text += static_cast<char>(c);

            while (true) {
                c = input_.get();
                if (c == EOF) {
                    *token = Token(text, start, static_cast<int>(pos_), "word");
                    return true;
                }

                current_pos = static_cast<int>(pos_);
                pos_++;

                if (!std::isalpha(static_cast<unsigned char>(c))) {
                    *token = Token(text, start, current_pos, "word");
                    return true;
                }
                text += static_cast<char>(c);
            }
        }
    }
}

}  // namespace analysis
}  // namespace minilucene
