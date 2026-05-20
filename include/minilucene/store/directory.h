#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace minilucene {
namespace store {

class IndexInput;
class IndexOutput;

class Directory {
public:
    virtual ~Directory() = default;

    virtual std::vector<std::string> List() = 0;
    virtual bool FileExists(const std::string& name) = 0;
    virtual int64_t FileLength(const std::string& name) = 0;
    virtual void DeleteFile(const std::string& name) = 0;
    virtual void RenameFile(const std::string& from, const std::string& to) = 0;

    virtual std::unique_ptr<IndexOutput> CreateOutput(const std::string& name) = 0;
    virtual std::unique_ptr<IndexInput> OpenInput(const std::string& name) = 0;

    virtual void Close() = 0;
};

}  // namespace store
}  // namespace minilucene
