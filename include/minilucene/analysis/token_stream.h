#pragma once

namespace minilucene {
namespace analysis {

class Token;

class TokenStream {
public:
    virtual ~TokenStream() = default;
    virtual bool Next(Token* token) = 0;
};

}  // namespace analysis
}  // namespace minilucene
