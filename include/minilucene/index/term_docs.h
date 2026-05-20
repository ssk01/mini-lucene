#pragma once

#include "minilucene/index/term_info.h"

namespace minilucene {
namespace index {

class TermDocs {
public:
    virtual ~TermDocs() = default;
    virtual bool Next() = 0;
    virtual int Doc() const = 0;
    virtual int Freq() const = 0;
    virtual void Close() = 0;
};

}  // namespace index
}  // namespace minilucene
