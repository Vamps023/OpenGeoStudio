#pragma once
// Minimal gtest stub — only provides macros used by LaneMaker code
#include <string>
#include <sstream>

// Helper class that accepts << and does nothing
struct GTestNullStream {
    template<typename T>
    GTestNullStream& operator<<(const T&) { return *this; }
};
static GTestNullStream g_nullStream;

#define EXPECT_TRUE(cond) g_nullStream
#define EXPECT_FALSE(cond) g_nullStream
#define EXPECT_EQ(a, b) g_nullStream
#define EXPECT_NE(a, b) g_nullStream
#define EXPECT_NEAR(a, b, c) g_nullStream
#define ASSERT_TRUE(cond) g_nullStream
#define ASSERT_FALSE(cond) g_nullStream
#define ASSERT_EQ(a, b) g_nullStream
#define ASSERT_NE(a, b) g_nullStream
#define FAIL() g_nullStream
#define SUCCEED() g_nullStream
#define TEST(suite, name) static void suite##_##name()
#define TEST_F(fixture, name) static void fixture##_##name()
