#pragma once

#include "minilucene/analysis/analyzer.h"

namespace minilucene {
namespace analysis {

class StopAnalyzer : public Analyzer {
public:
    std::unique_ptr<TokenStream> CreateTokenStream(
        const std::string& field, std::istream& input) override;
};

}  // namespace analysis
}  // namespace minilucene
