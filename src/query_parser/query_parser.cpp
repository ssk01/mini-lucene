#include "minilucene/query_parser/query_parser.h"
#include "minilucene/analysis/analyzer.h"
#include "minilucene/analysis/token.h"
#include "minilucene/analysis/token_stream.h"
#include "minilucene/index/field_infos.h"
#include "minilucene/index/term.h"
#include "minilucene/search/boolean_clause.h"
#include "minilucene/search/boolean_query.h"
#include "minilucene/search/fuzzy_query.h"
#include "minilucene/search/phrase_query.h"
#include "minilucene/search/prefix_query.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/wildcard_query.h"

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace minilucene {
namespace query_parser {

namespace {

// Mirror of Java QueryParser.jj clause-collection state.
enum Conj { CONJ_NONE, CONJ_AND, CONJ_OR };
enum Mod  { MOD_NONE,  MOD_REQ,  MOD_NOT };

struct PendingClause {
    std::unique_ptr<search::Query> q;
    bool required;
    bool prohibited;
};

}  // namespace

QueryParser::QueryParser(const std::string& field, const std::string& query,
                         analysis::Analyzer* analyzer)
    : field_(field), input_(query), analyzer_(analyzer) {}

std::unique_ptr<search::Query> QueryParser::Parse(
    const std::string& query, const std::string& field,
    analysis::Analyzer& analyzer) {
    QueryParser parser(field, query, &analyzer);
    return parser.Parse();
}

void QueryParser::UseFieldInfos(const index::FieldInfos& fi) {
    field_resolver_ = [&fi](const std::string& name) {
        return fi.FieldNumber(name);
    };
    field_info_lookup_ = [&fi](int number) -> std::string {
        const auto* fi_entry = fi.FieldByNumber(number);
        return fi_entry ? fi_entry->Name() : std::string();
    };
}

void QueryParser::SkipWhitespace() {
    while (pos_ < input_.size() &&
           std::isspace(static_cast<unsigned char>(input_[pos_]))) {
        ++pos_;
    }
}

bool QueryParser::IsTermChar(char c) const {
    if (std::isalnum(static_cast<unsigned char>(c))) return true;
    return c == '_';
}

// Read an optional `^<float>` suffix at the current position. Returns the
// parsed boost or 1.0 if no `^` is present.
namespace {
float ReadBoost(const std::string& input, size_t& pos) {
    if (pos >= input.size() || input[pos] != '^') return 1.0f;
    size_t start = pos + 1;
    size_t end = start;
    while (end < input.size() &&
           (std::isdigit(static_cast<unsigned char>(input[end])) ||
            input[end] == '.')) {
        ++end;
    }
    if (end == start) return 1.0f;  // bare `^` with no number — ignore
    float v = static_cast<float>(std::strtod(input.c_str() + start, nullptr));
    pos = end;
    if (v <= 0.0f) return 1.0f;
    return v;
}
}  // namespace

bool QueryParser::MatchKeyword(const char* kw) {
    size_t len = std::strlen(kw);
    if (pos_ + len > input_.size()) return false;
    if (std::memcmp(input_.data() + pos_, kw, len) != 0) return false;
    // Must be followed by non-term char (whitespace, paren, EOF, etc.) so
    // we don't gobble "ANDROID" as "AND" + "ROID".
    if (pos_ + len < input_.size() && IsTermChar(input_[pos_ + len])) {
        return false;
    }
    pos_ += len;
    return true;
}

// Reverse-map a field number back to a human name for ToString. When the
// number matches the default field, we use the ctor-supplied name.
// Otherwise we ask FieldInfos via the resolver-source map if present.
// Falls back to `field<N>` so output never silently lies.
std::string QueryParser::FieldDisplayName(int field_number) const {
    if (field_number == default_field_number_) return field_;
    if (field_info_lookup_) {
        std::string n = field_info_lookup_(field_number);
        if (!n.empty()) return n;
    }
    return "field<" + std::to_string(field_number) + ">";
}

int QueryParser::ResolveField(const std::string& name) const {
    if (name == field_) return default_field_number_;
    if (!field_resolver_) {
        throw std::runtime_error(
            "QueryParser: field '" + name + "' is not the default field '" +
            field_ + "' and no FieldResolver is set. Call SetFieldResolver() "
            "or UseFieldInfos() before parsing cross-field queries.");
    }
    int n = field_resolver_(name);
    if (n < 0) {
        throw std::runtime_error(
            "QueryParser: field '" + name + "' is not known to the resolver.");
    }
    return n;
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

std::unique_ptr<search::Query> QueryParser::Parse() {
    return ParseGroup(false);
}

// Reads a sequence of clauses joined by optional AND/OR and assembles a
// BooleanQuery (or returns the lone clause directly if the group has only
// one neutral clause).
std::unique_ptr<search::Query> QueryParser::ParseGroup(bool inside_paren) {
    std::vector<PendingClause> clauses;

    while (pos_ < input_.size()) {
        SkipWhitespace();
        if (pos_ >= input_.size()) break;
        if (inside_paren && input_[pos_] == ')') {
            ++pos_;
            break;
        }

        // Conjunction is only meaningful between clauses.
        Conj conj = CONJ_NONE;
        if (!clauses.empty()) {
            if (MatchKeyword("AND")) conj = CONJ_AND;
            else if (MatchKeyword("OR")) conj = CONJ_OR;
            SkipWhitespace();
        }

        // Modifier: + / - / NOT (NOT only at start of a clause).
        Mod mod = MOD_NONE;
        if (pos_ < input_.size() && input_[pos_] == '+') {
            mod = MOD_REQ; ++pos_;
        } else if (pos_ < input_.size() && input_[pos_] == '-') {
            mod = MOD_NOT; ++pos_;
        } else if (MatchKeyword("NOT")) {
            mod = MOD_NOT;
        }
        SkipWhitespace();
        if (pos_ >= input_.size()) break;

        // Optional `field:` prefix.
        std::string field = field_;
        size_t save = pos_;
        std::string fname;
        while (pos_ < input_.size() && IsTermChar(input_[pos_])) {
            fname += input_[pos_++];
        }
        if (pos_ < input_.size() && input_[pos_] == ':') {
            ++pos_;
            field = fname;
        } else {
            pos_ = save;  // rollback; the alnum run is actually the term
        }

        int field_number = ResolveField(field);

        // Atom: either `(group)` or a leaf term/phrase.
        SkipWhitespace();
        std::unique_ptr<search::Query> q;
        if (pos_ < input_.size() && input_[pos_] == '(') {
            ++pos_;
            q = ParseGroup(true);
        } else {
            q = ParseTerm(field_number);
        }
        if (!q) continue;

        // Java QueryParser.addClause semantics:
        //   - AND retroactively makes the previous clause required (unless
        //     it's prohibited).
        //   - Current clause is prohibited if mod==NOT.
        //   - Current clause is required if mod==REQ, OR if joined by AND
        //     and not prohibited.
        if (conj == CONJ_AND && !clauses.empty() &&
            !clauses.back().prohibited) {
            clauses.back().required = true;
        }
        bool prohibited = (mod == MOD_NOT);
        bool required   = (mod == MOD_REQ) ||
                          (conj == CONJ_AND && !prohibited);
        clauses.push_back({std::move(q), required, prohibited});
    }

    if (clauses.empty()) return nullptr;

    // Single neutral clause unwraps (matches Java behavior + keeps ToString
    // tidy for the common `body:foo` case).
    if (clauses.size() == 1 && !clauses[0].required && !clauses[0].prohibited) {
        return std::move(clauses[0].q);
    }

    auto bq = std::make_unique<search::BooleanQuery>();
    for (auto& c : clauses) {
        search::Occur occur =
            c.required   ? search::Occur::MUST
          : c.prohibited ? search::Occur::MUST_NOT
                         : search::Occur::SHOULD;
        bq->Add(std::move(c.q), occur);
    }
    return bq;
}

std::unique_ptr<search::Query> QueryParser::ParseTerm(int field_number) {
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
        float boost = ReadBoost(input_, pos_);
        // Reconstruct the field name for ToString display: the parser knows
        // the spelling from the source query (either ctor default or the
        // explicit `field:` prefix).
        std::string display_field = FieldDisplayName(field_number);
        if (words.size() == 1) {
            auto tq = std::make_unique<search::TermQuery>(
                index::Term(field_number, NormalizeTerm(words[0], analyzer_, field_)));
            tq->SetFieldName(display_field);
            tq->SetBoost(boost);
            return tq;
        }
        auto pq = std::make_unique<search::PhraseQuery>();
        for (const auto& w : words) {
            pq->Add(index::Term(field_number, NormalizeTerm(w, analyzer_, field_)));
        }
        pq->SetFieldName(display_field);
        pq->SetBoost(boost);
        return pq;
    }

    std::string word;
    while (pos_ < input_.size() && !std::isspace(static_cast<unsigned char>(input_[pos_])) &&
           input_[pos_] != '+' && input_[pos_] != '-' &&
           input_[pos_] != '(' && input_[pos_] != ')' &&
           input_[pos_] != '^') {  // stop before boost suffix
        if (input_[pos_] == '"') break;
        word += input_[pos_++];
    }

    if (word.empty()) return nullptr;

    float boost = ReadBoost(input_, pos_);
    std::string normalized = NormalizeTerm(word, analyzer_, field_);
    std::string display_field = FieldDisplayName(field_number);

    if (word.back() == '~') {
        auto q = std::make_unique<search::FuzzyQuery>(
            index::Term(field_number, normalized));
        q->SetFieldName(display_field);
        q->SetBoost(boost);
        return q;
    }

    if (word.back() == '*') {
        word.pop_back();
        auto q = std::make_unique<search::PrefixQuery>(
            index::Term(field_number, word));
        q->SetFieldName(display_field);
        q->SetBoost(boost);
        return q;
    }

    if (word.find('*') != std::string::npos || word.find('?') != std::string::npos) {
        auto q = std::make_unique<search::WildcardQuery>(
            index::Term(field_number, word));
        q->SetFieldName(display_field);
        q->SetBoost(boost);
        return q;
    }

    auto q = std::make_unique<search::TermQuery>(
        index::Term(field_number, normalized));
    q->SetFieldName(display_field);
    q->SetBoost(boost);
    return q;
}

}  // namespace query_parser
}  // namespace minilucene
