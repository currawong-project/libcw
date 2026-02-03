#include <gtest/gtest.h>
#include <limits>
#include <cstdint>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwText.h" // For nextNonWhiteChar, used in string_to_number<bool>
#include "cwNumericConvert.h"

using namespace cw;

// --- Tests for numeric_convert ---

TEST(NumericConvertTest, SameTypeConversion) {
    int src = 42, dst = 0;
    EXPECT_EQ(numeric_convert(src, dst), kOkRC);
    EXPECT_EQ(dst, 42);

    double d_src = 3.14, d_dst = 0.0;
    EXPECT_EQ(numeric_convert(d_src, d_dst), kOkRC);
    EXPECT_DOUBLE_EQ(d_dst, 3.14);
}

TEST(NumericConvertTest, WideningConversion) {
    int8_t src = 100;
    int32_t dst = 0;
    EXPECT_EQ(numeric_convert(src, dst), kOkRC);
    EXPECT_EQ(dst, 100);

    float f_src = 1.23f;
    double d_dst = 0.0;
    EXPECT_EQ(numeric_convert(f_src, d_dst), kOkRC);
    EXPECT_DOUBLE_EQ(d_dst, 1.23f);
}

TEST(NumericConvertTest, NarrowingConversionSuccess) {
    int32_t src = 120;
    int8_t dst = 0;
    EXPECT_EQ(numeric_convert(src, dst), kOkRC);
    EXPECT_EQ(dst, 120);

    double d_src = 255.0;
    uint8_t u8_dst = 0;
    EXPECT_EQ(numeric_convert(d_src, u8_dst), kOkRC);
    EXPECT_EQ(u8_dst, 255);
    
    double d_src_zero = 0.0;
    uint8_t u8_dst_zero = 1;
    EXPECT_EQ(numeric_convert(d_src_zero, u8_dst_zero), kOkRC);
    EXPECT_EQ(u8_dst_zero, 0);
}

TEST(NumericConvertTest, NarrowingConversionFailure) {
    int32_t src = 300;
    int8_t dst = 0;
    EXPECT_NE(numeric_convert(src, dst), kOkRC); // 300 is out of range for int8_t

    uint64_t large_src = std::numeric_limits<uint64_t>::max();
    int32_t i32_dst = 0;
    EXPECT_NE(numeric_convert(large_src, i32_dst), kOkRC);

    double d_src = std::numeric_limits<double>::max();
    float f_dst = 0.0f;
    EXPECT_NE(numeric_convert(d_src, f_dst), kOkRC);
}


// --- Tests for numeric_convert2 ---

TEST(NumericConvert2Test, SmallerSourceType) {
    int8_t src = 50;
    int32_t dst = 0;
    EXPECT_EQ(numeric_convert2(src, dst, (int32_t)0, (int32_t)100), kOkRC);
    EXPECT_EQ(dst, 50);
}

TEST(NumericConvert2Test, LargerSourceTypeInRange) {
    int32_t src = 75;
    int8_t dst = 0;
    EXPECT_EQ(numeric_convert2(src, dst, (int8_t)0, (int8_t)100), kOkRC);
    EXPECT_EQ(dst, 75);
}

TEST(NumericConvert2Test, LargerSourceTypeOutOfRange) {
    int32_t src_high = 150;
    int8_t dst = 0;
    EXPECT_NE(numeric_convert2(src_high, dst, (int8_t)0, (int8_t)100), kOkRC);

    int32_t src_low = -50;
    EXPECT_NE(numeric_convert2(src_low, dst, (int8_t)0, (int8_t)100), kOkRC);
}


// --- Tests for string_to_number ---

TEST(StringToNumberTest, IntegerSuccess) {
    int val = 0;
    EXPECT_EQ(string_to_number("123", val), kOkRC);
    EXPECT_EQ(val, 123);

    EXPECT_EQ(string_to_number("0", val), kOkRC);
    EXPECT_EQ(val, 0);

    EXPECT_EQ(string_to_number("-45", val), kOkRC);
    EXPECT_EQ(val, -45);
}

TEST(StringToNumberTest, HexIntegerSuccess) {
    int val = 0;
    EXPECT_EQ(string_to_number("0xFF", val), kOkRC);
    EXPECT_EQ(val, 255);

    unsigned int u_val = 0;
    EXPECT_EQ(string_to_number("0x10", u_val), kOkRC);
    EXPECT_EQ(u_val, 16);
}

TEST(StringToNumberTest, IntegerFailure) {
    int val = 0;
    EXPECT_NE(string_to_number("abc", val), kOkRC);
    EXPECT_NE(string_to_number("123a", val), kOkRC); // strtol would parse this, but the logic might fail later. Let's check behavior.
    EXPECT_EQ(string_to_number("123a", val), kOkRC); // strtol parses the "123" part. This is expected.
    EXPECT_EQ(val, 123);
    
    EXPECT_NE(string_to_number(nullptr, val), kOkRC);
    EXPECT_NE(string_to_number("", val), kOkRC);
}

TEST(StringToNumberTest, IntegerOverflow) {
    uint8_t val = 0;
    // This value is larger than uint8_t max
    EXPECT_NE(string_to_number("256", val), kOkRC);
}

TEST(StringToNumberTest, DoubleSuccess) {
    double val = 0.0;
    EXPECT_EQ(string_to_number("123.45", val), kOkRC);
    EXPECT_DOUBLE_EQ(val, 123.45);

    EXPECT_EQ(string_to_number("-0.5", val), kOkRC);
    EXPECT_DOUBLE_EQ(val, -0.5);
    
    EXPECT_EQ(string_to_number("1.23e2", val), kOkRC);
    EXPECT_DOUBLE_EQ(val, 123.0);
}

TEST(StringToNumberTest, DoubleFailure) {
    double val = 0.0;
    EXPECT_NE(string_to_number("nan", val), kOkRC);
    EXPECT_NE(string_to_number("inf", val), kOkRC);
    EXPECT_NE(string_to_number(nullptr, val), kOkRC);
    EXPECT_NE(string_to_number("", val), kOkRC);
}

TEST(StringToNumberTest, FloatSuccess) {
    float val = 0.0f;
    EXPECT_EQ(string_to_number("98.76", val), kOkRC);
    EXPECT_FLOAT_EQ(val, 98.76f);
}

TEST(StringToNumberTest, FloatOverflow) {
    float val = 0.0f;
    // This double value is too large to fit in a float
    EXPECT_NE(string_to_number("1e39", val), kOkRC);
}

TEST(StringToNumberTest, BoolSuccess) {
    bool val = false;
    EXPECT_EQ(string_to_number("true", val), kOkRC);
    EXPECT_TRUE(val);

    EXPECT_EQ(string_to_number("false", val), kOkRC);
    EXPECT_FALSE(val);

    // Test with leading whitespace
    val = false;
    EXPECT_EQ(string_to_number("  \t true", val), kOkRC);
    EXPECT_TRUE(val);
}

TEST(StringToNumberTest, BoolFailure) {
    bool val = true;
    EXPECT_NE(string_to_number("True", val), kOkRC); // case-sensitive
    EXPECT_NE(string_to_number("1", val), kOkRC);
    EXPECT_NE(string_to_number("random", val), kOkRC);
    EXPECT_NE(string_to_number(nullptr, val), kOkRC);
    EXPECT_NE(string_to_number("", val), kOkRC);
}

// --- Tests for number_to_string ---

TEST(NumberToStringTest, Integer) {
    char buf[32];
    int len = number_to_string(12345, buf, sizeof(buf));
    EXPECT_STREQ(buf, "12345");
    EXPECT_EQ(len, 5);

    len = number_to_string(-987, buf, sizeof(buf));
    EXPECT_STREQ(buf, "-987");
    EXPECT_EQ(len, 4);
}

TEST(NumberToStringTest, Unsigned) {
    char buf[32];
    int len = number_to_string(4294967295U, buf, sizeof(buf));
    EXPECT_STREQ(buf, "4294967295");
    EXPECT_EQ(len, 10);
}

TEST(NumberToStringTest, Double) {
    char buf[32];
    int len = number_to_string(123.456, buf, sizeof(buf));
    EXPECT_STREQ(buf, "123.456000"); // Default format is "%f"
    EXPECT_EQ(len, 10);
}

TEST(NumberToStringTest, CustomFormat) {
    char buf[32];
    int len = number_to_string(255, buf, sizeof(buf), "0x%X");
    EXPECT_STREQ(buf, "0xFF");
    EXPECT_EQ(len, 4);

    len = number_to_string(123.456, buf, sizeof(buf), "%.2f");
    EXPECT_STREQ(buf, "123.46"); // snprintf will round
    EXPECT_EQ(len, 6);
}

TEST(NumberToStringTest, BufferTruncation) {
    char buf[5];
    int len = number_to_string(123456789, buf, sizeof(buf));
    // snprintf writes N-1 chars and a null terminator.
    EXPECT_STREQ(buf, "1234");
    // snprintf returns the number of chars that *would* have been written.
    EXPECT_EQ(len, 9);
}

