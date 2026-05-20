#pragma once

#include <cstdint>
#include <string>

namespace minilucene {
namespace document {

class DateField {
public:
    static std::string TimeToString(int64_t time);
    static int64_t StringToTime(const std::string& s);

private:
    static const char BASE_36_DIGITS[];
};

}  // namespace document
}  // namespace minilucene
