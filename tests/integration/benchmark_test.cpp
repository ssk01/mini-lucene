// Performance benchmarks vs Lucene 9 (ES engine). Prints timing to stdout.
#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/boolean_query.h"
#include "minilucene/search/boolean_clause.h"
#include "minilucene/search/phrase_query.h"
#include "minilucene/search/prefix_query.h"
#include "minilucene/search/wildcard_query.h"
#include "minilucene/search/hits.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/ram_directory.h"
#include <gtest/gtest.h>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

using namespace minilucene;

struct CranDoc { std::string body; };

std::vector<CranDoc> LoadCranDocs(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    std::vector<CranDoc> docs;
    CranDoc cur;
    std::string* target = nullptr;
    while (std::getline(f, line)) {
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

// Index once, reuse across tests
class CranfieldBench : public ::testing::Test {
protected:
    void SetUp() override {
        docs = LoadCranDocs("tests/data/cranfield/cran.all.1400");
        ASSERT_GE(docs.size(), 1000);

        auto start = std::chrono::steady_clock::now();
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

        index_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        reader = std::make_unique<index::SegmentReader>(dir, "_0");
        searcher = std::make_unique<search::IndexSearcher>(*reader);
        CategorizeTerms();
    }

    store::RAMDirectory dir;
    std::vector<CranDoc> docs;
    std::unique_ptr<index::SegmentReader> reader;
    std::unique_ptr<search::IndexSearcher> searcher;
    int64_t index_ms;
    std::vector<std::string> high_freq_terms;
    std::vector<std::string> mid_freq_terms;
    std::vector<std::string> low_freq_terms;

    void CategorizeTerms() {
        auto terms = reader->Terms();
        while (terms->Next()) {
            int df = terms->DocFreq();
            const auto& t = terms->Current().Text();
            if (t.size() < 2) continue;
            if (df > 200) high_freq_terms.push_back(t);
            else if (df > 10) mid_freq_terms.push_back(t);
            else low_freq_terms.push_back(t);
        }
        terms->Close();
    }
};

TEST_F(CranfieldBench, IndexThroughput) {
    int docs_per_sec = static_cast<int>(docs.size() * 1000.0 / index_ms);
    std::cout << "\n=== mini-lucene Cranfield Benchmark ===" << std::endl;
    std::cout << "Documents: " << docs.size() << std::endl;
    std::cout << "Index time: " << index_ms << " ms (" << docs_per_sec << " docs/sec)" << std::endl;

    EXPECT_GT(docs_per_sec, 10);
    EXPECT_LT(index_ms, 30000);
    EXPECT_EQ(reader->NumDocs(), static_cast<int>(docs.size()));
}

TEST_F(CranfieldBench, QueryLatency) {
    std::vector<double> latencies_us;
    int total_hits = 0;

    // Mix of high/mid/low frequency terms
    std::vector<std::string> terms;
    for (int i = 0; i < 5 && i < static_cast<int>(high_freq_terms.size()); ++i) terms.push_back(high_freq_terms[i]);
    for (int i = 0; i < 5 && i < static_cast<int>(mid_freq_terms.size()); ++i) terms.push_back(mid_freq_terms[i]);
    for (int i = 0; i < 5 && i < static_cast<int>(low_freq_terms.size()); ++i) terms.push_back(low_freq_terms[i]);

    for (auto& q : terms) {
        auto qs = std::chrono::steady_clock::now();
        search::TermQuery tq(index::Term(0, q));
        auto hits = searcher->Search(tq);
        auto qe = std::chrono::steady_clock::now();

        double us = std::chrono::duration_cast<std::chrono::microseconds>(qe - qs).count();
        latencies_us.push_back(us);
        if (hits) total_hits += hits->Length();
    }

    double sum = 0, min = 999999, max = 0;
    for (auto l : latencies_us) {
        sum += l;
        if (l < min) min = l;
        if (l > max) max = l;
    }
    double avg = sum / latencies_us.size();
    // Skip first query for steady-state (includes warmup cost)
    double steady_sum = 0;
    for (size_t i = 1; i < latencies_us.size(); i++) steady_sum += latencies_us[i];
    double steady_avg = steady_sum / (latencies_us.size() - 1);

    std::cout << "Queries: " << terms.size() << std::endl;
    std::cout << "Total hits: " << total_hits << std::endl;
    std::cout << "Min query: " << min << " μs" << std::endl;
    std::cout << "Max query: " << max << " μs" << std::endl;
    std::cout << "Avg query (all): " << avg << " μs" << std::endl;
    std::cout << "Avg query (steady): " << steady_avg << " μs (excluding 1st warmup)" << std::endl;

    EXPECT_GT(total_hits, 100);
    EXPECT_LT(avg, 50000);
    EXPECT_FALSE(latencies_us.empty());
}

TEST_F(CranfieldBench, TermFrequencyLatency) {
    auto bench = [&](const std::vector<std::string>& terms, const char* label) {
        if (terms.empty()) { std::cout << label << ": (no terms)" << std::endl; return; }
        // Warmup
        for (int w = 0; w < 5 && w < static_cast<int>(terms.size()); ++w) {
            search::TermQuery tq(index::Term(0, terms[w]));
            searcher->Search(tq);
        }
        double sum = 0, min = 999999, max = 0;
        int tested = std::min(50, static_cast<int>(terms.size()));
        for (int i = 0; i < tested; ++i) {
            auto start = std::chrono::steady_clock::now();
            search::TermQuery tq(index::Term(0, terms[i]));
            auto hits = searcher->Search(tq);
            auto end = std::chrono::steady_clock::now();
            double us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            sum += us; if (us < min) min = us; if (us > max) max = us;
        }
        std::cout << label << " (" << tested << " terms): avg=" << (sum/tested)
                  << "μs min=" << min << "μs max=" << max << "μs" << std::endl;
    };

    std::cout << "\n=== TermQuery by Frequency ===" << std::endl;
    bench(high_freq_terms, "High freq (>200)");
    bench(mid_freq_terms,  "Mid freq (10-200)");
    bench(low_freq_terms,  "Low freq (<10)");
    std::cout << "  Total unique terms: " << (high_freq_terms.size() + mid_freq_terms.size() + low_freq_terms.size()) << std::endl;
}

TEST_F(CranfieldBench, MixedQueryTypes) {
    struct QueryCase { const char* name; std::function<void()> run; };
    std::vector<QueryCase> cases;

    // BooleanQuery (MUST + SHOULD)
    cases.push_back({"Boolean MUST+SHOULD", [this]() {
        search::BooleanQuery bq;
        bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "wing")), search::Occur::MUST);
        bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "pressure")), search::Occur::SHOULD);
        searcher->Search(bq);
    }});

    // BooleanQuery MUST_NOT
    cases.push_back({"Boolean MUST_NOT", [this]() {
        search::BooleanQuery bq;
        bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "wing")), search::Occur::MUST);
        bq.Add(std::make_unique<search::TermQuery>(index::Term(0, "boundary")), search::Occur::MUST_NOT);
        searcher->Search(bq);
    }});

    // PhraseQuery
    cases.push_back({"Phrase 2-term", [this]() {
        search::PhraseQuery pq;
        pq.Add(index::Term(0, "boundary"));
        pq.Add(index::Term(0, "layer"));
        searcher->Search(pq);
    }});

    // PrefixQuery
    cases.push_back({"Prefix 'bound'", [this]() {
        search::PrefixQuery pq(index::Term(0, "bound"));
        searcher->Search(pq);
    }});

    // WildcardQuery
    cases.push_back({"Wildcard 'shoc*'", [this]() {
        search::WildcardQuery wq(index::Term(0, "shoc*"));
        searcher->Search(wq);
    }});

    std::cout << "\n=== Mixed Query Types ===" << std::endl;
    for (auto& c : cases) {
        // Warmup
        c.run();
        double sum = 0, min = 999999, max = 0;
        for (int r = 0; r < 10; ++r) {
            auto start = std::chrono::steady_clock::now();
            c.run();
            auto end = std::chrono::steady_clock::now();
            double us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            sum += us; if (us < min) min = us; if (us > max) max = us;
        }
        std::cout << c.name << ": avg=" << (sum/10) << "μs min=" << min << "μs max=" << max << "μs" << std::endl;
    }
}

TEST_F(CranfieldBench, IndexSize) {
    int64_t total = 0;
    for (auto& name : dir.List()) total += dir.FileLength(name);
    double mb = total / (1024.0 * 1024.0);
    std::cout << "Index size: " << mb << " MB" << std::endl;

    EXPECT_GT(mb, 0.1);
    EXPECT_LT(mb, 100);
}
