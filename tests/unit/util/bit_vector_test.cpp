#include "minilucene/util/bit_vector.h"
#include <gtest/gtest.h>

namespace minilucene {
namespace util {

TEST(BitVector, SetAndGet) {
    BitVector bv(1000);
    for (int i = 0; i < 1000; ++i) {
        if (i % 2 == 0) bv.Set(i);
    }
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(bv.Get(i), i % 2 == 0);
    }

    for (int i = 0; i < 1000; ++i) {
        if (i % 3 == 0) bv.Clear(i);
    }
    for (int i = 0; i < 1000; ++i) {
        bool expected = (i % 2 == 0) && (i % 3 != 0);
        EXPECT_EQ(bv.Get(i), expected);
    }
}

TEST(BitVector, Count) {
    BitVector bv(1000);
    EXPECT_EQ(bv.Count(), 0);

    for (int i = 0; i < 500; ++i) {
        bv.Set(i * 2);
    }
    EXPECT_EQ(bv.Count(), 500);

    bv.Clear(0);
    EXPECT_EQ(bv.Count(), 499);
}

TEST(BitVector, BoundaryBits) {
    BitVector bv(100);
    bv.Set(0);
    EXPECT_TRUE(bv.Get(0));

    bv.Set(7);
    EXPECT_TRUE(bv.Get(7));

    bv.Set(8);
    EXPECT_TRUE(bv.Get(8));

    bv.Set(99);
    EXPECT_TRUE(bv.Get(99));
    bv.Clear(99);
    EXPECT_FALSE(bv.Get(99));
}

TEST(BitVector, OutOfRangeThrows) {
    BitVector bv(100);
    EXPECT_THROW(bv.Set(100), std::out_of_range);
    EXPECT_THROW(bv.Get(100), std::out_of_range);
    EXPECT_THROW(bv.Clear(100), std::out_of_range);
    EXPECT_THROW(bv.Set(-1), std::out_of_range);
}

}  // namespace util
}  // namespace minilucene
