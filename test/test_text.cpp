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
}

TEST(TextTest, TextCopy)
{
  const char* src = "123";
  const unsigned dstN = 10;
  char dst[ dstN ];

  textCopy(dst,dstN,src);
  EXPECT_STREQ(dst,"123");
  textCopy(dst+3,dstN,src);
  EXPECT_STREQ(dst,"123123");
  textCopy(dst+6,dstN,src);
  EXPECT_STREQ(dst,"123123123");
  textCopy(dst+12,dstN,src);
  EXPECT_STREQ(dst,"123123123");
  textCopy(dst+15,dstN,src);
  EXPECT_STREQ(dst,"123123123");
}


TEST(TextTest, TextCat)
{
  const char* src = "123";
  const unsigned dstN = 10;
  char dst[ dstN ];

  textCat(dst,dstN,src);
  EXPECT_STREQ(dst,"123") << "a:123";
  
  textCat(dst,dstN,src);
  EXPECT_STREQ(dst,"123123") << "b:123123";
  
  textCat(dst,dstN,"12");
  EXPECT_STREQ(dst,"12312312") << "c:12312312";

  textCat(dst,dstN,"31");
  EXPECT_STREQ(dst,"123123123") << "d:123123123";

  textCat(dst,dstN,src);
  EXPECT_STREQ(dst,"123123123")  << "e:123123123";
  
  textCat(dst,dstN,src);
  EXPECT_STREQ(dst,"123123123")  << "f:123123123";
}


