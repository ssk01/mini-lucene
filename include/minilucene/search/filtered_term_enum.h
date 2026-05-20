#pragma once

#include "minilucene/index/term_enum.h"

#include <memory>

namespace minilucene {
namespace index {
class IndexReader;
}

namespace search {

class FilteredTermEnum : public index::TermEnum {
public:
    bool Next() override;
    const index::Term& Current() const override;
    int DocFreq() const override;
    void Close() override;

protected:
    virtual bool TermMatch(const index::Term& term) = 0;
    void SetEnum(std::unique_ptr<index::TermEnum> actual_enum);

    std::unique_ptr<index::TermEnum> actual_enum_;
    index::Term current_term_;
    int doc_freq_ = 0;
    bool ended_ = false;
};

}  // namespace search
}  // namespace minilucene
