#include "minilucene/store/ram_directory.h"
#include "minilucene/store/index_input.h"
#include "minilucene/store/index_output.h"
#include <gtest/gtest.h>
#include <climits>

namespace minilucene {
namespace store {

TEST(VInt, RoundTrip) {
    RAMDirectory dir;
    for (int v : {0, 1, 127, 128, 16383, 16384, INT_MAX}) {
        auto out = dir.CreateOutput("x");
        out->WriteVInt(v);
        out->Close();

        auto in = dir.OpenInput("x");
        EXPECT_EQ(in->ReadVInt(), v);
        in->Close();

        dir.DeleteFile("x");
    }
}

TEST(VLong, RoundTrip) {
    RAMDirectory dir;
    for (int64_t v : {0LL, 1LL, 127LL, 128LL, 16383LL, 16384LL,
                      1LL << 31, (1LL << 62) - 1, LLONG_MAX}) {
        auto out = dir.CreateOutput("x");
        out->WriteVLong(v);
        out->Close();

        auto in = dir.OpenInput("x");
        EXPECT_EQ(in->ReadVLong(), v);
        in->Close();

        dir.DeleteFile("x");
    }
}

TEST(VInt, StringRoundTrip) {
    RAMDirectory dir;
    std::string s = "hello lucene!";
    auto out = dir.CreateOutput("x");
    out->WriteString(s);
    out->Close();

    auto in = dir.OpenInput("x");
    EXPECT_EQ(in->ReadString(), s);
}

TEST(VInt, EmptyString) {
    RAMDirectory dir;
    auto out = dir.CreateOutput("x");
    out->WriteString("");
    out->Close();

    auto in = dir.OpenInput("x");
    EXPECT_EQ(in->ReadString(), "");
}

}  // namespace store
}  // namespace minilucene
