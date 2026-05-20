#pragma once

#include <istream>
#include <memory>
#include <string>

namespace minilucene {
namespace analysis {

class TokenStream;

class Analyzer {
public:
    virtual ~Analyzer() = default;
    virtual std::unique_ptr<TokenStream> CreateTokenStream(
        const std::string& field, std::istream& input) = 0;
};

}  // namespace analysis
}  // namespace minilucene
