#pragma once

#include "minilucene/index/term.h"
#include "minilucene/index/term_info.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace minilucene {
namespace store {
class Directory;
class IndexInput;
}

namespace index {

class TermInfosReader {
public:
    TermInfosReader(store::Directory& dir, const std::string& segment);
    ~TermInfosReader();

    TermInfo Get(const Term& target);
    void Close();

private:
    struct TiiEntry {
        Term term;
        int64_t tis_offset;
    };

    TermInfo ReadTisEntry(store::IndexInput& tis);

    std::vector<TiiEntry> tii_entries_;
    std::unique_ptr<store::IndexInput> tis_;
};

}  // namespace index
}  // namespace minilucene
