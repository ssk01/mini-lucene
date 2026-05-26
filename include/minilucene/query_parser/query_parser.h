#pragma once

#include <functional>
#include <memory>
#include <string>

namespace minilucene {
namespace analysis {
class Analyzer;
}

namespace index {
class IndexReader;
class FieldInfos;
}

namespace search {
class Query;
}

namespace query_parser {

// Lucene 1.0.1-style query syntax. Supports:
//   - bare term: `fox`
//   - phrase: `"quick brown"`
//   - field prefix: `title:fox`
//   - modifiers: `+must` / `-not` / `NOT term`
//   - conjunctions: `A AND B` / `A OR B` (default operator is OR/SHOULD)
//   - grouping: `(A OR B) AND C`
//   - prefix: `foo*` / fuzzy: `foo~` / wildcard: `f?o`
// Not yet supported: boost `^N`, range `[a TO b]`.
class QueryParser {
public:
    // Resolves a field name to its numeric ID. Return -1 for unknown fields;
    // the parser will throw std::runtime_error on -1 so unknown fields don't
    // silently fall back to field 0.
    using FieldResolver = std::function<int(const std::string&)>;

    QueryParser(const std::string& field, const std::string& query,
                analysis::Analyzer* analyzer = nullptr);
    static std::unique_ptr<search::Query> Parse(const std::string& query,
                                                 const std::string& field,
                                                 analysis::Analyzer& analyzer);
    std::unique_ptr<search::Query> Parse();

    // Default field's numeric ID (used when no `field:` prefix is given, or
    // when the prefix matches the default field name passed to the ctor).
    void SetDefaultFieldNumber(int n) { default_field_number_ = n; }

    // Custom resolver for non-default field names. If unset, any
    // `othername:term` syntax with othername != ctor-field will throw.
    void SetFieldResolver(FieldResolver r) { field_resolver_ = std::move(r); }

    // Convenience: install a resolver backed by FieldInfos lookup.
    void UseFieldInfos(const index::FieldInfos& fi);

private:
    void SkipWhitespace();
    std::unique_ptr<search::Query> ParseGroup(bool inside_paren);
    std::unique_ptr<search::Query> ParseTerm(int field_number);
    int ResolveField(const std::string& name) const;
    std::string FieldDisplayName(int field_number) const;
    bool MatchKeyword(const char* kw);  // consumes if next word == kw (case-sensitive)
    bool IsTermChar(char c) const;

    std::string field_;
    std::string input_;
    analysis::Analyzer* analyzer_;
    size_t pos_ = 0;
    int default_field_number_ = 0;
    FieldResolver field_resolver_;
    // Reverse map: field_number -> name, for ToString display. Wired by
    // UseFieldInfos. Returning empty string means "unknown, fall back".
    std::function<std::string(int)> field_info_lookup_;
};

}  // namespace query_parser
}  // namespace minilucene
