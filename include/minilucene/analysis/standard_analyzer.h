#pragma once

#include "minilucene/analysis/analyzer.h"

namespace minilucene {
namespace analysis {

class StandardAnalyzer : public Analyzer {
public:
    StandardAnalyzer();
    std::unique_ptr<TokenStream> CreateTokenStream(
        const std::string& field, std::istream& input) override;
};

}  // namespace analysis
}  // namespace minilucene
