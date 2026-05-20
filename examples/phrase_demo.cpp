#include "minilucene/search/phrase_query.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/top_docs.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/index/term_docs.h"
#include "minilucene/index/term_positions.h"
#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/ram_directory.h"
#include <iostream>

using namespace minilucene;

int main() {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(analyzer));

    document::Document doc;
    doc.Add(document::Field::Text("body", "the quick brown fox"));
    w.AddDocument(doc);
    w.Close();

    std::cout << "Files: ";
    for (auto& f : dir.List()) std::cout << f << " ";
    std::cout << std::endl;

    index::SegmentReader reader(dir, "_0");
    std::cout << "NumDocs: " << reader.NumDocs() << std::endl;

    // Test TermDocs directly
    auto docs = reader.Docs(index::Term(0, "quick"));
    if (docs) {
        std::cout << "quick: docs found" << std::endl;
        if (docs->Next()) {
            std::cout << "  doc=" << docs->Doc() << " freq=" << docs->Freq() << std::endl;
        }
    } else {
        std::cout << "quick: NOT FOUND" << std::endl;
    }

    // Test TermPositions directly
    auto tp = reader.Positions(index::Term(0, "quick"));
    if (tp) {
        std::cout << "quick positions found" << std::endl;
        if (tp->Next()) {
            std::cout << "  doc=" << tp->Doc() << " freq=" << tp->Freq();
            for (int i = 0; i < tp->Freq(); ++i) {
                std::cout << " pos=" << tp->NextPosition();
            }
            std::cout << std::endl;
        }
    } else {
        std::cout << "quick positions: NOT FOUND" << std::endl;
    }

    // Test PhraseQuery scorer directly
    search::PhraseQuery query;
    query.Add(index::Term(0, "quick"));
    query.Add(index::Term(0, "brown"));

    std::cout << "Creating scorer..." << std::endl;
    auto scorer = query.CreateScorer(reader);
    if (scorer) {
        std::cout << "Scorer created" << std::endl;
        std::cout << "Calling Next()..." << std::endl;
        bool has = scorer->Next();
        std::cout << "has=" << has << std::endl;
        if (has) {
            std::cout << "doc=" << scorer->Doc() << " score=" << scorer->Score() << std::endl;
        }
    } else {
        std::cout << "Scorer is null!" << std::endl;
    }

    std::cout << "Done!" << std::endl;
    return 0;
}
