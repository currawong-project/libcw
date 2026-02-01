#include <gtest/gtest.h>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"

#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"

using namespace cw;


TEST(TextTest, TextLength)
{
  ASSERT_EQ(textLength(nullptr),0);
  ASSERT_EQ(textLength(""),0);
  ASSERT_EQ(textLength("1"),1);
  ASSERT_EQ(textLength("abc"),3);
  ASSERT_EQ(textLength("long string with spaces"),23);
}

TEST(TextTest, TextCopy)
{
  const char* src = "123456789";
  const unsigned dstN = 10;
  char dst[ dstN ];

  // Basic copy
  textCopy(dst, dstN, src);
  EXPECT_STREQ(dst, "123456789");

  // Copy with truncation due to dstN
  textCopy(dst, 5, src); // Should copy "1234" and null terminate
  EXPECT_STREQ(dst, "1234");
  EXPECT_EQ(strlen(dst), 4);

  // Copy with srcN truncation
  textCopy(dst, dstN, src, 3); // Should copy "123" and null terminate
  EXPECT_STREQ(dst, "123");
  EXPECT_EQ(strlen(dst), 3);

  // Copy with srcN less than actual length, and less than dstN
  textCopy(dst, dstN, src, 5); // Should copy "12345"
  EXPECT_STREQ(dst, "12345");
  EXPECT_EQ(strlen(dst), 5);

  // Copy empty string
  textCopy(dst, dstN, "");
  EXPECT_STREQ(dst, "");

  // Copy nullptr source
  textCopy(dst, dstN, nullptr);
  EXPECT_STREQ(dst, ""); // Should result in an empty string

  // Copy to dstN of 1 (only null terminator fits)
  char tiny_dst[1];
  textCopy(tiny_dst, 1, "abc");
  EXPECT_STREQ(tiny_dst, "");
  EXPECT_EQ(strlen(tiny_dst), 0);

  // Copy to nullptr dst (should return nullptr and not crash)
  // This cannot be directly tested with EXPECT_XXX as it involves internal behavior.
  // We rely on the function's internal nullptr check.
}


TEST(TextTest, TextCat)
{
  const unsigned dstN = 10;
  char dst[ dstN ] = "012"; // Initialize dst with some content

  // Basic concatenation
  textCat(dst, dstN, "345");
  EXPECT_STREQ(dst, "012345");

  // Concatenate with truncation due to dstN
  textCat(dst, dstN, "67890"); // dst is "012345", length 6. dstN is 10. Max append is 3 chars.
  EXPECT_STREQ(dst, "012345678"); // Should append "678" and null terminate
  EXPECT_EQ(strlen(dst), 9);

  // Reset dst
  strcpy(dst, "abc");
  // Concatenate with srcN truncation
  textCat(dst, dstN, "defgh", 2); // Should append "de"
  EXPECT_STREQ(dst, "abcde");
  EXPECT_EQ(strlen(dst), 5);

  // Concatenate empty string
  textCat(dst, dstN, "");
  EXPECT_STREQ(dst, "abcde"); // Should remain unchanged
  EXPECT_EQ(strlen(dst), 5);

  // Concatenate nullptr source
  textCat(dst, dstN, nullptr);
  EXPECT_STREQ(dst, "abcde"); // Should remain unchanged
  EXPECT_EQ(strlen(dst), 5);

  // Test when destination is already full
  strcpy(dst, "123456789");
  EXPECT_EQ(textCat(dst, dstN, "extra"), nullptr); // Should return nullptr if no space
  EXPECT_STREQ(dst, "123456789"); // Should remain unchanged
}

// Add new TEST blocks for other cwText functions

TEST(TextTest, CaseConversion) {
    char s_lower[] = "hello world";
    char s_upper[] = "HELLO WORLD";
    char s_mixed[] = "Hello World";
    char s_empty[] = "";
    char s_num[] = "123 ABC def";

    // textToLower(char* s)
    textToLower(s_upper);
    EXPECT_STREQ(s_upper, "hello world");
    textToLower(s_empty);
    EXPECT_STREQ(s_empty, "");
    char* s_null = nullptr;
    textToLower(s_null); // Should not crash
    EXPECT_EQ(s_null, nullptr);

    // textToUpper(char* s)
    textToUpper(s_lower);
    EXPECT_STREQ(s_lower, "HELLO WORLD");
    textToUpper(s_empty);
    EXPECT_STREQ(s_empty, "");
    textToUpper(s_null); // Should not crash
    EXPECT_EQ(s_null, nullptr);

    // textToLower(char* dst, const char* src, unsigned dstN)
    char dst_lower[20];
    textToLower(dst_lower, s_mixed, sizeof(dst_lower));
    EXPECT_STREQ(dst_lower, "hello world");
    textToLower(dst_lower, s_num, sizeof(dst_lower));
    EXPECT_STREQ(dst_lower, "123 abc def");
    textToLower(dst_lower, s_mixed, 5); // Truncation
    EXPECT_STREQ(dst_lower, "hell");
    textToLower(dst_lower, nullptr, sizeof(dst_lower)); // Null src
    EXPECT_STREQ(dst_lower, "hell");

    // textToUpper(char* dst, const char* src, unsigned dstN)
    char dst_upper[20];
    textToUpper(dst_upper, s_mixed, sizeof(dst_upper));
    EXPECT_STREQ(dst_upper, "HELLO WORLD");
    textToUpper(dst_upper, s_num, sizeof(dst_upper));
    EXPECT_STREQ(dst_upper, "123 ABC DEF");
    textToUpper(dst_upper, s_mixed, 5); // Truncation
    EXPECT_STREQ(dst_upper, "HELL");
    textToUpper(dst_upper, nullptr, sizeof(dst_upper)); // Null src
    EXPECT_STREQ(dst_upper, "HELL");
}

TEST(TextTest, Comparison) {
    // textCompare(const char* s0, const char* s1)
    EXPECT_EQ(textCompare("abc", "abc"), 0);
    EXPECT_LT(textCompare("abc", "abd"), 0);
    EXPECT_GT(textCompare("abd", "abc"), 0);
    EXPECT_EQ(textCompare("", ""), 0);
    EXPECT_GT(textCompare("abc", ""), 0);
    EXPECT_LT(textCompare("", "abc"), 0);
    EXPECT_EQ(textCompare(nullptr, nullptr), 0); // Both nullptr -> match
    EXPECT_GT(textCompare("abc", nullptr), 0); // One nullptr -> no match (s0 != s1 is 1)
    EXPECT_GT(textCompare(nullptr, "abc"), 0); // One nullptr -> no match (s0 != s1 is 1)

    // textCompare(const char* s0, const char* s1, unsigned n)
    EXPECT_EQ(textCompare("abcde", "abcde", 5), 0);
    EXPECT_EQ(textCompare("abcde", "abcfg", 3), 0);
    EXPECT_LT(textCompare("abcde", "abcfg", 4), 0);
    EXPECT_GT(textCompare("abcfg", "abcde", 4), 0);
    EXPECT_EQ(textCompare("abc", "def", 0), 0); // Compare 0 chars always equals
    EXPECT_EQ(textCompare(nullptr, nullptr, 5), 0);
    EXPECT_GT(textCompare("abc", nullptr, 3), 0);
    EXPECT_EQ(textCompare(nullptr, "abc", 3), 1);

    // textCompareI(const char* s0, const char* s1)
    EXPECT_EQ(textCompareI("abc", "ABC"), 0);
    EXPECT_EQ(textCompareI("Hello World", "hello world"), 0);
    EXPECT_LT(textCompareI("abc", "abd"), 0);
    EXPECT_GT(textCompareI("abd", "abc"), 0);
    EXPECT_EQ(textCompareI(nullptr, nullptr), 0);
    EXPECT_GT(textCompareI("abc", nullptr), 0);
    EXPECT_GT(textCompareI(nullptr, "abc"), 0);

    // textCompareI(const char* s0, const char* s1, unsigned n)
    EXPECT_EQ(textCompareI("abcde", "ABCDE", 5), 0);
    EXPECT_EQ(textCompareI("abcde", "ABCFG", 3), 0);
    EXPECT_LT(textCompareI("abcde", "ABCFG", 4), 0);
    EXPECT_GT(textCompareI("ABCFG", "abcde", 4), 0);
    EXPECT_EQ(textCompareI("abc", "def", 0), 0);
    EXPECT_EQ(textCompareI(nullptr, nullptr, 5), 0);
    EXPECT_GT(textCompareI("abc", nullptr, 3), 0);
    EXPECT_EQ(textCompareI(nullptr, "abc", 3), 1);
}

TEST(TextTest, WhitespaceManipulation) {
    const char* s1 = "   \t\n  hello world   \t\n";
    const char* s2 = "helloworld";
    const char* s3 = "   "; // All whitespace
    const char* s4 = "";    // Empty string

    // nextWhiteChar
    EXPECT_EQ(nextWhiteChar(s1), s1); // First char is whitespace
    EXPECT_EQ(nextWhiteChar(s1 + 12), s1 + 12); // ' ' in "world"
    EXPECT_EQ(nextWhiteChar(s2), nullptr); // No whitespace
    EXPECT_EQ(nextWhiteChar(nullptr), nullptr);

    // nextWhiteCharEOS
    EXPECT_EQ(nextWhiteCharEOS(s1), s1);
    EXPECT_EQ(nextWhiteCharEOS(s2), s2 + strlen(s2)); // Points to EOS
    EXPECT_EQ(nextWhiteCharEOS(nullptr), nullptr);

    // nextNonWhiteChar
    EXPECT_EQ(nextNonWhiteChar(s1), s1 + 7); // 'h' in "hello"
    EXPECT_EQ(nextNonWhiteChar(s2), s2); // First char is non-whitespace
    EXPECT_EQ(nextNonWhiteChar(s3), nullptr); // All whitespace returns nullptr
    EXPECT_EQ(nextNonWhiteChar(nullptr), nullptr);

    // nextNonWhiteCharEOS
    EXPECT_EQ(nextNonWhiteCharEOS(s1), s1 + 7);
    EXPECT_EQ(nextNonWhiteCharEOS(s2), s2);
    EXPECT_EQ(nextNonWhiteCharEOS(s3), s3 + strlen(s3)); // All whitespace, points to EOS
    EXPECT_EQ(nextNonWhiteCharEOS(nullptr), nullptr);

    // removeTrailingWhitespace
    char buf1[] = "hello world   \t\n";
    EXPECT_STREQ(removeTrailingWhitespace(buf1), "hello world");
    char buf2[] = "no_trailing_ws";
    EXPECT_STREQ(removeTrailingWhitespace(buf2), "no_trailing_ws");
    char buf3[] = "   ";
    EXPECT_STREQ(removeTrailingWhitespace(buf3), "");
    char buf4[] = "";
    EXPECT_STREQ(removeTrailingWhitespace(buf4), "");
    char* buf_null = nullptr;
    EXPECT_EQ(removeTrailingWhitespace(buf_null), nullptr);
}

TEST(TextTest, CharacterSearching) {
    char s[] = "abacada";
    const char* cs = "abacada";

    // firstMatchChar(char* s, char c)
    EXPECT_EQ(firstMatchChar(s, 'a'), s);
    EXPECT_EQ(firstMatchChar(s, 'b'), s + 1);
    EXPECT_EQ(firstMatchChar(s, 'z'), nullptr);
    EXPECT_EQ(firstMatchChar((const char*)nullptr, 'a'), nullptr);

    // firstMatchChar(const char* s, char c)
    EXPECT_EQ(firstMatchChar(cs, 'a'), cs);
    EXPECT_EQ(firstMatchChar(cs, 'b'), cs + 1);
    EXPECT_EQ(firstMatchChar(cs, 'z'), nullptr);
    EXPECT_EQ(firstMatchChar((const char*)nullptr, 'a'), nullptr);

    // firstMatchChar(char* s, unsigned sn, char c)
    EXPECT_EQ(firstMatchChar(s, 3, 'a'), s);      // 'a' in "aba"
    EXPECT_EQ(firstMatchChar(s, 3, 'c'), nullptr); // 'c' not in "aba"
    EXPECT_EQ(firstMatchChar(s, 0, 'a'), nullptr); // Search empty prefix
    EXPECT_EQ(firstMatchChar((const char*)nullptr, 5, 'a'), nullptr);

    // firstMatchChar(const char* s, unsigned sn, char c)
    EXPECT_EQ(firstMatchChar(cs, 3, 'a'), cs);
    EXPECT_EQ(firstMatchChar(cs, 3, 'c'), nullptr);
    EXPECT_EQ(firstMatchChar(cs, 0, 'a'), nullptr);
    EXPECT_EQ(firstMatchChar((const char*)nullptr, 5, 'a'), nullptr);

    // lastMatchChar(char* s, char c)
    EXPECT_EQ(lastMatchChar(s, 'a'), s + 6);
    EXPECT_EQ(lastMatchChar(s, 'd'), s + 5);
    EXPECT_EQ(lastMatchChar(s, 'z'), nullptr);
    EXPECT_EQ(lastMatchChar((const char*)nullptr, 'a'), nullptr);
    char empty_s[] = "";
    EXPECT_EQ(lastMatchChar(empty_s, 'a'), nullptr);

    // lastMatchChar(const char* s, char c)
    EXPECT_EQ(lastMatchChar(cs, 'a'), cs + 6);
    EXPECT_EQ(lastMatchChar(cs, 'd'), cs + 5);
    EXPECT_EQ(lastMatchChar(cs, 'z'), nullptr);
    EXPECT_EQ(lastMatchChar((const char*)nullptr, 'a'), nullptr);
    const char* empty_cs = "";
    EXPECT_EQ(lastMatchChar(empty_cs, 'a'), nullptr);
}

TEST(TextTest, Validation) {
    // isInteger
    EXPECT_TRUE(isInteger("123"));
    EXPECT_TRUE(isInteger("0"));
    EXPECT_TRUE(isInteger("9876543210"));
    EXPECT_FALSE(isInteger("-123")); // current isInteger returns false for negative
    EXPECT_FALSE(isInteger("123.0"));
    EXPECT_FALSE(isInteger("abc"));
    EXPECT_FALSE(isInteger(""));
    EXPECT_FALSE(isInteger(nullptr)); // Assuming nullptr returns false based on implementation
    
    // isReal
    EXPECT_TRUE(isReal("123.45"));
    EXPECT_TRUE(isReal(".5"));
    EXPECT_TRUE(isReal("1."));
    EXPECT_TRUE(isReal("0.0"));
    EXPECT_TRUE(isReal("123")); // An integer is also a real
    EXPECT_FALSE(isReal("1.2.3")); // Multiple decimal points
    EXPECT_FALSE(isReal("abc"));
    EXPECT_FALSE(isReal(""));
    EXPECT_FALSE(isReal(nullptr)); // Assuming nullptr returns false

    // isIdentifier
    EXPECT_TRUE(isIdentifier("myVar"));
    EXPECT_TRUE(isIdentifier("_private"));
    EXPECT_TRUE(isIdentifier("VAR_NAME123"));
    EXPECT_FALSE(isIdentifier("123var")); // Starts with number
    EXPECT_FALSE(isIdentifier("my-var")); // Contains hyphen
    EXPECT_FALSE(isIdentifier(""));
    EXPECT_FALSE(isIdentifier(nullptr)); // Assuming nullptr returns false
}

TEST(TextTest, JoiningAndAppending) {
    // textJoin(const char* s0, const char* s1)
    char* joined = textJoin("Hello", "World");
    EXPECT_STREQ(joined, "HelloWorld");
    mem::release(joined);

    joined = textJoin("Prefix ", "Suffix");
    EXPECT_STREQ(joined, "Prefix Suffix");
    mem::release(joined);

    joined = textJoin("Single", nullptr);
    EXPECT_STREQ(joined, "Single");
    mem::release(joined);

    joined = textJoin(nullptr, "Single");
    EXPECT_STREQ(joined, "Single");
    mem::release(joined);
    
    joined = textJoin(nullptr, nullptr);
    EXPECT_EQ(joined, nullptr); // Both null
    mem::release(joined); // Should handle nullptr gracefully

    // textJoin(const char* sep, const char** subStrArray, unsigned ssN)
    const char* strs1[] = {"one", "two", "three"};
    joined = textJoin("-", strs1, 3);
    EXPECT_STREQ(joined, "one-two-three");
    mem::release(joined);

    const char* strs2[] = {"single"};
    joined = textJoin(" ", strs2, 1);
    EXPECT_STREQ(joined, "single");
    mem::release(joined);

    const char* strs3[] = {}; // Empty array
    joined = textJoin(" ", strs3, 0);
    EXPECT_EQ(joined, nullptr);
    mem::release(joined);

    const char* strs4[] = {"a", nullptr, "c"}; // With a null string in array - behavior depends on implementation
    joined = textJoin(":", strs4, 3);
    EXPECT_STREQ(joined, "a:c"); // Behavior of textLength(nullptr) is 0
    mem::release(joined);

    // textAppend(char* s0, const char* s1)
    char* app_s0 = mem::duplStr("Initial");
    app_s0 = textAppend(app_s0, "Appended");
    EXPECT_STREQ(app_s0, "InitialAppended");
    mem::release(app_s0);

    app_s0 = mem::duplStr("Start");
    app_s0 = textAppend(app_s0, nullptr); // Append nullptr
    EXPECT_STREQ(app_s0, "Start");
    mem::release(app_s0);

    char* app_null = nullptr;
    app_null = textAppend(app_null, "First"); // Append to nullptr
    EXPECT_STREQ(app_null, "First");
    mem::release(app_null);

    app_null = textAppend(nullptr, nullptr); // Both nullptr
    EXPECT_EQ(app_null, nullptr);
    mem::release(app_null);
}

TEST(TextTest, TypeToTextConversion) {
    char buf[64];
    unsigned len;

    // bool
    len = toText(buf, sizeof(buf), true);
    EXPECT_STREQ(buf, "true");
    EXPECT_EQ(len, strlen("true"));
    len = toText(buf, sizeof(buf), false);
    EXPECT_STREQ(buf, "false");
    EXPECT_EQ(len, strlen("false"));

    // char
    len = toText(buf, sizeof(buf), 'A');
    EXPECT_STREQ(buf, "A");
    EXPECT_EQ(len, 1);

    // unsigned char
    len = toText(buf, sizeof(buf), (unsigned char)'Z');
    EXPECT_STREQ(buf, "Z");
    EXPECT_EQ(len, 1);
    
    // short
    len = toText(buf, sizeof(buf), (short)-123);
    EXPECT_STREQ(buf, "-123");
    len = toText(buf, sizeof(buf), (short)456);
    EXPECT_STREQ(buf, "456");

    // unsigned short
    len = toText(buf, sizeof(buf), (unsigned short)789);
    EXPECT_STREQ(buf, "789");

    // int
    len = toText(buf, sizeof(buf), -12345);
    EXPECT_STREQ(buf, "-12345");
    len = toText(buf, sizeof(buf), 67890);
    EXPECT_STREQ(buf, "67890");

    // unsigned int
    len = toText(buf, sizeof(buf), (unsigned int)98765);
    EXPECT_STREQ(buf, "98765");

    // long long
    len = toText(buf, sizeof(buf), (long long)-1234567890LL);
    EXPECT_STREQ(buf, "-1234567890");
    
    // unsigned long long
    len = toText(buf, sizeof(buf), (unsigned long long)9876543210ULL);
    EXPECT_STREQ(buf, "9876543210");

    // float
    len = toText(buf, sizeof(buf), 1.23f);
    // Note: Comparing float to string representation can be tricky due to precision
    // EXPECT_STREQ(buf, "1.230000"); // Depends on snprintf implementation
    // Instead, convert back and compare or check format
    float f_val;
    sscanf(buf, "%f", &f_val);
    EXPECT_FLOAT_EQ(f_val, 1.23f);
    EXPECT_GT(len, 0); // Ensure some characters were written

    // double
    len = toText(buf, sizeof(buf), 4.567);
    double d_val;
    sscanf(buf, "%lf", &d_val);
    EXPECT_DOUBLE_EQ(d_val, 4.567);
    EXPECT_GT(len, 0);

    // const char*
    len = toText(buf, sizeof(buf), "Test String");
    EXPECT_STREQ(buf, "Test String");
    EXPECT_EQ(len, strlen("Test String"));

    // toText buffer overflow
    char small_buf[5];
    len = toText(small_buf, sizeof(small_buf), "Long String");
    EXPECT_STREQ(small_buf, "Long"); // Truncated
    EXPECT_EQ(len, 0); // On overflow, toText returns 0

    // toText nullptr source (for const char* overload)
    len = toText(buf, sizeof(buf), nullptr);
    EXPECT_EQ(len, 0); // Should return 0 and log an error
}


