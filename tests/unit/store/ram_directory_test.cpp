#include "minilucene/store/ram_directory.h"
#include "minilucene/store/index_input.h"
#include "minilucene/store/index_output.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <random>
#include <vector>

namespace minilucene {
namespace store {

class RAMDirectoryTest : public testing::Test {
protected:
    void SetUp() override {
        dir_ = std::make_unique<RAMDirectory>();
    }

    std::unique_ptr<RAMDirectory> dir_;
};

TEST_F(RAMDirectoryTest, ListReflectsCreatedFiles) {
    EXPECT_TRUE(dir_->List().empty());

    dir_->CreateOutput("a");
    dir_->CreateOutput("b");
    dir_->CreateOutput("c");

    auto files = dir_->List();
    EXPECT_EQ(files.size(), 3);
    EXPECT_NE(std::find(files.begin(), files.end(), "a"), files.end());
    EXPECT_NE(std::find(files.begin(), files.end(), "b"), files.end());
    EXPECT_NE(std::find(files.begin(), files.end(), "c"), files.end());
}

TEST_F(RAMDirectoryTest, ReadWriteRoundTrip) {
    std::vector<uint8_t> test_data(1024 * 1024);
    std::mt19937 rng(42);
    for (auto& b : test_data) {
        b = static_cast<uint8_t>(rng());
    }

    {
        auto out = dir_->CreateOutput("test");
        for (uint8_t b : test_data) {
            out->WriteByte(b);
        }
        out->Close();
    }

    {
        auto in = dir_->OpenInput("test");
        EXPECT_EQ(in->Length(), static_cast<int64_t>(test_data.size()));
        for (size_t i = 0; i < test_data.size(); ++i) {
            EXPECT_EQ(in->ReadByte(), test_data[i]);
        }
        in->Close();
    }
}

TEST_F(RAMDirectoryTest, DeleteFile) {
    dir_->CreateOutput("tmp");
    EXPECT_TRUE(dir_->FileExists("tmp"));

    dir_->DeleteFile("tmp");
    EXPECT_FALSE(dir_->FileExists("tmp"));
    EXPECT_TRUE(dir_->List().empty());
}

TEST_F(RAMDirectoryTest, SeekBackwardAndForward) {
    auto out = dir_->CreateOutput("nums");
    for (int i = 0; i < 1000; ++i) {
        out->WriteVInt(i);
    }
    out->Close();

    auto in = dir_->OpenInput("nums");

    in->Seek(0);
    EXPECT_EQ(in->ReadVInt(), 0);

    in->Seek(500);
    // Seek to byte pos 500, then read — since VInt is variable-length,
    // we just verify we can seek around
    in->ReadVInt();

    in->Seek(0);
    EXPECT_EQ(in->ReadVInt(), 0);

    in->Close();
}

TEST_F(RAMDirectoryTest, FileExists) {
    EXPECT_FALSE(dir_->FileExists("nonexistent"));
    dir_->CreateOutput("exists");
    EXPECT_TRUE(dir_->FileExists("exists"));
}

TEST_F(RAMDirectoryTest, FileLength) {
    auto out = dir_->CreateOutput("len");
    out->WriteByte(0x01);
    out->WriteByte(0x02);
    out->WriteByte(0x03);
    out->Close();

    EXPECT_EQ(dir_->FileLength("len"), 3);
}

TEST_F(RAMDirectoryTest, RenameFile) {
    dir_->CreateOutput("old");
    dir_->RenameFile("old", "new");

    EXPECT_FALSE(dir_->FileExists("old"));
    EXPECT_TRUE(dir_->FileExists("new"));
}

}  // namespace store
}  // namespace minilucene
