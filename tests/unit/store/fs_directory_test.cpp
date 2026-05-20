#include "minilucene/store/fs_directory.h"
#include "minilucene/store/index_input.h"
#include "minilucene/store/index_output.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <filesystem>
#include <random>
#include <vector>

namespace minilucene {
namespace store {

class FSDirectoryTest : public testing::Test {
protected:
    void SetUp() override {
        path_ = std::filesystem::temp_directory_path() / "minilucene_fs_test_XXXXXX";
        mkdtemp(const_cast<char*>(path_.c_str()));
        dir_ = std::make_unique<FSDirectory>(path_);
    }

    void TearDown() override {
        dir_.reset();
        std::filesystem::remove_all(path_);
    }

    std::filesystem::path path_;
    std::unique_ptr<FSDirectory> dir_;
};

TEST_F(FSDirectoryTest, ListReflectsCreatedFiles) {
    EXPECT_TRUE(dir_->List().empty());

    dir_->CreateOutput("a");
    dir_->CreateOutput("b");

    auto files = dir_->List();
    EXPECT_EQ(files.size(), 2);
}

TEST_F(FSDirectoryTest, ReadWriteRoundTrip) {
    std::vector<uint8_t> test_data(1024 * 16);
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

TEST_F(FSDirectoryTest, FileExists) {
    EXPECT_FALSE(dir_->FileExists("nonexistent"));
    dir_->CreateOutput("exists");
    EXPECT_TRUE(dir_->FileExists("exists"));
}

TEST_F(FSDirectoryTest, FileLength) {
    auto out = dir_->CreateOutput("len");
    out->WriteByte(0x01);
    out->WriteByte(0x02);
    out->Close();

    EXPECT_EQ(dir_->FileLength("len"), 2);
}

TEST_F(FSDirectoryTest, DeleteFile) {
    dir_->CreateOutput("tmp");
    EXPECT_TRUE(dir_->FileExists("tmp"));

    dir_->DeleteFile("tmp");
    EXPECT_FALSE(dir_->FileExists("tmp"));
}

TEST_F(FSDirectoryTest, RenameFile) {
    dir_->CreateOutput("old");
    dir_->RenameFile("old", "new");

    EXPECT_FALSE(dir_->FileExists("old"));
    EXPECT_TRUE(dir_->FileExists("new"));
}

TEST_F(FSDirectoryTest, SeekBackwardAndForward) {
    auto out = dir_->CreateOutput("data");
    for (int i = 0; i < 256; ++i) {
        out->WriteByte(static_cast<uint8_t>(i));
    }
    out->Close();

    auto in = dir_->OpenInput("data");
    in->Seek(255);
    EXPECT_EQ(in->ReadByte(), 255);

    in->Seek(0);
    EXPECT_EQ(in->ReadByte(), 0);

    in->Seek(128);
    EXPECT_EQ(in->ReadByte(), 128);

    in->Close();
}

TEST_F(FSDirectoryTest, VIntRoundTrip) {
    auto out = dir_->CreateOutput("vint");
    for (int v : {0, 1, 127, 128, 16383, 16384, INT_MAX}) {
        out->WriteVInt(v);
    }
    out->Close();

    auto in = dir_->OpenInput("vint");
    for (int expected : {0, 1, 127, 128, 16383, 16384, INT_MAX}) {
        EXPECT_EQ(in->ReadVInt(), expected);
    }
    in->Close();
}

}  // namespace store
}  // namespace minilucene
