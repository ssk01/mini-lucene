#pragma once

#include "minilucene/analysis/token_stream.h"

#include <memory>

namespace minilucene {
namespace analysis {

class TokenFilter : public TokenStream {
public:
    explicit TokenFilter(std::unique_ptr<TokenStream> input)
        : input_(std::move(input)) {}
    ~TokenFilter() override = default;

    TokenFilter(const TokenFilter&) = delete;
    TokenFilter& operator=(const TokenFilter&) = delete;

protected:
    std::unique_ptr<TokenStream> input_;
};

}  // namespace analysis
}  // namespace minilucene
