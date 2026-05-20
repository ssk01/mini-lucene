// Performance benchmarks: index throughput, query latency.
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
#include <chrono>
#include <fstream>
#include <random>
#include <string>
#include <vector>

using namespace minilucene;

struct CranDoc { std::string body; };

std::vector<CranDoc> LoadCranDocs(const std::string& path, int max_docs = 1400) {
    std::ifstream f(path);
    std::string line;
    std::vector<CranDoc> docs;
    CranDoc cur;
    std::string* target = nullptr;
    while (std::getline(f, line) && static_cast<int>(docs.size()) < max_docs) {
        if (line.substr(0, 2) == ".I") {
            if (!cur.body.empty()) { docs.push_back(cur); cur = CranDoc(); }
            target = nullptr;
        } else if (line == ".W" || line == ".T") { target = &cur.body; }
        else if (line == ".A" || line == ".B") { target = nullptr; }
        else if (target && !line.empty()) {
            if (!cur.body.empty()) cur.body += " ";
            cur.body += line;
        }
    }
    if (!cur.body.empty()) docs.push_back(cur);
    return docs;
}

TEST(Benchmark, CranfieldIndexThroughput) {
    auto docs = LoadCranDocs("tests/data/cranfield/cran.all.1400");
    ASSERT_GE(docs.size(), 1000);

    auto start = std::chrono::steady_clock::now();

    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.mergeFactor = 10000;
    for (auto& d : docs) {
        document::Document doc;
        doc.Add(document::Field::Text("body", d.body));
        w.AddDocument(doc);
    }
    w.Close();

    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    auto docs_per_sec = static_cast<int>(docs.size() * 1000.0 / ms);

    EXPECT_GT(docs_per_sec, 10) << "Index throughput too low: " << docs_per_sec << " docs/sec";
    EXPECT_LT(ms, 30000) << "Indexing took too long: " << ms << "ms";

    index::SegmentReader r(dir, "_0");
    EXPECT_EQ(r.NumDocs(), static_cast<int>(docs.size()));
}

TEST(Benchmark, CranfieldQueryLatency) {
    auto docs = LoadCranDocs("tests/data/cranfield/cran.all.1400");
    ASSERT_GE(docs.size(), 1000);

    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.mergeFactor = 10000;
    for (auto& d : docs) {
        document::Document doc;
        doc.Add(document::Field::Text("body", d.body));
        w.AddDocument(doc);
    }
    w.Close();

    index::SegmentReader r(dir, "_0");
    search::IndexSearcher s(r);

    std::vector<std::string> queries = {
        "wing", "boundary", "layer", "shock", "turbulent",
        "supersonic", "pressure", "velocity", "temperature", "flow",
        "mach", "airfoil", "stability"
    };

    auto start = std::chrono::steady_clock::now();
    int total_hits = 0;
    for (auto& q : queries) {
        search::TermQuery tq(index::Term(0, q));
        auto hits = s.Search(tq);
        if (hits) total_hits += hits->Length();
    }
    auto end = std::chrono::steady_clock::now();
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    auto avg_us = us / queries.size();

    EXPECT_GT(total_hits, 100) << "Should find many hits across queries";
    EXPECT_LT(avg_us, 50000) << "Avg query latency too high: " << avg_us << "μs";
}

TEST(Benchmark, CranfieldIndexSize) {
    auto docs = LoadCranDocs("tests/data/cranfield/cran.all.1400");
    ASSERT_GE(docs.size(), 1000);

    store::RAMDirectory dir;
    auto a = std::make_unique<analysis::SimpleAnalyzer>();
    index::IndexWriter w(dir, std::move(a));
    w.mergeFactor = 10000;
    for (auto& d : docs) {
        document::Document doc;
        doc.Add(document::Field::Text("body", d.body));
        w.AddDocument(doc);
    }
    w.Close();

    // Total size of all index files
    int64_t total_bytes = 0;
    for (auto& name : dir.List()) {
        total_bytes += dir.FileLength(name);
    }

    int mb = static_cast<int>(total_bytes / (1024 * 1024));
    EXPECT_GT(mb, 0) << "Index should be at least 1MB";
    EXPECT_LT(mb, 100) << "Index should not exceed 100MB";
}


