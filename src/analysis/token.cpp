#include "minilucene/analysis/token.h"

namespace minilucene {
namespace analysis {

Token::Token(std::string text, int start_offset, int end_offset, std::string type)
    : text_(std::move(text))
    , start_offset_(start_offset)
    , end_offset_(end_offset)
    , type_(std::move(type)) {}

}  // namespace analysis
}  // namespace minilucene
