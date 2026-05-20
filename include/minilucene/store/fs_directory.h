#pragma once

#include "minilucene/store/directory.h"

#include <filesystem>
#include <string>
#include <vector>

namespace minilucene {
namespace store {

class FSDirectory : public Directory {
public:
    explicit FSDirectory(const std::string& path);
    ~FSDirectory() override;

    std::vector<std::string> List() override;
    bool FileExists(const std::string& name) override;
    int64_t FileLength(const std::string& name) override;
    void DeleteFile(const std::string& name) override;
    void RenameFile(const std::string& from, const std::string& to) override;
    std::unique_ptr<IndexOutput> CreateOutput(const std::string& name) override;
    std::unique_ptr<IndexInput> OpenInput(const std::string& name) override;
    void Close() override;

private:
    std::filesystem::path dir_path_;
};

}  // namespace store
}  // namespace minilucene
