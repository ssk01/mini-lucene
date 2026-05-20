#include "minilucene/document/date_field.h"

#include <algorithm>

namespace minilucene {
namespace document {

const char DateField::BASE_36_DIGITS[] = "0123456789abcdefghijklmnopqrstuvwxyz";

std::string DateField::TimeToString(int64_t time) {
    uint64_t t = static_cast<uint64_t>(time);
    std::string result(7, '0');
    for (int i = 6; i >= 0; --i) {
        result[i] = BASE_36_DIGITS[t % 36];
        t /= 36;
    }
    return result;
}

int64_t DateField::StringToTime(const std::string& s) {
    int64_t result = 0;
    for (char c : s) {
        result *= 36;
        if (c >= '0' && c <= '9') result += (c - '0');
        else if (c >= 'a' && c <= 'z') result += (c - 'a' + 10);
        else if (c >= 'A' && c <= 'Z') result += (c - 'A' + 10);
    }
    return result;
}

}  // namespace document
}  // namespace minilucene
