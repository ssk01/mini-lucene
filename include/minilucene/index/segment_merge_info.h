#pragma once

#include "minilucene/index/term.h"
#include "minilucene/index/term_enum.h"
#include "minilucene/index/term_positions.h"

#include <memory>
#include <vector>

namespace minilucene {
namespace index {

class SegmentReader;

class SegmentMergeInfo {
public:
    SegmentMergeInfo(int base, std::unique_ptr<TermEnum> term_enum,
                     SegmentReader* reader);
    ~SegmentMergeInfo();

    bool Next();
    void Close();

    Term term_;
    int base_;
    SegmentReader* reader_;
    std::unique_ptr<TermEnum> term_enum_;
    std::unique_ptr<TermPositions> postings_;
    std::vector<int> doc_map_;
};

}  // namespace index
}  // namespace minilucene
