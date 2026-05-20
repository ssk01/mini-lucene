#include "minilucene/store/fs_directory.h"
#include "minilucene/store/index_input.h"
#include "minilucene/store/index_output.h"

#include <fstream>
#include <stdexcept>

namespace minilucene {
namespace store {

namespace {

class FSIndexOutput : public IndexOutput {
public:
    explicit FSIndexOutput(const std::filesystem::path& path)
        : file_(path, std::ios::binary) {}

    void WriteByte(uint8_t b) override {
        file_.put(static_cast<char>(b));
    }

    int64_t FilePointer() override {
        return file_.tellp();
    }

    void Flush() override {
        file_.flush();
    }

    void Close() override {
        file_.close();
    }

private:
    std::ofstream file_;
};

class FSIndexInput : public IndexInput {
public:
    explicit FSIndexInput(const std::filesystem::path& path)
        : file_(path, std::ios::binary) {
        if (!file_.is_open()) {
            throw std::runtime_error("cannot open file: " + path.string());
        }
        file_.seekg(0, std::ios::end);
        length_ = file_.tellg();
        file_.seekg(0, std::ios::beg);
    }

    uint8_t ReadByte() override {
        char c;
        file_.get(c);
        if (file_.fail()) {
            throw std::runtime_error("read beyond EOF");
        }
        return static_cast<uint8_t>(c);
    }

    void Seek(int64_t pos) override {
        file_.seekg(pos, std::ios::beg);
    }

    int64_t FilePointer() override {
        return file_.tellg();
    }

    int64_t Length() const override {
        return length_;
    }

    void Close() override {
        file_.close();
    }

private:
    std::ifstream file_;
    int64_t length_;
};

}  // namespace

FSDirectory::FSDirectory(const std::string& path)
    : dir_path_(path) {
    std::filesystem::create_directories(dir_path_);
}

FSDirectory::~FSDirectory() {
    Close();
}

std::vector<std::string> FSDirectory::List() {
    std::vector<std::string> names;
    for (const auto& entry : std::filesystem::directory_iterator(dir_path_)) {
        if (entry.is_regular_file()) {
            names.push_back(entry.path().filename().string());
        }
    }
    return names;
}

bool FSDirectory::FileExists(const std::string& name) {
    return std::filesystem::exists(dir_path_ / name);
}

int64_t FSDirectory::FileLength(const std::string& name) {
    return std::filesystem::file_size(dir_path_ / name);
}

void FSDirectory::DeleteFile(const std::string& name) {
    std::filesystem::remove(dir_path_ / name);
}

void FSDirectory::RenameFile(const std::string& from, const std::string& to) {
    std::filesystem::rename(dir_path_ / from, dir_path_ / to);
}

std::unique_ptr<IndexOutput> FSDirectory::CreateOutput(const std::string& name) {
    return std::make_unique<FSIndexOutput>(dir_path_ / name);
}

std::unique_ptr<IndexInput> FSDirectory::OpenInput(const std::string& name) {
    return std::make_unique<FSIndexInput>(dir_path_ / name);
}

void FSDirectory::Close() {}

}  // namespace store
}  // namespace minilucene
