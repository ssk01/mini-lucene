#include "minilucene/analysis/stop_analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/document/field.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/store/fs_directory.h"
#include "minilucene/store/index_input.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

void IndexDocs(minilucene::index::IndexWriter& writer, const fs::path& path) {
    if (fs::is_directory(path)) {
        for (const auto& entry : fs::directory_iterator(path)) {
            IndexDocs(writer, entry.path());
        }
    } else {
        auto ext = path.extension().string();
        if (ext != ".txt" && ext != ".md" && ext != ".cpp" && ext != ".h") return;

        std::cout << "adding " << path.string() << std::endl;

        minilucene::document::Document doc;
        std::ifstream file(path);
        if (file) {
            doc.Add(minilucene::document::Field::Text("contents", file));
        }

        doc.Add(minilucene::document::Field::Text("path", path.string()));

        auto ft = fs::last_write_time(path);
        auto sft = std::chrono::duration_cast<std::chrono::seconds>(
            ft.time_since_epoch()).count();
        doc.Add(minilucene::document::Field::Keyword("modified",
            std::to_string(sft)));

        writer.AddDocument(doc);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <index_dir> <docs_dir>" << std::endl;
        return 1;
    }

    std::string index_path = argv[1];
    std::string docs_path = argv[2];

    try {
        auto start = std::chrono::steady_clock::now();

        minilucene::store::FSDirectory dir(index_path);
        auto analyzer = std::make_unique<minilucene::analysis::StopAnalyzer>();
        minilucene::index::IndexWriter writer(dir, std::move(analyzer));
        writer.mergeFactor = 20;

        IndexDocs(writer, fs::path(docs_path));

        writer.Optimize();
        writer.Close();

        auto end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::cout << ms << " total milliseconds" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "caught a " << typeid(e).name()
                  << "\n with message: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
