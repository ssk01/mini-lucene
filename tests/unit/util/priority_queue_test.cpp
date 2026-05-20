#include "minilucene/util/priority_queue.h"
#include <gtest/gtest.h>
#include <vector>
#include <algorithm>
#include <numeric>
#include <random>

namespace minilucene {
namespace util {

class IntPriorityQueue : public PriorityQueue<int> {
protected:
    bool LessThan(const int& a, const int& b) const override {
        return a < b;
    }
};

TEST(PriorityQueue, MinHeapInvariant) {
    IntPriorityQueue pq;
    pq.Initialize(0);

    std::vector<int> values(1000);
    std::iota(values.begin(), values.end(), 0);
    std::shuffle(values.begin(), values.end(), std::mt19937(42));

    for (int v : values) {
        pq.Put(v);
    }

    EXPECT_EQ(pq.Size(), 1000);
    int prev = -1;
    while (pq.Size() > 0) {
        int top = pq.Top();
        EXPECT_GE(top, prev);
        prev = top;
        pq.Pop();
    }
}

TEST(PriorityQueue, FixedSizeTopK) {
    IntPriorityQueue pq;
    pq.Initialize(10);

    for (int i = 0; i < 100; ++i) {
        pq.Put(i);
    }

    EXPECT_EQ(pq.Size(), 10);

    std::vector<int> results;
    while (pq.Size() > 0) {
        results.push_back(pq.Top());
        pq.Pop();
    }

    EXPECT_EQ(results.size(), 10);
    for (int v : results) {
        EXPECT_GE(v, 90);
        EXPECT_LE(v, 99);
    }
    for (size_t i = 1; i < results.size(); ++i) {
        EXPECT_GE(results[i], results[i - 1]);
    }
}

TEST(PriorityQueue, PutWithEvict) {
    IntPriorityQueue pq;
    pq.Initialize(3);

    pq.Put(10);
    pq.Put(20);
    pq.Put(30);
    EXPECT_EQ(pq.Size(), 3);
    EXPECT_EQ(pq.Top(), 10);

    pq.Put(5);
    EXPECT_EQ(pq.Size(), 3);
    EXPECT_EQ(pq.Top(), 10);

    pq.Put(25);
    EXPECT_EQ(pq.Size(), 3);
    EXPECT_EQ(pq.Top(), 20);
}

}  // namespace util
}  // namespace minilucene
