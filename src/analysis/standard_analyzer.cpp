#include "minilucene/analysis/standard_analyzer.h"
#include "minilucene/analysis/token.h"
#include "minilucene/analysis/token_stream.h"
#include "minilucene/analysis/tokenizer.h"
#include "minilucene/analysis/lower_case_filter.h"
#include "minilucene/analysis/stop_filter.h"
#include <cctype>
#include <sstream>

namespace minilucene {
namespace analysis {

namespace {

class StandardTokenizer : public Tokenizer {
public:
    StandardTokenizer(std::istream& input) : Tokenizer(input) {
        std::ostringstream ss;
        ss << input.rdbuf();
        content_ = ss.str();
        pos_ = 0;
    }
    bool Next(Token* token) override;

private:
    std::string content_;
    size_t pos_ = 0;

    bool IsAlpha(char c) {
        return std::isalpha(static_cast<unsigned char>(c));
    }
    bool IsAlnum(char c) {
        return std::isalnum(static_cast<unsigned char>(c));
    }
};

bool StandardTokenizer::Next(Token* token) {
    while (pos_ < content_.size() && !IsAlpha(content_[pos_])) ++pos_;
    if (pos_ >= content_.size()) return false;

    int start = static_cast<int>(pos_);

    // Check for acronym: 2+ letters separated by dots
    if (pos_ + 3 < content_.size() && IsAlpha(content_[pos_]) &&
        content_[pos_ + 1] == '.' && IsAlpha(content_[pos_ + 2])) {
        std::string acr;
        while (pos_ < content_.size()) {
            if (IsAlpha(content_[pos_])) {
                acr += content_[pos_++];
            } else if (content_[pos_] == '.') {
                if (pos_ + 1 < content_.size() && IsAlpha(content_[pos_ + 1])) {
                    acr += '.'; ++pos_;
                } else {
                    ++pos_; break;  // trailing dot, skip it
                }
            } else break;
        }
        if (acr.size() >= 3) {
            *token = Token(acr, start, static_cast<int>(pos_), "word");
            return true;
        }
        pos_ = start;
    }

    // Read a word (letters only)
    std::string word;
    while (pos_ < content_.size() && IsAlpha(content_[pos_])) {
        word += content_[pos_++];
    }
    if (word.empty()) return false;

    // Check for email: word@domain.extension
    if (pos_ < content_.size() && content_[pos_] == '@') {
        size_t saved = pos_;
        std::string email = word + "@";
        ++pos_;
        while (pos_ < content_.size() && (IsAlnum(content_[pos_]) || content_[pos_] == '.' || content_[pos_] == '-' || content_[pos_] == '_')) {
            email += content_[pos_++];
        }
        if (email.find('@') < email.size() - 1 && email.find('.') != std::string::npos) {
            *token = Token(email, start, static_cast<int>(pos_), "word");
            return true;
        }
        pos_ = saved;
    }

    // Check for URL: protocol://...
    if (word == "http" || word == "https" || word == "ftp") {
        size_t saved = pos_;
        if (pos_ + 2 < content_.size() && content_[pos_] == ':' &&
            content_[pos_ + 1] == '/' && content_[pos_ + 2] == '/') {
            std::string url = word;
            do { url += content_[pos_++]; }
            while (pos_ < content_.size() && !std::isspace(static_cast<unsigned char>(content_[pos_])));
            *token = Token(url, start, static_cast<int>(pos_), "word");
            return true;
        }
        pos_ = saved;
    }

    // Plain word
    *token = Token(word, start, static_cast<int>(pos_), "word");
    return true;
}

}  // namespace

StandardAnalyzer::StandardAnalyzer() {}

std::unique_ptr<TokenStream> StandardAnalyzer::CreateTokenStream(
    const std::string& field, std::istream& input) {
    auto tokenizer = std::make_unique<StandardTokenizer>(input);
    auto lower = std::make_unique<LowerCaseFilter>(std::move(tokenizer));
    return std::make_unique<StopFilter>(std::move(lower), StopFilter::DefaultStopWords());
}

}  // namespace analysis
}  // namespace minilucene
