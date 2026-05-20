#pragma once

#include <algorithm>
#include <vector>

namespace minilucene {
namespace util {

class Arrays {
public:
    template <typename T>
    static void MergeSort(std::vector<T>& a, int lo, int hi) {
        if (hi - lo <= 1) return;
        int mid = lo + (hi - lo) / 2;
        MergeSort(a, lo, mid);
        MergeSort(a, mid, hi);
        std::inplace_merge(a.begin() + lo, a.begin() + mid, a.begin() + hi);
    }
};

}  // namespace util
}  // namespace minilucene
