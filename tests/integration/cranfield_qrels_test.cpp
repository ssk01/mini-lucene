// Index full Cranfield collection and verify known term frequencies.
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
#include <map>
#include <set>
#include <sstream>
#include <vector>

using namespace minilucene;

struct CranDoc { int id; std::string body; };

std::vector<CranDoc> LoadDocs(const std::string& path, int max_docs = 1400) {
    std::ifstream f(path);
    std::string line;
    std::vector<CranDoc> docs;
    CranDoc cur; cur.id = 0;
    std::string* target = nullptr;
    while (std::getline(f, line) && static_cast<int>(docs.size()) < max_docs) {
        if (line.substr(0, 2) == ".I") {
            if (cur.id > 0) { docs.push_back(cur); cur = CranDoc(); }
            cur.id = std::stoi(line.substr(3)); target = nullptr;
        } else if (line == ".W" || line == ".T") { target = &cur.body; }
        else if (line == ".A" || line == ".B") { target = nullptr; }
        else if (target && !line.empty()) {
            if (!target->empty()) *target += " ";
            *target += line;
        }
    }
    if (cur.id > 0) docs.push_back(cur);
    return docs;
}

TEST(CranfieldQrels, FullIndexVerify) {
    auto docs = LoadDocs("tests/data/cranfield/cran.all.1400", 1400);
    ASSERT_EQ(docs.size(), 1400);

    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    for (auto& d : docs) {
        document::Document doc;
        doc.Add(document::Field::Text("body", d.body));
        w.AddDocument(doc);
    }
    w.Close();

    index::SegmentReader r(dir, "_0");
    EXPECT_EQ(r.NumDocs(), 1400);
    EXPECT_GT(r.DocFreq(index::Term(0, "the")), 100);
    EXPECT_GT(r.DocFreq(index::Term(0, "wing")), 100);

    search::IndexSearcher s(r);
    std::vector<std::pair<std::string, int>> checks = {
        {"boundary", 10}, {"layer", 10}, {"wing", 50}, {"shock", 10},
        {"turbulent", 20}, {"supersonic", 5}, {"pressure", 30},
    };
    for (auto& c : checks) {
        search::TermQuery tq(index::Term(0, c.first));
        auto hits = s.Search(tq);
        ASSERT_NE(hits, nullptr) << "'" << c.first << "'";
        EXPECT_GE(hits->Length(), c.second)
            << "'" << c.first << "' expected ≥" << c.second << " got " << hits->Length();
    }
}
