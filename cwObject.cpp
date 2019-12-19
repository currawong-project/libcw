#include <type_traits>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwLex.h"
#include "cwText.h"
#include "cwNumericConvert.h"
#include "cwObject.h"
#include "cwObjectTemplate.h"


namespace cw
{
  enum
  {
   kLCurlyLexTId = cw::kUserLexTId+1, 
   kRCurlyLexTId,
   kLHardLexTId,
   kRHardLexTId,
   kColonLexTId,
   kCommaLexTId,
   kTrueLexTId,
   kFalseLexTId,
   kNullLexTId
  };

  idLabelPair_t _objTokenArray[] =
  {
   { kLCurlyLexTId, "{" },
   { kRCurlyLexTId, "}" },
   { kLHardLexTId,  "[" },
   { kRHardLexTId,  "]" },
   { kColonLexTId,  ":" },
   { kCommaLexTId,  "," },
   { kTrueLexTId,   "true"},
   { kFalseLexTId,  "false"},
   { kNullLexTId,   "null" },
   { cw::kErrorLexTId,""}
  
  };


  
  void _objTypeFree( object_t* o )
  { memRelease(o); }
  
  void _objTypeFreeString( object_t* o )
  {
    memRelease( o->u.str );
    _objTypeFree(o);
  }
  
  const char* _objTypeIdToLabel( objTypeId_t tid );


  void _objTypeNullToString(   object_t* o, char* buf, unsigned bufN ) { snprintf(buf,bufN,"%s","NULL");  }
  void _objTypeCharToString(   object_t* o, char* buf, unsigned bufN ) { number_to_string<char>(o->u.c,"%c",buf,bufN); }
  void _objTypeInt8ToString(   object_t* o, char* buf, unsigned bufN ) { number_to_string<int8_t>(o->u.i8,"%i",buf,bufN); }
  void _objTypeUInt8ToString(  object_t* o, char* buf, unsigned bufN ) { number_to_string<uint8_t>(o->u.u8,"%i",buf,bufN); }
  void _objTypeInt16ToString(  object_t* o, char* buf, unsigned bufN ) { number_to_string<int16_t>(o->u.i16,"%i",buf,bufN); }
  void _objTypeUInt16ToString( object_t* o, char* buf, unsigned bufN ) { number_to_string<uint16_t>(o->u.u16,"%i",buf,bufN); } 
  void _objTypeInt32ToString(  object_t* o, char* buf, unsigned bufN ) { number_to_string<int32_t>(o->u.i32,"%i",buf,bufN); }
  void _objTypeUInt32ToString( object_t* o, char* buf, unsigned bufN ) { number_to_string<uint32_t>(o->u.u32,"%i",buf,bufN); }
  void _objTypeInt64ToString(  object_t* o, char* buf, unsigned bufN ) { number_to_string<int64_t>(o->u.i64,"%i",buf,bufN); }
  void _objTypeUInt64ToString( object_t* o, char* buf, unsigned bufN ) { number_to_string<uint64_t>(o->u.u64,"%i",buf,bufN); }
  void _objTypeBoolToString(   object_t* o, char* buf, unsigned bufN ) { number_to_string<bool>(o->u.b,"%i",buf,bufN); }
  void _objTypeFloatToString(  object_t* o, char* buf, unsigned bufN ) { number_to_string<float>(o->u.f,"%f",buf,bufN); }
  void _objTypeDoubleToString( object_t* o, char* buf, unsigned bufN ) { number_to_string<double>(o->u.d,"%f",buf,bufN); }
  void _objTypeStringToString( object_t* o, char* buf, unsigned bufN ) { snprintf(buf,bufN,"%s",o->u.str); }

  
  rc_t _objTypeValueFromChar(     const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.c,tid,  dst,o->type->label); }
  rc_t _objTypeValueFromInt8(     const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.i8,tid, dst,o->type->label); }
  rc_t _objTypeValueFromUInt8(    const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.u8,tid, dst,o->type->label); }
  rc_t _objTypeValueFromInt16(    const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.i16,tid,dst,o->type->label); }
  rc_t _objTypeValueFromUInt16(   const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.u16,tid,dst,o->type->label); }
  rc_t _objTypeValueFromInt32(    const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.i32,tid,dst,o->type->label); }
  rc_t _objTypeValueFromUInt32(   const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.u32,tid,dst,o->type->label); }
  rc_t _objTypeValueFromInt64(    const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.i64,tid,dst,o->type->label); }
  rc_t _objTypeValueFromUInt64(   const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.u64,tid,dst,o->type->label); }
  rc_t _objTypeValueFromFloat(    const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.f,tid,  dst,o->type->label); }
  rc_t _objTypeValueFromDouble(   const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.d,tid,  dst,o->type->label); }
  rc_t _objTypeValueFromBool(     const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.b,tid,dst,o->type->label); }

  rc_t _objTypeValueFromNonValue( const object_t* o, unsigned tid, void* dst )
  { return cwLogError(kInvalidArgRC, "There is no conversion from '%s' to '%s'.", _objTypeIdToLabel(tid), o->type->label); }
  
  rc_t _objTypeValueFromString(   const object_t* o, unsigned tid, void* dst )
  { return _objTypeValueFromNonValue(o,tid,dst); }
  
  rc_t _objTypeValueFromVect(     const object_t* o, unsigned tid, void* dst )
  { return _objTypeValueFromNonValue(o,tid,dst); }

  void _objTypePrintIndent( const char* text, unsigned indent, const char* indentStr=" " )
  {
    for(unsigned i=0; i<indent; ++i)
      printf(indentStr);
    printf("%s",text);
 
  }

  void _objTypePrintChild( const object_t* o, print_ctx_t& c, const char* eolStr=",\n", const char* indentStr=" " )
  {
    _objTypePrintIndent(" ",c.indent,indentStr);
    o->type->print(o,c);
    printf(eolStr);
  }
  
  void _objTypePrintNull(   const object_t* o, print_ctx_t& c ) { printf("NULL "); }
  void _objTypePrintError(  const object_t* o, print_ctx_t& c ) { printf("Error "); }
  void _objTypePrintChar(   const object_t* o, print_ctx_t& c ) { printf("%c",o->u.c); }
  void _objTypePrintInt8(   const object_t* o, print_ctx_t& c ) { printf("%i",o->u.i8); }
  void _objTypePrintUInt8(  const object_t* o, print_ctx_t& c ) { printf("%i",o->u.u8); }
  void _objTypePrintInt16(  const object_t* o, print_ctx_t& c ) { printf("%i",o->u.i16); }
  void _objTypePrintUInt16( const object_t* o, print_ctx_t& c ) { printf("%i",o->u.u16); }
  void _objTypePrintInt32(  const object_t* o, print_ctx_t& c ) { printf("%i",o->u.i32); }
  void _objTypePrintUInt32( const object_t* o, print_ctx_t& c ) { printf("%i",o->u.u32); }
  void _objTypePrintInt64(  const object_t* o, print_ctx_t& c ) { printf("%li",o->u.i64); }
  void _objTypePrintUInt64( const object_t* o, print_ctx_t& c ) { printf("%li",o->u.u64); }
  void _objTypePrintBool(   const object_t* o, print_ctx_t& c ) { printf("%s",o->u.b ? "true" : "false"); }
  void _objTypePrintFloat(  const object_t* o, print_ctx_t& c ) { printf("%f",o->u.f); }
  void _objTypePrintDouble( const object_t* o, print_ctx_t& c ) { printf("%f",o->u.d); }
  void _objTypePrintString( const object_t* o, print_ctx_t& c ) { printf("%s",o->u.str); }
  void _objTypePrintVect(   const object_t* o, print_ctx_t& c ) { printf("<vect>"); }
  void _objTypePrintPair(   const object_t* o, print_ctx_t& c )
  {
    o->u.children->type->print(o->u.children,c);
    printf(": ");
    o->u.children->sibling->type->print(o->u.children->sibling,c);    
  }
  
  void _objTypePrintList( const object_t* o, print_ctx_t& c )
  {
    const char* indentStr  = c.listOnOneLineFl ? ""    : " ";

    char bracketStr[] = { '[','\0','\0' };
    char eoValStr[]   = { ',','\0','\0' };

    if(!c.listOnOneLineFl)
    {
      bracketStr[1] = '\n';
      eoValStr[1] = '\n';
    }
      
    
    _objTypePrintIndent(bracketStr,0);
    c.indent += 2;
    
    for(const object_t* ch=o->u.children; ch!=nullptr; ch=ch->sibling)
    {
      if( ch->sibling == nullptr )
        eoValStr[0] = ' ';
      
      _objTypePrintChild(ch,c,eoValStr,indentStr);
    }
    
    c.indent -= 2;
    _objTypePrintIndent("]",c.listOnOneLineFl ? 0 : c.indent);
  }
  
  void _objTypePrintDict( const object_t* o, print_ctx_t& c )
  {
    _objTypePrintIndent("{\n",0);
    c.indent += 2;
    
    for(const object_t* ch=o->u.children; ch!=nullptr; ch=ch->sibling)
      _objTypePrintChild(ch,c);

    c.indent -= 2;
    _objTypePrintIndent("}",c.indent);
  }
  
  void _objTypePrintRoot( const object_t* o, print_ctx_t& c )
  {
    _objTypePrintDict(o,c);
  }
    
  objType_t _objTypeArray[] =
  {
   { kNullTId,    "null",      0,                                _objTypeFree, _objTypeValueFromNonValue, _objTypePrintNull },
   { kErrorTId,   "error",     0,                                _objTypeFree, _objTypeValueFromNonValue, _objTypePrintError },
   { kCharTId,    "char",      0,                                _objTypeFree, _objTypeValueFromChar,     _objTypePrintChar },
   { kInt8TId,    "int8",      0,                                _objTypeFree, _objTypeValueFromInt8,     _objTypePrintInt8 },
   { kUInt8TId,   "uint8",     0,                                _objTypeFree, _objTypeValueFromUInt8,    _objTypePrintUInt8 },
   { kInt16TId,   "int16",     0,                                _objTypeFree, _objTypeValueFromInt16,    _objTypePrintInt16 },
   { kUInt16TId,  "uint16",    0,                                _objTypeFree, _objTypeValueFromUInt16,   _objTypePrintUInt16 },
   { kInt32TId,   "int32",     0,                                _objTypeFree, _objTypeValueFromInt32,    _objTypePrintInt32 },
   { kUInt32TId,  "uint32",    0,                                _objTypeFree, _objTypeValueFromUInt32,   _objTypePrintUInt32 },
   { kInt64TId,   "int64",     0,                                _objTypeFree, _objTypeValueFromInt64,    _objTypePrintInt64 },
   { kUInt64TId,  "uint64",    0,                                _objTypeFree, _objTypeValueFromUInt64,   _objTypePrintUInt64 },
   { kBoolTId,    "bool",      0,                                _objTypeFree, _objTypeValueFromBool,     _objTypePrintBool },
   { kFloatTId,   "float",     0,                                _objTypeFree, _objTypeValueFromFloat,    _objTypePrintFloat },
   { kDoubleTId,  "double",    0,                                _objTypeFree, _objTypeValueFromDouble,   _objTypePrintDouble },
   { kStringTId,  "string",    0,                                _objTypeFreeString, _objTypeValueFromString, _objTypePrintString },
   { kVectTId,    "vect",      0,                                _objTypeFree, _objTypeValueFromVect,     _objTypePrintVect },
   { kPairTId,    "pair",      kContainerFl | kValueContainerFl, _objTypeFree, _objTypeValueFromNonValue, _objTypePrintPair },
   { kListTId,    "list",      kContainerFl | kValueContainerFl, _objTypeFree, _objTypeValueFromNonValue, _objTypePrintList },
   { kDictTId,    "dict",      kContainerFl,                     _objTypeFree, _objTypeValueFromNonValue, _objTypePrintDict },
   { kRootTId,    "root",      kContainerFl | kValueContainerFl, _objTypeFree, _objTypeValueFromNonValue, _objTypePrintRoot },
   { kInvalidTId, "<invalid>", 0, nullptr }   
  };



  objType_t* _objIdToType( objTypeId_t tid )
  {
    unsigned i;
    for(i=0; _objTypeArray[i].id != kInvalidTId; ++i)
      if( _objTypeArray[i].id == tid )
        return _objTypeArray + i;

    cwLogError(kInvalidIdRC,"The object type id %i is not valid.",tid);
    return nullptr;
  }

  const char* _objTypeIdToLabel( objTypeId_t tid )
  {
    const objType_t* type;
    if((type = _objIdToType(tid)) == nullptr )
      return "<invalid>";
    
    return type->label;
  }

  
  object_t* _objAllocate( objTypeId_t tid, object_t* parent )
  {
    objType_t* type = nullptr;
    if( tid != kInvalidTId )
    {
      if((type = _objIdToType(tid)) == nullptr )
      {
        cwLogError(kObjAllocFailRC,"Object allocation failed.");
        return nullptr;
      }
    }
    
    object_t* o =  memAllocZ<object_t>();
    o->type   = type;
    o->parent = parent;
    return o;
  }

  rc_t _objSyntaxError( lexH_t lexH, const char* fmt, ... )
  {
    va_list vl;
    va_start(vl,fmt);

    cwLogVError( kSyntaxErrorRC, fmt, vl );
    cwLogError(  kSyntaxErrorRC, "Error on line: %i.", lexCurrentLineNumber(lexH));
    va_end(vl);
    return kSyntaxErrorRC;
  }
 
  
  rc_t _objVerifyParentIsValueContainer( lexH_t lexH, const object_t* parent, const char* msg )
  {
    if( parent == nullptr )
      return _objSyntaxError(lexH,"The parent node must always be valid.");
    
    // it is legal for a parent of a value to be null when the value is the root element.
    if( !(parent->is_value_container()))
      return _objSyntaxError(lexH,"Value nodes of type '%s' must be contained by 'root', 'pair' or 'array' node.",msg);
    
    return kOkRC;
  }
  
  object_t* _objAppendLeftMostNode( object_t* parent, object_t* newNode )
  {
    if( newNode == nullptr )
      return nullptr;
    
    object_t* child = parent->u.children;

    if( parent->u.children == nullptr )
      parent->u.children = newNode;
    else
    {
      while( child->sibling != nullptr )
        child = child->sibling;

      child->sibling = newNode;
    }

    newNode->parent = parent;
    return newNode;
  }

  object_t* _objCreateConainerNode( lexH_t lexH, object_t* parent, objTypeId_t tid )
  {
    if( _objVerifyParentIsValueContainer(lexH,parent,_objTypeIdToLabel(tid)) == kOkRC )
      return _objAppendLeftMostNode( parent, _objAllocate( tid, parent ));
    
    return nullptr;            
  }

}


void cw::object_t::free()
{
  if( is_container() )
  {
    object_t* o1 = nullptr;
    for(object_t* o = u.children; o != nullptr; o=o1 )
    {
      o1 = o->sibling;
      o->free();
    }
  }
  
  type->free(this);
}


unsigned cw::object_t::child_count() const
{
  unsigned n = 0;
  if( is_container() && u.children != nullptr)
  {
    object_t* o = u.children;
    
    for(n=1; o->sibling != nullptr; o=o->sibling)
      ++n;
  }
  return n;
}

cw::rc_t cw::object_t::value( void* dst, unsigned dstTypeId ) { return type->value(this,dstTypeId,dst); }
cw::rc_t cw::object_t::value( char& v )     const { return type->value(this,kCharTId,&v); }
cw::rc_t cw::object_t::value( int8_t&  v )  const { return type->value(this,kInt8TId,&v); }
cw::rc_t cw::object_t::value( uint8_t& v )  const { return type->value(this,kUInt8TId,&v); }
cw::rc_t cw::object_t::value( int16_t&  v ) const { return type->value(this,kInt16TId,&v); }
cw::rc_t cw::object_t::value( uint16_t& v ) const { return type->value(this,kUInt16TId,&v); }
cw::rc_t cw::object_t::value( int32_t&  v ) const { return type->value(this,kInt32TId,&v); }
cw::rc_t cw::object_t::value( uint32_t& v ) const { return type->value(this,kUInt32TId,&v); }
cw::rc_t cw::object_t::value( int64_t&  v ) const { return type->value(this,kInt64TId,&v); }
cw::rc_t cw::object_t::value( uint64_t& v ) const { return type->value(this,kUInt64TId,&v); }
cw::rc_t cw::object_t::value( float&  v )   const { return type->value(this,kFloatTId,&v); }
cw::rc_t cw::object_t::value( double& v )   const { return type->value(this,kDoubleTId,&v); }
cw::rc_t cw::object_t::value( char*& v )    const { return type->value(this,kStringTId,&v); }

const char* cw::object_t::pair_label() const
{
  cwAssert( is_pair() );
  if( is_pair() )
    return u.children->u.str;
  return nullptr;
}

const struct cw::object_str* cw::object_t::pair_value() const
{
  cwAssert( is_pair() );
  if( is_pair() )
    return u.children->sibling;
  return nullptr;
}

const struct cw::object_str* cw::object_t::find( const char* label ) const
{
  if( is_container() )
  {
    for(object_t* o=u.children; o!=nullptr; o=o->sibling)
    {
      if( o->is_pair() && textCompare(o->pair_label(),label) == 0 )
        return o->pair_value();

      const object_t* ch;
      if((ch = o->find(label)) != nullptr )
        return ch;
    }     
  }
  
  return nullptr;
}


void cw::object_t::print(const print_ctx_t* c) const
{
  print_ctx_t ctx;
  if( c != nullptr )
    ctx = *c;
  type->print(this,ctx); 
}



cw::rc_t cw::objectFromString( const char* s, object_t*& objRef )
{
  lexH_t    lexH;
  rc_t      rc;
  unsigned  lexFlags = 0;
  unsigned  lexId    = kErrorLexTId;
  object_t* cnp      = _objAllocate(kRootTId,nullptr);
  object_t* root     = cnp;

  objRef = nullptr;

  if((rc = lexCreate(lexH,s,textLength(s), lexFlags )) != kOkRC )
    return rc;

  // setup the lexer with additional tokens
  for(unsigned i=0; _objTokenArray[i].id != cw::kErrorLexTId; ++i)
    if((rc = lexRegisterToken( lexH, _objTokenArray[i].id, _objTokenArray[i].label )) != kOkRC )
    {
      rc = cwLogError(rc,"Object lexer token registration failed on token id:%i : '%s'",_objTokenArray[i].id, _objTokenArray[i].label);
      goto errLabel;
    }

  // main parser loop
  while((lexId = lexGetNextToken(lexH)) != cw::kErrorLexTId && (lexId != kEofLexTId) && (rc == kOkRC))
  {

    //printf("Lex:%s\n",lexIdToLabel(lexH,lexId));
    
    switch( lexId )
    {
      case kLCurlyLexTId:
        cnp = _objCreateConainerNode( lexH, cnp, kDictTId );
        break;

      case kRCurlyLexTId:
        if( cnp == nullptr )
          _objSyntaxError(lexH,"An end of 'object' was encountered without an associated 'object' start.");
        else
          cnp = cnp->parent;
        break;

      case kLHardLexTId:        
        cnp = _objCreateConainerNode( lexH, cnp, kListTId );
        break;

      case kRHardLexTId:
        if( cnp == nullptr )
          rc = _objSyntaxError(lexH,"An end of 'array' was encountered without an associated 'array' start.");
        else
          cnp = cnp->parent;
        break;

      case kColonLexTId:
        if( cnp == nullptr || !cnp->is_pair() )
          rc = _objSyntaxError(lexH,"A colon was encountered outside a 'pair' node.");
        break;

      case kCommaLexTId:
        if( cnp == nullptr || (!cnp->is_list() && !cnp->is_dict()) )
          rc = _objSyntaxError(lexH,"Unexpected comma outside of 'array' or 'object'.");
        break;
        
      case kRealLexTId:
        _objCreateValueNode( cnp, lexTokenDouble(lexH), "real" );
        break;
        
      case kIntLexTId:
        _objCreateValueNode( cnp, lexTokenInt(lexH), "int" );
        break;
        
      case kHexLexTId:
        _objCreateValueNode( cnp, lexTokenInt(lexH), "int", kHexFl );
        break;
        
      case kTrueLexTId:
        _objCreateValueNode( cnp, true, "true" );        
        break;

      case kFalseLexTId:
        _objCreateValueNode( cnp, false, "false" );        
        break;

      case kNullLexTId:
        _objAppendLeftMostNode( cnp, _objAllocate( kNullTId, cnp ));
        break;        

      case kIdentLexTId:
      case kQStrLexTId:
        {
          
          // if the parent is an object then this string must be a pair label
          if( cnp->is_dict() )
            cnp = _objAppendLeftMostNode( cnp, _objAllocate( kPairTId, cnp ));
               
          
          char*    v       = memDuplStr(lexTokenText(lexH),lexTokenCharCount(lexH));
          unsigned identFl = lexId == kIdentLexTId ? kIdentFl    : 0;
          
          
          _objCreateValueNode<char*>( cnp, v, "string", identFl );
        }
        break;

      case kEofLexTId:
        break;

      default:
        _objSyntaxError(lexH,"Unknown token type (%i) in text.", int(lexId) );
    }

    // if this is a pair node and it now has both values 
    // then make the parent 'object' the current node
    if( cnp->is_pair() && cnp->child_count()==2 )
      cnp = cnp->parent;

    
  }
  
  objRef = root;
  
 errLabel:
  rc_t rc0 = lexDestroy(lexH);

  return rc != kOkRC ? rc : rc0;
  
}

cw::rc_t cw::objectFromFile( const char* fn, object_t*& objRef )
{
  rc_t     rc         = kOkRC;
  unsigned bufByteCnt = 0;
  char*    buf        = NULL;
  
  if(( buf = fileFnToStr(fn, &bufByteCnt)) != NULL )
  {
    rc = objectFromString( buf, objRef );
    memRelease(buf);
  }

  return rc;
}


void cw::objectPrintTypes( object_t* o0 )
{
  if( o0->is_container() )
    for(object_t* o = o0->u.children; o!=nullptr; o=o->sibling)
      objectPrintTypes(o);

  printf("%s ",o0->type->label);
}






