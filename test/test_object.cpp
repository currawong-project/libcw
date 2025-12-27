#include <gtest/gtest.h>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTest.h"

#include "cwObject.h"

using namespace cw;

class ObjectTest : public testing::Test
{
protected:
  ObjectTest(){}
  
  virtual ~ObjectTest(){}

  virtual void SetUp() override
  {
    
    const char    s [] = "{ a:1, b:2, c:[ 1.23, 4.56 ], d:true, e:false, f:true }";
    ASSERT_EQ( objectFromString(s,_o), kOkRC );    
  }

  virtual void TearDown() override
  {
    if( _o != nullptr )
      _o->free();
  }


  object_t* _o = nullptr;
  
};

TEST_F(ObjectTest, ParseTest )
{
  int v;
  int a = 0;
  int b = 0;
  const object_t* c = nullptr;
  bool d,e,f;
  
  
  ASSERT_NE( _o, nullptr );
  
  ASSERT_EQ(_o->get("b",v),kOkRC);
  EXPECT_EQ(v,2);


  ASSERT_EQ(_o->getv("a",a,"b",b),kOkRC);
  EXPECT_EQ(a,1);
  EXPECT_EQ(b,2);
  

  ASSERT_EQ(_o->readv("a",0,a,
                      "b",0,b,
                      "c",kOptFl | kListTId,c,
                      "d",0,d,
                      "e",0,e,
                      "f",0,f), kOkRC );
  
  EXPECT_EQ(a,1);
  EXPECT_EQ(b,2);
  EXPECT_EQ(d,true);
  EXPECT_EQ(e,false);
  EXPECT_EQ(f,true);

  // test that readv() fails if a requested field is not found
  EXPECT_NE(_o->readv("g",0,a), kOkRC );

  // get the getv_opt() does not fail a requested field is not found
  EXPECT_EQ(_o->getv_opt("g",a), kOkRC );
  
}

TEST_F(ObjectTest, PrintAndDuplTest  )
{
  int a,b;
  const object_t* c = nullptr;
  bool d,e,f;
  object_t* oo = nullptr;
  
  const unsigned bufN = 128;
  char buf[bufN];
  const char* s = " { \"a\" : 1, \"b\" : 2, \"c\" :  [ 1.230000, 4.560000 ] , \"d\" : true, \"e\" : false, \"f\" : true } ";

  ASSERT_EQ(_o->readv("a",0,a,
                      "b",0,b,
                      "c",kOptFl | kListTId,c,
                      "d",0,d,
                      "e",0,e,
                      "f",0,f), kOkRC );
  
  unsigned i = _o->to_string(buf,bufN);
  ASSERT_LT(i,bufN);
  ASSERT_STREQ(buf,s);

  // duplicate the tree
  ASSERT_NE(oo = _o->duplicate(),nullptr);

  // verify that the tree is the identical by comparing it's string output
  i = oo->to_string(buf,bufN);
  ASSERT_LT(i,bufN);
  ASSERT_STREQ(buf,s);
  oo->free();
  
}

