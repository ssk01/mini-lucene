#include "minilucene/store/ram_directory.h"
#include "minilucene/store/index_input.h"
#include "minilucene/store/index_output.h"

#include <algorithm>
#include <stdexcept>

namespace minilucene {
namespace store {

namespace {

class RAMIndexOutput : public IndexOutput {
public:
    explicit RAMIndexOutput(RAMFilePtr file) : file_(std::move(file)) {}

    void WriteByte(uint8_t b) override {
        file_->data.push_back(b);
    }

    int64_t FilePointer() override {
        return static_cast<int64_t>(file_->data.size());
    }

    void Flush() override {}

    void Close() override {}

private:
    RAMFilePtr file_;
};

class RAMIndexInput : public IndexInput {
public:
    explicit RAMIndexInput(RAMFilePtr file)
        : file_(std::move(file)), pos_(0) {}

    uint8_t ReadByte() override {
        if (static_cast<size_t>(pos_) >= file_->data.size()) {
            throw std::out_of_range("read beyond EOF");
        }
        return file_->data[pos_++];
    }

    void Seek(int64_t pos) override {
        if (pos < 0 || static_cast<size_t>(pos) > file_->data.size()) {
            throw std::out_of_range("seek out of range");
        }
        pos_ = pos;
    }

    int64_t FilePointer() override {
        return pos_;
    }

    int64_t Length() const override {
        return static_cast<int64_t>(file_->data.size());
    }

    void Close() override {}

private:
    RAMFilePtr file_;
    int64_t pos_;
};

}  // namespace

RAMDirectory::~RAMDirectory() {
    Close();
}

std::vector<std::string> RAMDirectory::List() {
    std::vector<std::string> names;
    for (const auto& entry : files_) {
        names.push_back(entry.first);
    }
    return names;
}

bool RAMDirectory::FileExists(const std::string& name) {
    return files_.find(name) != files_.end();
}

int64_t RAMDirectory::FileLength(const std::string& name) {
    auto it = files_.find(name);
    if (it == files_.end()) {
        throw std::runtime_error("file not found: " + name);
    }
    return static_cast<int64_t>(it->second->data.size());
}

void RAMDirectory::DeleteFile(const std::string& name) {
    auto it = files_.find(name);
    if (it == files_.end()) {
        throw std::runtime_error("file not found: " + name);
    }
    files_.erase(it);
}

void RAMDirectory::RenameFile(const std::string& from, const std::string& to) {
    auto it = files_.find(from);
    if (it == files_.end()) {
        throw std::runtime_error("file not found: " + from);
    }
    files_[to] = it->second;
    files_.erase(it);
}

std::unique_ptr<IndexOutput> RAMDirectory::CreateOutput(const std::string& name) {
    auto file = std::make_shared<RAMFile>();
    files_[name] = file;
    return std::make_unique<RAMIndexOutput>(std::move(file));
}

std::unique_ptr<IndexInput> RAMDirectory::OpenInput(const std::string& name) {
    auto it = files_.find(name);
    if (it == files_.end()) {
        throw std::runtime_error("file not found: " + name);
    }
    return std::make_unique<RAMIndexInput>(it->second);
}

void RAMDirectory::Close() {
    files_.clear();
}

}  // namespace store
}  // namespace minilucene
