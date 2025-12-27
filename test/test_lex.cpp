#include <gtest/gtest.h>

#include "cwCommon.h"
#include "cwTest.h"
#include "cwLex.h"
#include "cwText.h"

using namespace cw;
using namespace cw::lex;


TEST(LexTest, ManyLexTests)
{
  typedef struct 
  {
    unsigned    line;
    unsigned    col;
    unsigned    token_id;
    const char* token_text;
    unsigned    token_char_cnt;
    unsigned    int_value;
    double      double_value;
    bool        is_unsigned_fl;
    bool        is_float_fl;
  } result_t;
    
  rc_t     rc  = kOkRC;
  unsigned tid = kInvalidId;
  lex::handle_t h;

  char buf[] = 
    "123ident0\n 123.456\nident0\n"
    "0xa12+.2\n"
    "                       // comment \n"
    "/* block \n"
    "comment */"
    "\"quoted string\""
    "ident1 "
    "1234.56f"
    "345u"
    "                       // last line comment";

  // initialize a lexer with a buffer of text
  ASSERT_EQ( (rc = create(h,buf,strlen(buf), kReturnSpaceLexFl | kReturnCommentsLexFl)), kOkRC );

  const unsigned kPlusLexTId  = kUserLexTId+1;
  const unsigned kMinusLexTId = kUserLexTId+2;
    

  // register some additional recoginizers
  registerToken(h,kUserLexTId+1,"+");
  registerToken(h,kUserLexTId+2,"-");

  result_t resultA[] = {
    { 1, 1,  kIntLexTId,      "123", 3, 123, 123.0,   false, false },
    { 1, 4,  kIdentLexTId, "ident0", 6,   0,   0.0,   false, false },
    { 1, 10, kSpaceLexTId,       "", 2,   0,   0.0,   false, false },
    { 2,  3, kRealLexTId, "123.456", 7, 123, 123.456, false, false },
    { 2, 10, kSpaceLexTId,       "", 1,   0,   0.0,   false, false },
    { 3,  2, kIdentLexTId, "ident0", 6, 123, 123.456, false, false },
    { 3,  8, kSpaceLexTId,       "", 1,   0,   0.0,   false, false },
    { 4,  2, kHexLexTId,    "0xa12", 5,2578,2578.0,   false, false },
    { 4,  7, kPlusLexTId,       "+", 1,   0,   0.0,   false, false }, 
    { 4,  8, kRealLexTId,      ".2", 2,   0,   0.2,   false, false },
    { 4, 10, kSpaceLexTId,       "",24,   0,   0.0,   false, false },
    { 5, 25, kLineCmtLexTId,     "",12,   0,   0.0,   false, false },
    { 6,  2, kBlockCmtLexTId,    "",20,   0,   0.0,   false, false },
    { 7, 12, kQStrLexTId, "quoted string",13, 0, 0.0,false, false },
    { 7, 27, kIdentLexTId, "ident1", 6,   0,   0.0,   false, false },
    { 7, 33, kSpaceLexTId,       "", 1,   0,   0.0,   false, false },
    { 7, 34, kRealLexTId,"1234.56f", 8,1234,1234.56,  false, true  },
    { 7, 42, kIntLexTId,     "345u", 4, 345, 345.0,   true,  false },
    { 7, 46, kSpaceLexTId,       "",23,   0,   0.0,   false, false },
    { 7, 69, kLineCmtLexTId,     "",20,   0,   0.0,   false, false }
    
  };
  
  const unsigned resultN = sizeof(resultA)/sizeof(resultA[0]);
  
  // ask for token id's 
  for(unsigned i=0; (tid = getNextToken(h)) != kEofLexTId; ++i )
  {
    if( i >= resultN )
    {
      EXPECT_LE( i, resultN );
      break;
    } 
    else
    {
      const result_t* r = resultA + i;
    
      EXPECT_EQ(    currentLineNumber(h),   r->line );
      EXPECT_EQ(    currentColumnNumber(h), r->col );
      EXPECT_EQ(    tokenId(h),             r->token_id );
      EXPECT_TRUE(  textIsEqual( tokenText(h),  r->token_text, strlen(r->token_text) ) );
      EXPECT_EQ(    tokenCharCount(h),      r->token_char_cnt );

      if( tid==kIntLexTId || tid==kRealLexTId || tid==kHexLexTId )
      {
        EXPECT_EQ( tokenInt(h),               r->int_value );
        EXPECT_EQ( tokenDouble(h),            r->double_value );
        EXPECT_EQ( tokenIsUnsigned(h),        r->is_unsigned_fl );
        EXPECT_EQ( tokenIsSinglePrecision(h), r->is_float_fl );        
      }      
    }
  }

errLabel:
  // finalize the lexer 
  EXPECT_EQ(destroy(h),kOkRC );

}
