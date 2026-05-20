#include "minilucene/index/index_writer.h"
#include "minilucene/index/document_writer.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/index/segment_merger.h"
#include "minilucene/index/term.h"
#include "minilucene/index/term_enum.h"
#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/ram_directory.h"
#include <gtest/gtest.h>
#include <set>

namespace minilucene {

document::Document Doc(const std::string& value) {
    document::Document doc;
    doc.Add(document::Field::Text("body", value));
    return doc;
}

TEST(SegmentMerger, MergedIndexEquivalentToSingle) {
    store::RAMDirectory dir;
    auto analyzer = std::make_unique<analysis::SimpleAnalyzer>();

    // Write 3 docs, flushing each to a separate segment
    index::IndexWriter writer(dir, std::move(analyzer));

    // Manually create segments by using DocumentWriter directly
    for (int i = 0; i < 3; ++i) {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a);
        dw.AddDocument(Doc("fox"));
        dw.Flush("_" + std::to_string(i));
    }

    // Merge
    index::SegmentMerger merger(dir, {"_0", "_1", "_2"}, "_merged");
    merger.Merge();

    // Verify merged segment has all 3 docs
    index::SegmentReader reader(dir, "_merged");
    EXPECT_EQ(reader.NumDocs(), 3);

    auto terms = reader.Terms();
    std::set<std::string> term_set;
    while (terms->Next()) {
        term_set.insert(terms->Current().Text());
    }
    terms->Close();
    EXPECT_TRUE(term_set.find("fox") != term_set.end());
}

TEST(SegmentMerger, MultiTermMerge) {
    store::RAMDirectory dir;

    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a);
        dw.AddDocument(Doc("fox"));
        dw.Flush("_0");
    }

    {
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::DocumentWriter dw(dir, *a);
        dw.AddDocument(Doc("rabbit"));
        dw.Flush("_1");
    }

    index::SegmentMerger merger(dir, {"_0", "_1"}, "_merged");
    merger.Merge();

    index::SegmentReader reader(dir, "_merged");
    EXPECT_EQ(reader.NumDocs(), 2);

    int df_fox = reader.DocFreq(index::Term(0, "fox"));
    int df_rabbit = reader.DocFreq(index::Term(0, "rabbit"));
    EXPECT_EQ(df_fox, 1);
    EXPECT_EQ(df_rabbit, 1);
}

}  // namespace minilucene
