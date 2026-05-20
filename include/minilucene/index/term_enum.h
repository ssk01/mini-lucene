#pragma once

#include "minilucene/index/term.h"

namespace minilucene {
namespace index {

class TermEnum {
public:
    virtual ~TermEnum() = default;
    virtual bool Next() = 0;
    virtual Term Current() const = 0;
    virtual int DocFreq() const = 0;
    virtual void Close() = 0;
};

}  // namespace index
}  // namespace minilucene
