#include "minilucene/index/segment_reader.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/index/term.h"
#include "minilucene/document/document.h"
#include "minilucene/query_parser/query_parser.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/top_docs.h"
#include "minilucene/store/fs_directory.h"
#include "minilucene/store/index_input.h"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <index_dir>" << std::endl;
        return 1;
    }

    try {
        minilucene::store::FSDirectory dir(argv[1]);
        auto seg_infos = minilucene::index::SegmentInfos::Read(dir);
        if (seg_infos->Segments().empty()) {
            std::cerr << "no segments in index" << std::endl;
            return 1;
        }

        std::string seg_name = seg_infos->Segments()[0].name;
        minilucene::index::SegmentReader reader(dir, seg_name);
        minilucene::search::IndexSearcher searcher(reader);

        std::string line;
        while (true) {
            std::cout << "Query: ";
            if (!std::getline(std::cin, line)) break;
            if (line.empty()) continue;

            minilucene::query_parser::QueryParser parser("contents", line);
            auto query = parser.Parse();
            if (!query) {
                std::cout << "no query parsed" << std::endl;
                continue;
            }

            auto results = searcher.Search(*query, 10);
            std::cout << results.total_hits << " total matching documents" << std::endl;

            for (const auto& sd : results.score_docs) {
                auto doc = reader.Document(sd.doc);
                if (doc) {
                    auto* path_field = doc->GetField("path");
                    std::string path = path_field ? path_field->Value() : "unknown";
                    std::cout << sd.doc << ". " << path << " (score=" << sd.score << ")" << std::endl;
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "caught a " << typeid(e).name()
                  << "\n with message: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
