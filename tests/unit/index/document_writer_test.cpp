#include "minilucene/index/document_writer.h"
#include "minilucene/index/field_infos.h"
#include "minilucene/index/term.h"
#include "minilucene/index/term_info.h"
#include "minilucene/analysis/simple_analyzer.h"
#include "minilucene/document/document.h"
#include "minilucene/document/field.h"
#include "minilucene/store/ram_directory.h"
#include "minilucene/store/index_input.h"
#include <gtest/gtest.h>
#include <set>
#include <string>

namespace minilucene {
namespace index {

TEST(DocumentWriter, SingleDocSegmentFilesCreated) {
    store::RAMDirectory dir;
    analysis::SimpleAnalyzer analyzer;
    document::Document doc;
    doc.Add(document::Field::Text("body", "the quick brown fox"));

    DocumentWriter writer(dir, analyzer);
    writer.AddDocument(doc);
    writer.Flush("_0");

    for (auto suffix : {".fnm", ".tis", ".tii", ".frq", ".prx", ".nrm"}) {
        EXPECT_TRUE(dir.FileExists(std::string("_0") + suffix));
    }
}

TEST(DocumentWriter, TermsSortedAlphabetically) {
    store::RAMDirectory dir;
    analysis::SimpleAnalyzer analyzer;
    document::Document doc;
    doc.Add(document::Field::Text("body", "quick brown fox"));

    DocumentWriter writer(dir, analyzer);
    writer.AddDocument(doc);
    writer.Flush("_0");

    auto in = dir.OpenInput("_0.tis");
    std::vector<std::string> terms;
    int num_terms = 0;
    while (true) {
        try {
            int fn = in->ReadVInt();
            (void)fn;
            std::string t = in->ReadString();
            int df = in->ReadVInt();
            (void)df;
            in->ReadVLong();
            in->ReadVLong();
            terms.push_back(t);
            ++num_terms;
        } catch (...) {
            break;
        }
    }
    in->Close();

    EXPECT_GE(num_terms, 3);
    std::vector<std::string> expected = {"brown", "fox", "quick"};
    EXPECT_EQ(terms, expected);
}

TEST(DocumentWriter, FrequencyEncodedCorrectly) {
    store::RAMDirectory dir;
    analysis::SimpleAnalyzer analyzer;
    document::Document doc;
    doc.Add(document::Field::Text("body", "fox fox fox cat"));

    DocumentWriter writer(dir, analyzer);
    writer.AddDocument(doc);
    writer.Flush("_0");

    std::map<std::string, int> freqs;
    auto in = dir.OpenInput("_0.tis");
    while (true) {
        try {
            int fn = in->ReadVInt();
            (void)fn;
            std::string t = in->ReadString();
            int df = in->ReadVInt();
            (void)df;
            in->ReadVLong();
            in->ReadVLong();
            freqs[t] = df;
        } catch (...) {
            break;
        }
    }
    in->Close();

    EXPECT_EQ(freqs["fox"], 1);
    EXPECT_EQ(freqs["cat"], 1);
}

TEST(DocumentWriter, PositionsRecorded) {
    store::RAMDirectory dir;
    analysis::SimpleAnalyzer analyzer;
    document::Document doc;
    doc.Add(document::Field::Text("body", "a b a b a"));

    DocumentWriter writer(dir, analyzer);
    writer.AddDocument(doc);
    writer.Flush("_0");

    auto tis = dir.OpenInput("_0.tis");
    int64_t a_prox_ptr = -1;
    int64_t b_prox_ptr = -1;
    while (true) {
        try {
            int fn = tis->ReadVInt();
            (void)fn;
            std::string t = tis->ReadString();
            tis->ReadVInt();
            tis->ReadVLong();
            int64_t prox_ptr = tis->ReadVLong();
            if (t == "a") a_prox_ptr = prox_ptr;
            if (t == "b") b_prox_ptr = prox_ptr;
        } catch (...) {
            break;
        }
    }
    tis->Close();

    auto prx = dir.OpenInput("_0.prx");
    ASSERT_GE(a_prox_ptr, 0);
    ASSERT_GE(b_prox_ptr, 0);

    prx->Seek(a_prox_ptr);
    std::vector<int> a_pos;
    try {
        int p = 0;
        for (int i = 0; i < 3; ++i) {
            p += prx->ReadVInt();
            a_pos.push_back(p);
        }
    } catch (...) {}
    EXPECT_EQ(a_pos, std::vector<int>({0, 2, 4}));

    prx->Seek(b_prox_ptr);
    std::vector<int> b_pos;
    try {
        int p = 0;
        for (int i = 0; i < 2; ++i) {
            p += prx->ReadVInt();
            b_pos.push_back(p);
        }
    } catch (...) {}
    EXPECT_EQ(b_pos, std::vector<int>({1, 3}));

    prx->Close();
}

TEST(DocumentWriter, TiiSamplesEvery128Terms) {
    store::RAMDirectory dir;
    analysis::SimpleAnalyzer analyzer;
    document::Document doc;
    std::string many_words;
    for (int i = 0; i < 300; ++i) {
        if (i > 0) many_words += " ";
        many_words += std::string(1, 'a' + (i / 26)) + std::string(1, 'a' + (i % 26));
    }
    doc.Add(document::Field::Text("body", many_words));

    DocumentWriter writer(dir, analyzer);
    writer.AddDocument(doc);
    writer.Flush("_0");

    auto tii = dir.OpenInput("_0.tii");
    int count = 0;
    while (true) {
        try {
            tii->ReadVInt();
            tii->ReadString();
            tii->ReadVLong();
            ++count;
        } catch (...) {
            break;
        }
    }
    tii->Close();

    EXPECT_GE(count, 2);
    EXPECT_LE(count, 3);
}

}  // namespace index
}  // namespace minilucene
