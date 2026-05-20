#include "minilucene/analysis/stop_analyzer.h"
#include "minilucene/document/date_field.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/store/fs_directory.h"

#include <chrono>
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
        std::cout << "adding " << path.string() << std::endl;

        minilucene::document::Document doc;

        // contents first → field 0 (so QueryParser's Term(0, x) matches)
        std::ifstream file(path);
        if (file) {
            doc.Add(minilucene::document::Field::Text("contents", file));
        }

        // path: stored, tokenized
        doc.Add(minilucene::document::Field::Text("path", path.string()));

        // modified: stored, indexed, NOT tokenized — use DateField encoding
        auto ft = fs::last_write_time(path);
        auto sft = std::chrono::duration_cast<std::chrono::seconds>(
            ft.time_since_epoch()).count();
        doc.Add(minilucene::document::Field::Keyword("modified",
            minilucene::document::DateField::TimeToString(sft)));

        writer.AddDocument(doc);
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <docs_dir>" << std::endl;
        return 1;
    }

    try {
        auto start = std::chrono::steady_clock::now();

        minilucene::store::FSDirectory dir("index");
        auto analyzer = std::make_unique<minilucene::analysis::StopAnalyzer>();
        minilucene::index::IndexWriter writer(dir, std::move(analyzer));
        writer.mergeFactor = 20;

        IndexDocs(writer, fs::path(argv[1]));

        writer.Optimize();
        writer.Close();

        auto end = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        std::cout << ms << " total milliseconds" << std::endl;

    } catch (const std::exception& e) {
        std::cout << " caught a " << typeid(e).name()
                  << "\n with message: " << e.what() << std::endl;
    }

    return 0;
}
