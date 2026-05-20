#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/term.h"
#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/ram_directory.h"
#include <gtest/gtest.h>
#include <set>
#include <string>
#include <vector>

namespace minilucene {
namespace index {

document::Document MakeDoc(const std::string& field, const std::string& value) {
    document::Document doc;
    doc.Add(document::Field::Text(field, value));
    return doc;
}

std::vector<std::string> CollectAllTerms(SegmentReader& r) {
    auto terms = r.Terms();
    std::vector<std::string> result;
    while (terms->Next()) {
        result.push_back(terms->Current().Text());
    }
    terms->Close();
    return result;
}

TEST(WriteRead, EnumerateAllTerms) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    IndexWriter w(dir, std::move(analyzer));
    w.AddDocument(MakeDoc("body", "alpha beta gamma"));
    w.AddDocument(MakeDoc("body", "beta gamma delta"));
    w.Close();

    SegmentReader r(dir, "_0");
    auto terms = CollectAllTerms(r);
    std::vector<std::string> expected = {"alpha", "beta", "delta", "gamma"};
    EXPECT_EQ(terms, expected);
    r.Close();
}

TEST(WriteRead, TermDocsIteration) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();

    // Write a single doc and test reading back
    IndexWriter w(dir, std::move(analyzer));
    w.AddDocument(MakeDoc("body", "beta gamma"));
    w.Close();

    ASSERT_TRUE(dir.FileExists("_0.frq"));

    SegmentReader r(dir, "_0");

    auto docs = r.Docs(Term(0, "beta"));
    ASSERT_NE(docs, nullptr);
    ASSERT_TRUE(docs->Next());
    EXPECT_EQ(docs->Doc(), 0);
    EXPECT_EQ(docs->Freq(), 1);
    EXPECT_FALSE(docs->Next());
    docs->Close();

    r.Close();
}

TEST(WriteRead, TermPositionsExact) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    IndexWriter w(dir, std::move(analyzer));
    w.AddDocument(MakeDoc("body", "a b a"));
    w.Close();

    SegmentReader r(dir, "_0");

    auto tp = r.Positions(Term(0, "a"));
    ASSERT_NE(tp, nullptr);
    ASSERT_TRUE(tp->Next());
    EXPECT_EQ(tp->Doc(), 0);
    EXPECT_EQ(tp->Freq(), 2);
    EXPECT_EQ(tp->NextPosition(), 0);
    EXPECT_EQ(tp->NextPosition(), 2);
    EXPECT_FALSE(tp->Next());
    tp->Close();

    r.Close();
}

TEST(WriteRead, SeekToMidDictionary) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();
    IndexWriter w(dir, std::move(analyzer));
    std::string many_words;
    for (int i = 0; i < 300; ++i) {
        if (i > 0) many_words += " ";
        many_words += std::string(1, 'a' + (i / 26)) + std::string(1, 'a' + (i % 26));
    }
    w.AddDocument(MakeDoc("body", many_words));
    w.Close();

    SegmentReader r(dir, "_0");
    int df = r.DocFreq(Term(0, "ky"));
    EXPECT_EQ(df, 1);
    r.Close();
}

}  // namespace index
}  // namespace minilucene
