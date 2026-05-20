#include "minilucene/analysis/stop_analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/query_parser/query_parser.h"
#include "minilucene/search/hits.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/query.h"
#include "minilucene/search/searcher.h"

#include <iostream>
#include <memory>
#include <string>

int main(int argc, char* argv[]) {
    try {
        auto analyzer = std::make_unique<minilucene::analysis::StopAnalyzer>();
        minilucene::search::IndexSearcher searcher("index");

        std::string line;
        while (true) {
            std::cout << "Query: ";
            if (!std::getline(std::cin, line)) break;

            auto query = minilucene::query_parser::QueryParser::Parse(
                line, "contents", *analyzer);
            std::cout << "Searching for: " << query->ToString() << std::endl;

            auto hits = searcher.Search(*query);
            if (!hits) {
                std::cout << "0 total matching documents" << std::endl;
                continue;
            }

            std::cout << hits->Length() << " total matching documents" << std::endl;

            const int HITS_PER_PAGE = 10;
            for (int start = 0; start < hits->Length(); start += HITS_PER_PAGE) {
                int end = std::min(hits->Length(), start + HITS_PER_PAGE);
                for (int i = start; i < end; ++i) {
                    auto doc = hits->Doc(i);
                    std::string path = "unknown";
                    if (doc) {
                        auto* pf = doc->GetField("path");
                        if (pf) path = pf->Value();
                    }
                    std::cout << i << ". " << path << std::endl;
                }
                if (hits->Length() > end) {
                    std::cout << "more (y/n) ? ";
                    if (!std::getline(std::cin, line)) break;
                    if (line.empty() || line[0] == 'n') break;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cout << " caught a " << typeid(e).name()
                  << "\n with message: " << e.what() << std::endl;
    }

    return 0;
}
