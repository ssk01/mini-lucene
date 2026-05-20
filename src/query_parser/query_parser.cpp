#include "minilucene/query_parser/query_parser.h"
#include "minilucene/index/term.h"
#include "minilucene/search/boolean_clause.h"
#include "minilucene/search/boolean_query.h"
#include "minilucene/search/fuzzy_query.h"
#include "minilucene/search/phrase_query.h"
#include "minilucene/search/prefix_query.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/wildcard_query.h"

#include <cctype>
#include <stdexcept>
#include <vector>

namespace minilucene {
namespace query_parser {

QueryParser::QueryParser(const std::string& field, const std::string& query)
    : field_(field), input_(query) {}

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
            if (auto* bq = dynamic_cast<search::BooleanQuery*>(result.get())) {
                bq->Add(std::move(clause), search::Occur::SHOULD);
            }
        }
    }

    if (clauses == 0) return nullptr;
    return result;
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
                index::Term(0, words[0]));
        }
        auto pq = std::make_unique<search::PhraseQuery>();
        for (const auto& w : words) {
            pq->Add(index::Term(0, w));
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

    if (word.back() == '~') {
        word.pop_back();
        return std::make_unique<search::FuzzyQuery>(
            index::Term(0, word));
    }

    if (word.back() == '*') {
        bool only_star = (word.size() == 1);
        if (only_star) {
            return std::make_unique<search::PrefixQuery>(
                index::Term(0, ""));
        }
        if (word.find('*') != std::string::npos || word.find('?') != std::string::npos) {
            return std::make_unique<search::WildcardQuery>(
                index::Term(0, word));
        }
        word.pop_back();
        return std::make_unique<search::PrefixQuery>(
            index::Term(0, word));
    }

    if (word.find('*') != std::string::npos || word.find('?') != std::string::npos) {
        return std::make_unique<search::WildcardQuery>(
            index::Term(0, word));
    }

    return std::make_unique<search::TermQuery>(
        index::Term(0, word));
}

}  // namespace query_parser
}  // namespace minilucene
