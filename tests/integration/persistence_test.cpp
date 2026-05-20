// Tests real filesystem I/O: FSDirectory, delete persistence, re-open verification.
#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/index/index_writer.h"
#include "minilucene/index/segment_reader.h"
#include "minilucene/index/segment_infos.h"
#include "minilucene/index/term.h"
#include "minilucene/index/term_docs.h"
#include "minilucene/search/index_searcher.h"
#include "minilucene/search/term_query.h"
#include "minilucene/search/hits.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/fs_directory.h"
#include <gtest/gtest.h>
#include <cstdlib>
#include <filesystem>

using namespace minilucene;

std::string TempDir() {
    auto p = std::filesystem::temp_directory_path() / "minilucene_persist_test_XXXXXX";
    mkdtemp(const_cast<char*>(p.c_str()));
    return p.string();
}

TEST(FSDirectory, IndexAndSearch) {
    std::string dir_path = TempDir();
    {
        store::FSDirectory dir(dir_path);
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        for (int i = 0; i < 10; ++i) {
            document::Document d;
            d.Add(document::Field::Text("body", "fox rabbit " + std::to_string(i)));
            w.AddDocument(d);
        }
        w.Close();
    }

    {
        store::FSDirectory dir(dir_path);
        auto seg_infos = index::SegmentInfos::Read(dir);
        ASSERT_FALSE(seg_infos->Segments().empty());
        index::SegmentReader r(dir, seg_infos->Segments()[0].name);
        EXPECT_EQ(r.NumDocs(), 10);
        EXPECT_EQ(r.DocFreq(index::Term(0, "fox")), 10);
    }

    std::filesystem::remove_all(dir_path);
}

TEST(FSDirectory, PersistDelete) {
    std::string dir_path = TempDir();
    {
        store::FSDirectory dir(dir_path);
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        document::Document d1; d1.Add(document::Field::Text("body", "fox"));
        document::Document d2; d2.Add(document::Field::Text("body", "rabbit"));
        w.AddDocument(d1);
        w.AddDocument(d2);
        w.Close();
    }

    {
        store::FSDirectory dir(dir_path);
        auto seg_infos = index::SegmentInfos::Read(dir);
        index::SegmentReader r(dir, seg_infos->Segments()[0].name);
        EXPECT_EQ(r.NumDocs(), 2);
        r.Delete(0);
        EXPECT_EQ(r.NumDocs(), 1);
        r.Close();
    }

    {
        store::FSDirectory dir(dir_path);
        auto seg_infos = index::SegmentInfos::Read(dir);
        index::SegmentReader r(dir, seg_infos->Segments()[0].name);
        EXPECT_EQ(r.NumDocs(), 1);
        r.Close();
    }

    std::filesystem::remove_all(dir_path);
}

TEST(FSDirectory, MultiSegment) {
    std::string dir_path = TempDir();
    {
        store::FSDirectory dir(dir_path);
        auto a = std::make_unique<analysis::SimpleAnalyzer>();
        index::IndexWriter w(dir, std::move(a));
        w.mergeFactor = 2;
        document::Document d;
        d.Add(document::Field::Text("body", "doc"));
        w.AddDocument(d);
        w.AddDocument(d);
        w.AddDocument(d);
        w.Close();
    }

    {
        store::FSDirectory dir(dir_path);
        auto seg_infos = index::SegmentInfos::Read(dir);
        int total = 0;
        for (auto& si : seg_infos->Segments()) total += si.doc_count;
        EXPECT_EQ(total, 3);
    }

    std::filesystem::remove_all(dir_path);
}
