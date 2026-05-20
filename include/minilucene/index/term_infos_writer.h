#pragma once

#include "minilucene/index/term.h"
#include "minilucene/index/term_info.h"

#include <memory>

namespace minilucene {
namespace store {
class Directory;
class IndexOutput;
}

namespace index {

class TermInfosWriter {
public:
    TermInfosWriter(store::Directory& dir, const std::string& segment);
    ~TermInfosWriter();

    void Add(const Term& term, const TermInfo& ti);
    void Close();

private:
    std::unique_ptr<store::IndexOutput> tis_;
    std::unique_ptr<store::IndexOutput> tii_;
    int num_terms_ = 0;
    int64_t tis_offset_ = 0;
    bool closed_ = false;
};

}  // namespace index
}  // namespace minilucene
