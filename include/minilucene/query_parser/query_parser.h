#pragma once

#include <memory>
#include <string>

namespace minilucene {
namespace analysis {
class Analyzer;
}

namespace index {
class IndexReader;
}

namespace search {
class Query;
}

namespace query_parser {

class QueryParser {
public:
    QueryParser(const std::string& field, const std::string& query,
                analysis::Analyzer* analyzer = nullptr);
    static std::unique_ptr<search::Query> Parse(const std::string& query,
                                                 const std::string& field,
                                                 analysis::Analyzer& analyzer);
    std::unique_ptr<search::Query> Parse();

private:
    void SkipWhitespace();
    std::unique_ptr<search::Query> ParseClause();
    std::unique_ptr<search::Query> ParseTerm();

    std::string field_;
    std::string input_;
    analysis::Analyzer* analyzer_;
    size_t pos_ = 0;
};

}  // namespace query_parser
}  // namespace minilucene
