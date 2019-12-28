#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFile.h"

#include "cwLex.h"


namespace cw
{
  namespace lex
  {
    enum
    {
     kRealFloatLexFl   = 0x01,
     kIntUnsignedLexFl = 0x02
    };

    struct lex_str;

    typedef unsigned (*lexMatcherFuncPtr_t)( struct lex_str* p, const char* cp, unsigned cn, const char* keyStr );

    // token match function record  
    typedef struct
    {
      unsigned            typeId;   // token type this matcher recognizes     
      lexMatcherFuncPtr_t funcPtr;  // recognizer function (only used if userPtr==nullptr)
      char*               tokenStr; // fixed string data used by the recognizer (only used if userPtr==nullptr)
      lexUserMatcherPtr_t userPtr;  // user defined recognizer function (only used if funcPtr==nullptr)
      bool                enableFl; // true if this matcher is enabled
    } lexMatcher;



    typedef struct lex_str
    {
      const char* cp;              // character buffer
      unsigned    cn;              // count of characters in buffer
      unsigned    ci;              // current buffer index position
      unsigned    flags;           // lexer control flags
      unsigned    curTokenId;      // type id of the current token
      unsigned    curTokenCharIdx; // index into cp[] of the current token
      unsigned    curTokenCharCnt; // count of characters in the current token 
      unsigned    curLine;         // line number of the current token
      unsigned    curCol;          // column number of the current token
    
      unsigned    nextLine;
      unsigned    nextCol;
      char*       blockBegCmtStr;
      char*       blockEndCmtStr;
      char*       lineCmtStr;
    
      lexMatcher* mfp;            // base of matcher array   
      unsigned    mfi;            // next available matcher array slot
      unsigned    mfn;            // count of elementes in mfp[]
      char*       textBuf;        // text buf used by lexSetFile()
    
      unsigned    attrFlags;      // used to store the int and real suffix type flags
      unsigned    lastRC;
    } lex_t;


    lex_t* _handleToPtr(handle_t h){ return handleToPtr<handle_t,lex_t>(h); }

    bool _lexIsNewline( char c )
    { return c == '\n'; }

    bool _lexIsCommentTypeId( unsigned typeId )
    { return typeId == kBlockCmtLexTId || typeId == kLineCmtLexTId; }

  
    // Locate 'keyStr' in cp[cn] and return the index into cp[cn] of the character
    // following the last char in 'keyStr'.  If keyStr is not found return kInvalidIdx.
    unsigned _lexScanTo( const char* cp, unsigned cn, const char* keyStr )
    {
      unsigned i = 0;
      unsigned n = strlen(keyStr);

      if( n <= cn )
        for(; i<=cn-n; ++i)
          if( strncmp(cp + i, keyStr, n ) == 0 )
            return i+n;

      return kInvalidIdx;
    
    }


    unsigned _lexExactStringMatcher(   lex_t* p, const char* cp, unsigned cn, const char* keyStr )
    {
      unsigned n = strlen(keyStr);
      return strncmp(keyStr,cp,n) == 0  ? n : 0;
    }


    unsigned _lexSpaceMatcher( lex_t* p, const char* cp, unsigned cn, const char* keyStr )
    {
      unsigned i=0;
      for(; i<cn; ++i)
        if( !isspace(cp[i]) )
          break;
      return i;
    }

    unsigned _lexRealMatcher(  lex_t* p, const char* cp, unsigned cn, const char* keyStr )
    {
      unsigned i  = 0;
      unsigned n  = 0;     // decimal point counter
      unsigned d  = 0;     // digit counter
      bool     fl = false; // expo flag  

      for(; i<cn && n<=1; ++i)
      {
        if( i==0 && cp[i]=='-' )  // allow a leading '-'
          continue;

        if( isdigit(cp[i]) )      // allow digits 
        {
          ++d;
          continue;
        }

        if( cp[i] == '.'  && n==0 ) // allow exactly  one decimal point
          ++n;   
        else
          break;
      }

      // if there was at least one digit and the next char is an 'e'
      if( d>0 && i<cn && (cp[i] == 'e' || cp[i] == 'E') )
      {
        unsigned e=0;
        ++i;
        unsigned j = i;

        fl = false;

        for(; i<cn; ++i)
        {
          if( i==j && cp[i]=='-' ) // allow the char following the e to be '-'
            continue;

          if( isdigit(cp[i]) )
          {
            ++e;
            ++d;
            continue;
          }

          // stop at the first non-digit
          break;
        }

        // an exp exists if digits follwed the 'e'
        fl = e > 0;
     
      }

      // if at least one digit was found 
      if( d>0 )
      {
        // Note that this path allows a string w/o a decimal pt to trigger a match.

        if(i<cn)
        {
          // if the real has a suffix
          switch(cp[i])
          {
            case 'F':
            case 'f':
              p->attrFlags = cwSetFlag(p->attrFlags,kRealFloatLexFl);
              ++i;
              break;
          }

        }
    
        // match w/o suffix return
        if( d>0 && (fl || n==1 || cwIsFlag(p->attrFlags,kRealFloatLexFl)) )
          return i;
      }

      return 0; // no-match return
    }

    unsigned _lexIntMatcher(   lex_t* p, const char* cp, unsigned cn, const char* keyStr )
    {
      unsigned i = 0;
      bool signFl = false;
      unsigned digitCnt = 0;
  
      for(; i<cn; ++i)
      {
        if( i==0 && cp[i]=='-' )
        {
          signFl = true;
          continue;
        }

        if( !isdigit(cp[i]) )
          break;

        ++digitCnt;
      }

      // BUG BUG BUG
      // If an integer is specified using 'e' notiation 
      // (see _lexRealMatcher()) and the number of exponent places
      // specified following the 'e' is positive and >= number of
      // digits following the decimal point (in effect zeros are
      // padded on the right side) then the value is an integer.
      //
      // The current implementation recognizes all numeric strings 
      // containing a decimal point as reals. 

      // if no integer was found
      if( digitCnt==0)
        return 0;


      // check for suffix
      if(i<cn )
      {
    
        switch(cp[i])
        {
          case 'u':
          case 'U':
            if( signFl )
              cwLogError(kSyntaxErrorRC,"A signed integer has a 'u' or 'U' suffix on line %i",p->curLine);
            else
            {
              p->attrFlags = cwSetFlag(p->attrFlags,kIntUnsignedLexFl);          
              ++i;
            }
            break;

          default:
            break;
        }
      }

      return  i;
    }

    unsigned _lexHexMatcher(   lex_t* p, const char* cp, unsigned cn, const char* keyStr )
    {
      unsigned i = 0;

      if( cn < 3 )
        return 0;

      if( cp[0]=='0' && cp[1]=='x')    
        for(i=2; i<cn; ++i)
          if( !isxdigit(cp[i]) )
            break;

      return i;
    }


    unsigned _lexIdentMatcher( lex_t* p, const char* cp, unsigned cn, const char* keyStr )
    {
      unsigned i = 0;
      if( isalpha(cp[0]) || (cp[0]== '_'))
      {
        i = 1;
        for(; i<cn; ++i)
          if( !isalnum(cp[i]) && (cp[i] != '_') )
            break;
      }
      return i;
    }


    unsigned _lexQStrMatcher( lex_t* p, const char* cp, unsigned cn, const char* keyStr )
    {
      bool escFl = false;
      unsigned i = 0;
      if( cp[i] != '"' )
        return 0;

      for(i=1; i<cn; ++i)
      {
        if( escFl )
        {
          escFl = false;
          continue;
        }

        if( cp[i] == '\\' )
        {
          escFl = true;
          continue;
        }

        if( cp[i] == '"' )
          return i+1;    
      }

      return cwLogError(kSyntaxErrorRC, "Missing string literal end quote on line: %i.", p->curLine);
    }

    unsigned _lexQCharMatcher( lex_t* p, const char* cp, unsigned cn, const char* keyStr )
    {
      unsigned i = 0;
      if( i >= cn || cp[i]!='\'' )
        return 0;

      i+=2;

      if( i >= cn || cp[i]!='\'')
        return 0;

      return 3;
    }


    unsigned _lexBlockCmtMatcher( lex_t* p, const char* cp, unsigned cn, const char* keyStr )
    {  
      unsigned n = strlen(p->blockBegCmtStr);

      if( strncmp( p->blockBegCmtStr, cp, n ) == 0 )
      {
        unsigned i;
        if((i = _lexScanTo(cp + n, cn-n,p->blockEndCmtStr)) == kInvalidIdx )
        {
          cwLogError(kSyntaxErrorRC, "Missing end of block comment on line:%i.", p->curLine);
          return 0;
        }

        return n + i;
      }
      return 0;
    }

    unsigned _lexLineCmtMatcher( lex_t* p, const char* cp, unsigned cn, const char* keyStr )
    {
      unsigned n = strlen(p->lineCmtStr);
      if( strncmp( p->lineCmtStr, cp, n ) == 0)
      {
        unsigned i;
        const char newlineStr[] = "\n";
        if((i = _lexScanTo(cp + n, cn-n, newlineStr)) == kInvalidIdx )
        {
          // no EOL was found so the comment must be on the last line of the source
          return cn;
        }  

        return n + i;
      }
      return 0;
    }

    rc_t  _lexInstallMatcher( lex_t* p, unsigned typeId, lexMatcherFuncPtr_t funcPtr, const char* keyStr, lexUserMatcherPtr_t userPtr )
    {
      assert( funcPtr==nullptr || userPtr==nullptr );
      assert( !(funcPtr==nullptr && userPtr==nullptr));

      // if there is no space in the user token array - then expand it
      if( p->mfi == p->mfn )
      {
        int incr_cnt = 10;
        lexMatcher* np = mem::allocZ<lexMatcher>( p->mfn + incr_cnt );
        memcpy(np,p->mfp,p->mfi*sizeof(lexMatcher));
        mem::release(p->mfp);
        p->mfp = np;
        p->mfn += incr_cnt;
      }

      p->mfp[p->mfi].tokenStr = nullptr;
      p->mfp[p->mfi].typeId   = typeId;
      p->mfp[p->mfi].funcPtr  = funcPtr;
      p->mfp[p->mfi].userPtr  = userPtr;
      p->mfp[p->mfi].enableFl = true;

      if( keyStr != nullptr )
      {
        // allocate space for the token string and store it
        p->mfp[p->mfi].tokenStr = mem::duplStr(keyStr);
      }


      p->mfi++;
      return kOkRC;
    }
    rc_t _lexReset( lex_t* p )
    {

      p->ci              = 0;

      p->curTokenId      = kErrorLexTId;
      p->curTokenCharIdx = kInvalidIdx;
      p->curTokenCharCnt = 0;

      p->curLine         = 0;
      p->curCol          = 0;
      p->nextLine        = 0;
      p->nextCol         = 0;

      p->lastRC = kOkRC;

      return kOkRC;
    }

    rc_t _lexSetTextBuffer( lex_t* p, const char* cp, unsigned cn )
    {
      p->cp = cp;
      p->cn = cn;

      return _lexReset(p);
    }

    lexMatcher* _lexFindUserToken( lex_t* p, unsigned id, const char* tokenStr )
    {
      unsigned i = 0;
      for(; i<p->mfi; ++i)
      {
        if( id != kInvalidId && p->mfp[i].typeId == id  )
          return p->mfp + i;
      
        if( p->mfp[i].tokenStr != nullptr && tokenStr != nullptr && strcmp(p->mfp[i].tokenStr,tokenStr)==0  )
          return p->mfp + i;

      }

      return nullptr;
    }

  }  
} // namespace cw

cw::rc_t cw::lex::create( handle_t& hRef, const char* cp, unsigned cn, unsigned flags )
{
  rc_t   rc               = kOkRC;
  char  dfltLineCmt[]     = "//";
  char  dfltBlockBegCmt[] = "/*";
  char  dfltBlockEndCmt[] = "*/";
  lex_t* p                = nullptr;
  
  if((rc = lex::destroy(hRef)) != kOkRC )
    return rc;
  
  p          = mem::allocZ<lex_t>();

  p->flags           = flags;
  
  _lexSetTextBuffer( p, cp, cn );

  int init_mfn       = 10;
  p->mfp             = mem::allocZ<lexMatcher>( init_mfn );
  p->mfn             = init_mfn;
  p->mfi             = 0;

  p->lineCmtStr      = mem::duplStr( dfltLineCmt );
  p->blockBegCmtStr  = mem::duplStr( dfltBlockBegCmt );
  p->blockEndCmtStr  = mem::duplStr( dfltBlockEndCmt );

  _lexInstallMatcher( p, kSpaceLexTId,    _lexSpaceMatcher,    nullptr, nullptr );
  _lexInstallMatcher( p, kRealLexTId,     _lexRealMatcher,     nullptr, nullptr  );
  _lexInstallMatcher( p, kIntLexTId,      _lexIntMatcher,      nullptr, nullptr  );
  _lexInstallMatcher( p, kHexLexTId,      _lexHexMatcher,      nullptr, nullptr  );
  _lexInstallMatcher( p, kIdentLexTId,    _lexIdentMatcher,    nullptr, nullptr  );
  _lexInstallMatcher( p, kQStrLexTId,     _lexQStrMatcher,     nullptr, nullptr  );
  _lexInstallMatcher( p, kBlockCmtLexTId, _lexBlockCmtMatcher, nullptr, nullptr  );
  _lexInstallMatcher( p, kLineCmtLexTId,  _lexLineCmtMatcher,  nullptr, nullptr  );

  if( cwIsFlag(flags,kReturnQCharLexFl) )
    _lexInstallMatcher( p, kQCharLexTId,    _lexQCharMatcher,    nullptr, nullptr  );

  hRef.set(p);

  _lexReset(p);

  return rc;
}

cw::rc_t cw::lex::destroy( handle_t& hRef )
{
  if( hRef.isValid() == false )
    return kOkRC;

  lex_t* p = _handleToPtr(hRef);

  if( p != nullptr )
  {

    if( p->mfp != nullptr )
    {
      unsigned i = 0;

      // free the user token strings
      for(; i<p->mfi; ++i)
        if( p->mfp[i].tokenStr != nullptr )
          mem::release(p->mfp[i].tokenStr);

      // free the matcher array
      mem::release(p->mfp);
      p->mfi = 0;
      p->mfn = 0;
    }

    mem::release(p->lineCmtStr);
    mem::release(p->blockBegCmtStr);
    mem::release(p->blockEndCmtStr);
    mem::release(p->textBuf);

    // free the lexer object
    mem::release(p);
    hRef.set(nullptr);
  }

  return kOkRC;
}

cw::rc_t cw::lex::reset( handle_t h )
{
  lex_t* p = _handleToPtr(h);
  return _lexReset(p);
}


bool               cw::lex::isValid( handle_t h )
{ return h.isValid(); }

cw::rc_t            cw::lex::setTextBuffer( handle_t h, const char* cp, unsigned cn )
{
  lex_t* p = _handleToPtr(h);
  return _lexSetTextBuffer(p,cp,cn);
}

cw::rc_t cw::lex::setFile( handle_t h, const char* fn )
{
  rc_t           rc = kOkRC;
  file::handle_t fh;
  lex_t*         p  = _handleToPtr(h);
  long           n  = 0;

  assert( fn != nullptr && p != nullptr );

  // open the file
  if((rc = file::open(fh,fn,file::kReadFl)) != kOkRC )
    return rc;

  // seek to the end of the file
  if((rc = file::seek(fh,file::kEndFl,0)) != kOkRC )
    return rc;
  
  // get the length of the file
  if((rc = file::tell(fh,&n)) != kOkRC )
    return rc;

  // rewind to the beginning of the file
  if((rc = file::seek(fh,file::kBeginFl,0)) != kOkRC )
    return rc;

  // allocate the text buffer
  if((p->textBuf = mem::resizeZ<char>(p->textBuf, n+1)) == nullptr )
  {
    rc = cwLogError(kMemAllocFailRC,"Unable to allocate the text file buffer for:'%s'.",fn);
    goto errLabel;
  }

  // read the file into the buffer
  if((rc = file::read(fh,p->textBuf,n)) != kOkRC )
    return rc;

  if((rc = _lexSetTextBuffer( p, p->textBuf, n )) != kOkRC )
    goto errLabel;
  
 errLabel:
  // close the file
  rc_t rc0 = file::close(fh);

  if(rc != kOkRC )
    return rc;
  
  return rc0;
}

cw::rc_t           cw::lex::registerToken( handle_t h, unsigned id, const char* tokenStr )
{
  lex_t* p = _handleToPtr(h);

  // prevent duplicate tokens
  if( _lexFindUserToken( p, id, tokenStr ) != nullptr )
    return cwLogError( kInvalidArgRC, "id:%i token:%s duplicates the token string or id", id, tokenStr );


  return _lexInstallMatcher( p, id, _lexExactStringMatcher, tokenStr, nullptr );
  

}

cw::rc_t            cw::lex::registerMatcher( handle_t h, unsigned id, lexUserMatcherPtr_t userPtr )
{
  lex_t* p = _handleToPtr(h);

  // prevent duplicate tokens
  if( _lexFindUserToken( p, id, nullptr ) != nullptr )
    return cwLogError(kInvalidArgRC, "A token matching function has already been installed for token id: %i", id );

  return _lexInstallMatcher( p, id, nullptr, nullptr, userPtr );
}

cw::rc_t            cw::lex::enableToken( handle_t h, unsigned id, bool enableFl )
{
  lex_t* p = _handleToPtr(h);

  unsigned mi = 0;
  for(; mi<p->mfi; ++mi)
    if( p->mfp[mi].typeId == id )
    {
      p->mfp[mi].enableFl = enableFl;
      return kOkRC;
    }

  return cwLogError( kInvalidArgRC, "%i is not a valid token type id.",id);
}

unsigned           cw::lex::filterFlags( handle_t h )
{
  lex_t* p = _handleToPtr(h);
  return p->flags;
}

void               cw::lex::setFilterFlags( handle_t h, unsigned flags )
{
  lex_t* p = _handleToPtr(h);
  p->flags = flags;
}


unsigned           cw::lex::getNextToken( handle_t h )
{
  lex_t* p = _handleToPtr(h);

  if( p->lastRC != kOkRC )
    return kErrorLexTId;

  while( p->ci < p->cn )
  {
    unsigned i;
    unsigned mi         = 0;
    unsigned maxCharCnt = 0;
    unsigned maxIdx     = kInvalidIdx;

    p->curTokenId      = kErrorLexTId;
    p->curTokenCharIdx = kInvalidIdx;
    p->curTokenCharCnt = 0;
    p->attrFlags       = 0;

    // try each matcher
    for(; mi<p->mfi; ++mi)
      if( p->mfp[mi].enableFl )
      {
        unsigned charCnt = 0;
        if( p->mfp[mi].funcPtr != nullptr )
          charCnt = p->mfp[mi].funcPtr(p, p->cp + p->ci, p->cn - p->ci, p->mfp[mi].tokenStr );
        else
          charCnt = p->mfp[mi].userPtr( p->cp + p->ci, p->cn - p->ci);

        // notice if the matcher set the error code
        if( p->lastRC != kOkRC )
          return kErrorLexTId;

        // if this matched token is longer then the prev. matched token or
        // if the prev matched token was an identifier and this matched token is an equal length user defined token
        if( (charCnt > maxCharCnt) 
          || (charCnt>0 && charCnt==maxCharCnt && p->mfp[maxIdx].typeId==kIdentLexTId && p->mfp[mi].typeId >=kUserLexTId ) 
          || (charCnt>0 && charCnt<maxCharCnt  && p->mfp[maxIdx].typeId==kIdentLexTId && p->mfp[mi].typeId >=kUserLexTId && cwIsFlag(p->flags,kUserDefPriorityLexFl))
            )
        {
          maxCharCnt = charCnt;
          maxIdx     = mi;
        }

      }

    // no token was matched
    if( maxIdx == kInvalidIdx )
    {
      if( cwIsFlag(p->flags,kReturnUnknownLexFl) )
      {
        maxCharCnt = 1;
      }
      else
      {
        cwLogError( kSyntaxErrorRC, "Unable to recognize token:'%c' on line %i.",*(p->cp+p->ci), p->curLine);
        return kErrorLexTId;     
      }
    }

    // update the current line and column position    
    p->curLine = p->nextLine;
    p->curCol  = p->nextCol;
    

    // find the next column and line position
    for(i=0; i<maxCharCnt; ++i)
    {
      if( _lexIsNewline(p->cp[ p->ci + i ]) )
      {
        p->nextLine++;
        p->nextCol = 1;
      }
      else
        p->nextCol++;
    }

    bool returnFl = true;

    if( maxIdx != kInvalidIdx )
    {
      // check the space token filter
      if( (p->mfp[ maxIdx ].typeId == kSpaceLexTId) && (cwIsFlag(p->flags,kReturnSpaceLexFl)==0) )
        returnFl = false;

      // check the comment token filter
      if( _lexIsCommentTypeId(p->mfp[ maxIdx ].typeId) && (cwIsFlag(p->flags,kReturnCommentsLexFl)==0) )
        returnFl = false;
    }

    // update the lexer state
    p->curTokenId      = maxIdx==kInvalidIdx ? kUnknownLexTId : p->mfp[ maxIdx ].typeId;    
    p->curTokenCharIdx = p->ci;
    p->curTokenCharCnt = maxCharCnt;
      
    // advance the text buffer
    p->ci += maxCharCnt;

    if( returnFl )
      return p->curTokenId;
  }

  p->lastRC = kEofRC;

  return kEofLexTId;

}

unsigned cw::lex::tokenId( handle_t h )
{
  lex_t* p = _handleToPtr(h);

  return p->curTokenId;
}

const char* cw::lex::tokenText( handle_t h )
{
  lex_t* p = _handleToPtr(h);

  if( p->curTokenCharIdx == kInvalidIdx )
    return nullptr;

  unsigned n = p->curTokenId == kQStrLexTId ? 1 : 0;

  return p->cp + p->curTokenCharIdx + n;
}


unsigned           cw::lex::tokenCharCount(  handle_t h )
{
  lex_t* p = _handleToPtr(h);

  if( p->curTokenCharIdx == kInvalidIdx )
    return 0;

  unsigned n = p->curTokenId == kQStrLexTId ? 2 : 0;

  return p->curTokenCharCnt - n;
}

int                cw::lex::tokenInt(        handle_t h )
{  return strtol( lex::tokenText(h),nullptr,0 ); }

unsigned           cw::lex::tokenUInt(        handle_t h )
{  return strtol( lex::tokenText(h),nullptr,0 ); }

float              cw::lex::tokenFloat(        handle_t h )
{  return strtof( lex::tokenText(h),nullptr ); }

double             cw::lex::tokenDouble(        handle_t h )
{  return strtod( lex::tokenText(h),nullptr ); }


bool               cw::lex::tokenIsUnsigned( handle_t h )
{
  lex_t* p = _handleToPtr(h);
  return p->curTokenId == kIntLexTId && cwIsFlag(p->attrFlags,kIntUnsignedLexFl);
}

bool               cw::lex::tokenIsSinglePrecision( handle_t h )
{
  lex_t* p = _handleToPtr(h);
  return p->curTokenId == kRealLexTId && cwIsFlag(p->attrFlags,kRealFloatLexFl);
}

unsigned cw::lex::currentLineNumber( handle_t h )
{ 
  lex_t* p = _handleToPtr(h);
  return p->curLine + 1;
}

unsigned cw::lex::currentColumnNumber( handle_t h ) 
{
  lex_t* p = _handleToPtr(h);
  return p->curCol + 1;
}

unsigned           cw::lex::errorRC( handle_t h )
{
  lex_t* p = _handleToPtr(h);
  return p->lastRC;
}

const char* cw::lex::idToLabel( handle_t h, unsigned typeId )
{
  lex_t* p = _handleToPtr(h);

  switch( typeId )
  {
    case kErrorLexTId:    return "<error>";
    case kEofLexTId:      return "<EOF>";
    case kSpaceLexTId:    return "<space>";
    case kRealLexTId:     return "<real>";
    case kIntLexTId:      return "<int>";
    case kHexLexTId:      return "<hex>";
    case kIdentLexTId:    return "<ident>";
    case kQStrLexTId:     return "<qstr>";
    case kBlockCmtLexTId: return "<bcmt>";
    case kLineCmtLexTId:  return "<lcmt>";
    default:
      {
        lexMatcher*  mp;
        if((mp = _lexFindUserToken(p,typeId,nullptr)) == nullptr )
          return "<unknown>";
        return mp->tokenStr;
      }
  }
  return "<invalid>";
}


namespace cw
{
  namespace lex
  {
    //{ { label:cwLexEx }
    //(
    // lexTest() gives a simple 'lex' example.
    //)

    //(
    void test()
    {
      rc_t     rc  = kOkRC;
      unsigned tid = kInvalidId;
      handle_t h;
    
      char buf[] = 
        "123ident0\n 123.456\nident0\n"
        "0xa12+.2\n"
        "                       // comment \n"
        "/* block \n"
        "comment */"
        "\"quoted string\""
        "ident1"
        "                       // last line comment";

      // initialize a lexer with a buffer of text
      if((rc = lex::create(h,buf,strlen(buf), kReturnSpaceLexFl | kReturnCommentsLexFl)) != kOkRC )
      {
        cwLogError(rc,"Lexer initialization failed.");
        return;
      }

      // register some additional recoginizers
      lex::registerToken(h,kUserLexTId+1,"+");
      lex::registerToken(h,kUserLexTId+2,"-");

      // ask for token id's 
      while( (tid = lex::getNextToken(h)) != kEofLexTId )
      {
        // print information about each token
        cwLogInfo("%i %i %s '%.*s' (%i) ", 
          lex::currentLineNumber(h), 
          lex::currentColumnNumber(h), 
          lex::idToLabel(h,tid), 
          lex::tokenCharCount(h), 
          lex::tokenText(h) , 
          lex::tokenCharCount(h));

        // if the token is a number ...
        if( tid==kIntLexTId || tid==kRealLexTId || tid==kHexLexTId )
        {
          // ... then request the numbers value
          int    iv = lex::tokenInt(h);
          double dv = lex::tokenDouble(h);

          cwLogInfo("%i %f",iv,dv);
        }

        cwLogInfo("\n");

        // handle errors
        if( tid == kErrorLexTId )
        {
          cwLogInfo("Error:%i\n", lex::errorRC(h));
          break;
        }

      }

      // finalize the lexer 
      lex::destroy(h);

    }
  }
}

//)
//}
