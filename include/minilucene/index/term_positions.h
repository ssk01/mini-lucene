#pragma once

#include "minilucene/index/term_docs.h"

namespace minilucene {
namespace index {

class TermPositions : public TermDocs {
public:
    virtual int NextPosition() = 0;
};

}  // namespace index
}  // namespace minilucene
