#include <gtest/gtest.h>
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwTextBuf.h"
#include <cstring>

using namespace cw;

class TextBufTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }
};

TEST_F(TextBufTest, CreateDestroy) {
    textBuf::handle_t h;
    rc_t rc = textBuf::create(h, 64, 64);
    ASSERT_EQ(rc, kOkRC);
    ASSERT_TRUE(h.isValid());
    
    EXPECT_STREQ(textBuf::text(h), "");
    
    rc = textBuf::destroy(h);
    EXPECT_EQ(rc, kOkRC);
    EXPECT_FALSE(h.isValid());
}

TEST_F(TextBufTest, AlternativeCreate) {
    textBuf::handle_t h = textBuf::create(64, 64);
    ASSERT_TRUE(h.isValid());
    textBuf::destroy(h);
}

TEST_F(TextBufTest, BasicPrint) {
    textBuf::handle_t h = textBuf::create(64, 64);
    
    textBuf::print(h, "Hello %s!", "World");
    EXPECT_STREQ(textBuf::text(h), "Hello World!");
    
    textBuf::print(h, " %d", 123);
    EXPECT_STREQ(textBuf::text(h), "Hello World! 123");
    
    textBuf::destroy(h);
}

TEST_F(TextBufTest, Clear) {
    textBuf::handle_t h = textBuf::create(64, 64);
    
    textBuf::print(h, "Some text");
    EXPECT_STREQ(textBuf::text(h), "Some text");
    
    textBuf::clear(h);
    
    textBuf::print(h, "New");
    EXPECT_STREQ(textBuf::text(h), "New");
    
    textBuf::destroy(h);
}

TEST_F(TextBufTest, AutoExpansion) {
    // Start with a very small buffer
    textBuf::handle_t h = textBuf::create(4, 4);
    
    // Print more than 4 chars
    textBuf::print(h, "1234567890");
    EXPECT_STREQ(textBuf::text(h), "1234567890");
    
    // Print even more
    textBuf::print(h, "abcdefghij");
    EXPECT_STREQ(textBuf::text(h), "1234567890abcdefghij");
    
    textBuf::destroy(h);
}

TEST_F(TextBufTest, PrintTyped) {
    textBuf::handle_t h = textBuf::create(64, 64);
    
    textBuf::printBool(h, true);
    textBuf::print(h, ",");
    textBuf::printBool(h, false);
    EXPECT_STREQ(textBuf::text(h), "true,false");
    
    textBuf::clear(h);
    textBuf::printInt(h, -42);
    EXPECT_STREQ(textBuf::text(h), "-42");
    
    textBuf::clear(h);
    textBuf::printUInt(h, 100);
    EXPECT_STREQ(textBuf::text(h), "100");
    
    textBuf::clear(h);
    textBuf::printFloat(h, 3.14);
    // Default %f is usually 6 decimal places
    EXPECT_STRCASEEQ(textBuf::text(h), "3.140000");
    
    textBuf::destroy(h);
}

TEST_F(TextBufTest, CustomBoolFormat) {
    textBuf::handle_t h = textBuf::create(64, 64);
    
    textBuf::setBoolFormat(h, true, "YES");
    textBuf::setBoolFormat(h, false, "NO");
    
    textBuf::printBool(h, true);
    textBuf::print(h, "/");
    textBuf::printBool(h, false);
    
    EXPECT_STREQ(textBuf::text(h), "YES/NO");
    
    textBuf::destroy(h);
}

TEST_F(TextBufTest, LargePrint) {
    textBuf::handle_t h = textBuf::create(10, 10);
    
    std::string large(1000, 'A');
    textBuf::print(h, "%s", large.c_str());
    
    EXPECT_EQ(strlen(textBuf::text(h)), 1000);
    EXPECT_STREQ(textBuf::text(h), large.c_str());
    
    textBuf::destroy(h);
}
