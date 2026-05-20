#pragma once

#include "minilucene/store/directory.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace minilucene {
namespace store {

struct RAMFile {
    std::vector<uint8_t> data;
};
using RAMFilePtr = std::shared_ptr<RAMFile>;

class RAMDirectory : public Directory {
public:
    RAMDirectory() = default;
    ~RAMDirectory() override;

    std::vector<std::string> List() override;
    bool FileExists(const std::string& name) override;
    int64_t FileLength(const std::string& name) override;
    void DeleteFile(const std::string& name) override;
    void RenameFile(const std::string& from, const std::string& to) override;
    std::unique_ptr<IndexOutput> CreateOutput(const std::string& name) override;
    std::unique_ptr<IndexInput> OpenInput(const std::string& name) override;
    void Close() override;

private:
    std::map<std::string, RAMFilePtr> files_;
};

}  // namespace store
}  // namespace minilucene
