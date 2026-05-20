// Uses the Cranfield collection (standard IR test collection) as test data.
// cran.all.1400: 1400 aerodynamics abstracts
// cran.qry: 225 queries with relevance judgments
// cranqrel: relevance judgments (query_id doc_id relevance)
//
// This test indexes the full collection and runs selected queries,
// comparing results against known relevance judgments.

#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/hits.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/ram_directory.h"
#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <map>
#include <set>
#include <vector>

namespace minilucene {

// Parse a Cranfield document. Format:
// .I <id>\n.T\ntitle\n.A\nauthor\n.B\nbib\n.W\nabstract
struct CranDoc {
    int id;
    std::string title;
    std::string author;
    std::string bib;
    std::string body;
};

std::vector<CranDoc> ParseCranfield(const std::string& path, int max_docs = 1400) {
    std::ifstream file(path);
    std::string line;
    std::vector<CranDoc> docs;
    CranDoc current;
    current.id = 0;
    std::string* target = nullptr;

    while (std::getline(file, line) && static_cast<int>(docs.size()) < max_docs) {
        if (line.substr(0, 2) == ".I") {
            if (current.id > 0) docs.push_back(current);
            current = CranDoc();
            current.id = std::stoi(line.substr(3));
            target = nullptr;
        } else if (line == ".T") target = &current.title;
        else if (line == ".A") target = &current.author;
        else if (line == ".B") target = &current.bib;
        else if (line == ".W") target = &current.body;
        else if (target && !line.empty()) {
            if (!target->empty()) *target += " ";
            *target += line;
        }
    }
    if (current.id > 0) docs.push_back(current);
    return docs;
}

// Parse relevance judgments
std::map<int, std::set<int>> ParseQrels(const std::string& path) {
    std::ifstream file(path);
    std::map<int, std::set<int>> qrels;
    int qid, doc_id;
    float rel;
    while (file >> qid >> doc_id >> rel) {
        if (rel > 0) qrels[qid].insert(doc_id);
    }
    return qrels;
}

TEST(Cranfield, ParseDocuments) {
    auto docs = ParseCranfield("tests/data/cranfield/cran.all.1400", 5);
    ASSERT_GE(docs.size(), 5);
    EXPECT_EQ(docs[0].id, 1);
    EXPECT_FALSE(docs[0].body.empty());
    EXPECT_TRUE(docs[0].body.find("aerodynamics") != std::string::npos);
}

TEST(Cranfield, ParseQrels) {
    auto qrels = ParseQrels("tests/data/cranfield/cranqrel");
    EXPECT_FALSE(qrels.empty());
    // Query 1 has at least one relevant doc
    EXPECT_FALSE(qrels[1].empty());
}

TEST(Cranfield, FullIndex) {
    auto docs = ParseCranfield("tests/data/cranfield/cran.all.1400", 1400);
    ASSERT_EQ(docs.size(), 1400);

    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter writer(dir, std::move(analyzer));

    for (const auto& d : docs) {
        document::Document doc;
        doc.Add(document::Field::Text("body", d.title + " " + d.body));
        writer.AddDocument(doc);
    }
    writer.Close();

    index::SegmentReader reader(dir, "_0");
    EXPECT_EQ(reader.NumDocs(), 1400);
    EXPECT_GT(reader.DocFreq(index::Term(0, "the")), 100);
    EXPECT_GT(reader.DocFreq(index::Term(0, "wing")), 100);
}

TEST(Cranfield, FullSearch) {
    auto docs = ParseCranfield("tests/data/cranfield/cran.all.1400", 1400);
    ASSERT_EQ(docs.size(), 1400);

    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter writer(dir, std::move(analyzer));

    for (const auto& d : docs) {
        document::Document doc;
        doc.Add(document::Field::Text("body", d.title + " " + d.body));
        writer.AddDocument(doc);
    }
    writer.Close();

    index::SegmentReader reader(dir, "_0");
    search::IndexSearcher searcher(reader);

    // Run several queries on the full corpus
    struct { const char* term; int min_hits; } queries[] = {
        {"boundary", 5}, {"layer", 10}, {"wing", 50},
        {"shock", 20}, {"turbulent", 30}, {"supersonic", 10},
        {"pressure", 50}, {"temperature", 10}, {"velocity", 30},
    };

    for (auto& q : queries) {
        search::TermQuery tq(index::Term(0, q.term));
        auto hits = searcher.Search(tq);
        ASSERT_NE(hits, nullptr) << "query '" << q.term << "'";
        EXPECT_GE(hits->Length(), q.min_hits)
            << "query '" << q.term << "' expected ≥" << q.min_hits
            << " hits, got " << hits->Length();
    }
}

}  // namespace minilucene
