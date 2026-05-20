#include "minilucene/query_parser/query_parser.h"
#include "minilucene/analysis/analyzer.h"
#include "minilucene/analysis/token.h"
#include "minilucene/analysis/token_stream.h"
#include "minilucene/index/term.h"
#include "minilucene/search/boolean_clause.h"
#include "minilucene/search/boolean_query.h"
#include "minilucene/search/fuzzy_query.h"
#include "minilucene/search/phrase_query.h"
#include "minilucene/search/prefix_query.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/wildcard_query.h"

#include <cctype>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace minilucene {
namespace query_parser {

QueryParser::QueryParser(const std::string& field, const std::string& query,
                         analysis::Analyzer* analyzer)
    : field_(field), input_(query), analyzer_(analyzer) {}

std::unique_ptr<search::Query> QueryParser::Parse(
    const std::string& query, const std::string& field,
    analysis::Analyzer& analyzer) {
    QueryParser parser(field, query, &analyzer);
    return parser.Parse();
}

void QueryParser::SkipWhitespace() {
    while (pos_ < input_.size() && std::isspace(static_cast<unsigned char>(input_[pos_]))) {
        ++pos_;
    }
}

std::unique_ptr<search::Query> QueryParser::Parse() {
    auto result = std::make_unique<search::BooleanQuery>();
    int clauses = 0;

    while (pos_ < input_.size()) {
        SkipWhitespace();
        if (pos_ >= input_.size()) break;
        auto clause = ParseClause();
        if (clause) {
            ++clauses;
            auto* bq = static_cast<search::BooleanQuery*>(result.get());
            bq->Add(std::move(clause), search::Occur::SHOULD);
        }
    }

    if (clauses == 0) return nullptr;
    return result;
}

std::string NormalizeTerm(const std::string& text, analysis::Analyzer* analyzer,
                           const std::string& field) {
    if (!analyzer) return text;
    std::istringstream stream(text);
    auto ts = analyzer->CreateTokenStream(field, stream);
    analysis::Token token;
    if (ts->Next(&token)) return token.Text();
    return text;
}

std::unique_ptr<search::Query> QueryParser::ParseClause() {
    SkipWhitespace();
    if (pos_ >= input_.size()) return nullptr;

    search::Occur occur = search::Occur::SHOULD;
    std::string field = field_;

    if (input_[pos_] == '+') {
        occur = search::Occur::MUST;
        ++pos_;
    } else if (input_[pos_] == '-') {
        occur = search::Occur::MUST_NOT;
        ++pos_;
    }

    SkipWhitespace();

    std::string fname;
    while (pos_ < input_.size() && (std::isalnum(static_cast<unsigned char>(input_[pos_])) || input_[pos_] == '_')) {
        fname += input_[pos_++];
    }
    if (pos_ < input_.size() && input_[pos_] == ':') {
        ++pos_;
        field = fname;
    } else {
        pos_ -= fname.size();
    }

    auto term = ParseTerm();
    if (!term) return nullptr;

    if (occur == search::Occur::SHOULD) return term;

    auto bq = std::make_unique<search::BooleanQuery>();
    bq->Add(std::move(term), occur);
    return bq;
}

std::unique_ptr<search::Query> QueryParser::ParseTerm() {
    SkipWhitespace();
    if (pos_ >= input_.size()) return nullptr;

    if (input_[pos_] == '"') {
        ++pos_;
        std::vector<std::string> words;
        std::string word;
        while (pos_ < input_.size() && input_[pos_] != '"') {
            if (std::isspace(static_cast<unsigned char>(input_[pos_]))) {
                if (!word.empty()) {
                    words.push_back(word);
                    word.clear();
                }
                ++pos_;
            } else {
                word += input_[pos_++];
            }
        }
        if (!word.empty()) words.push_back(word);
        if (pos_ < input_.size()) ++pos_;

        if (words.empty()) return nullptr;
        if (words.size() == 1) {
            return std::make_unique<search::TermQuery>(
                index::Term(0, NormalizeTerm(words[0], analyzer_, field_)));
        }
        auto pq = std::make_unique<search::PhraseQuery>();
        for (const auto& w : words) {
            pq->Add(index::Term(0, NormalizeTerm(w, analyzer_, field_)));
        }
        return pq;
    }

    std::string word;
    while (pos_ < input_.size() && !std::isspace(static_cast<unsigned char>(input_[pos_])) &&
           input_[pos_] != '+' && input_[pos_] != '-') {
        if (input_[pos_] == '"') break;
        word += input_[pos_++];
    }

    if (word.empty()) return nullptr;

    std::string normalized = NormalizeTerm(word, analyzer_, field_);

    if (word.back() == '~') {
        return std::make_unique<search::FuzzyQuery>(
            index::Term(0, normalized));
    }

    if (word.back() == '*') {
        word.pop_back();
        return std::make_unique<search::PrefixQuery>(
            index::Term(0, word));
    }

    if (word.find('*') != std::string::npos || word.find('?') != std::string::npos) {
        return std::make_unique<search::WildcardQuery>(
            index::Term(0, word));
    }

    return std::make_unique<search::TermQuery>(
        index::Term(0, normalized));
}

}  // namespace query_parser
}  // namespace minilucene
