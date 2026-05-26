#include "minilucene/analysis/stop_analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/index/field_infos.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/query_parser/query_parser.h"
#include "minilucene/search/hits.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/query.h"
#include "minilucene/search/searcher.h"
#include "minilucene/store/fs_directory.h"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

int main(int argc, char* argv[]) {
    std::string index_path = "index";
    if (argc > 1) index_path = argv[1];

    try {
        auto analyzer = std::make_unique<minilucene::analysis::StopAnalyzer>();

        // Load FieldInfos from the first segment so QueryParser can resolve
        // `field:term` syntax (e.g. `path:doc1`). Without this, any cross-
        // field query throws std::runtime_error.
        minilucene::store::FSDirectory dir(index_path);
        auto seg_infos = minilucene::index::SegmentInfos::Read(dir);
        std::unique_ptr<minilucene::index::FieldInfos> fi;
        if (seg_infos && !seg_infos->Segments().empty()) {
            fi = minilucene::index::FieldInfos::Read(
                dir, seg_infos->Segments()[0].name);
        }

        minilucene::search::IndexSearcher searcher(dir);

        std::string line;
        while (true) {
            std::cout << "Query: ";
            if (!std::getline(std::cin, line)) break;
            if (line.empty()) continue;

            minilucene::query_parser::QueryParser parser(
                "contents", line, analyzer.get());
            if (fi) {
                parser.UseFieldInfos(*fi);
                int default_no = fi->FieldNumber("contents");
                if (default_no >= 0) parser.SetDefaultFieldNumber(default_no);
            }

            std::unique_ptr<minilucene::search::Query> query;
            try {
                query = parser.Parse();
            } catch (const std::exception& e) {
                std::cout << "parse error: " << e.what() << std::endl;
                continue;
            }
            if (!query) {
                std::cout << "(empty query)" << std::endl;
                continue;
            }
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
