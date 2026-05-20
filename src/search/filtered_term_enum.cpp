#include "minilucene/search/filtered_term_enum.h"

namespace minilucene {
namespace search {

void FilteredTermEnum::SetEnum(std::unique_ptr<index::TermEnum> actual_enum) {
    actual_enum_ = std::move(actual_enum);
}

bool FilteredTermEnum::Next() {
    // STUB: returns false
    return false;
}

const index::Term& FilteredTermEnum::Current() const { return current_term_; }
int FilteredTermEnum::DocFreq() const { return doc_freq_; }
void FilteredTermEnum::Close() { if (actual_enum_) actual_enum_->Close(); }

}  // namespace search
}  // namespace minilucene
