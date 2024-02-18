#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwNbMem.h"
#include "cwThread.h"

/*
=======================
0000 void*    mem   - pointer to base of memory (also pointer to this block_t record)
0008 var_t*   var   - pointer to first var
0016 block_t* link  - link to next block
0024 unsigned byteN - count of bytes following first
0028 unsigned       - (reserved) 
0032 unsigned byteN - data byte count (16)
0036 unsigned refN  - reference count
0040                                    <----- first data word
0044
0048 
0052 
0056 unsigned byteN - data byte count (16)
0060 unsigned refN  - reference count 
0064                                    <----- first data word
0068
0072
0076


 */

namespace cw
{
  namespace nbmem
  {
    typedef struct var_str
    {
      std::atomic<unsigned long long> refN; //
      unsigned                        byteN; // byteN is the size of the data area of this variable (The total size is (sizeof(var_t) + byteN)
      unsigned                        reserved; //
    } var_t;

    typedef struct block_str
    {
      char*             mem;    // base of data memory for this block | block |var0|data0|var1|data1| |varx|datax|  
      var_t*            var;    //
      struct block_str* link;   //
      unsigned          byteN;  // mem bytes = sizeof(block_t) + byteN
      unsigned          res0;   //
    } block_t;
    
    typedef struct nbmem_str
    {
      unsigned blockByteN;   //
      block_t* base;         //
    } nbmem_t;

    nbmem_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,nbmem_t>(h); }
    
    rc_t _destroy( nbmem_t* p )
    {
      rc_t     rc = kOkRC;
      block_t* b  = p->base;
      
      while( b!=nullptr )
      {
        block_t* b0 = b->link;
        
        mem::release(b);
        b = b0;          
      }
      
      mem::release(p);
      return rc;
    }

    block_t* _alloc_block( unsigned byteN )
    {
      unsigned blockByteN = byteN + sizeof(block_t);
      char*    mem        = mem::allocZ<char>(blockByteN);
      block_t* b          = (block_t*)mem;
  
      b->mem        = mem;
      b->byteN      = byteN;
      b->link       = nullptr;
      b->var        = (var_t*)( mem + sizeof(block_t) );
      b->var->byteN = byteN - sizeof(var_t);
      b->var->refN  = 0;

      return b;
    }

    // 
    void* _alloc_var( var_t* v, unsigned byteN, char* dataAreaEnd )
    {
      // 
      while( (char*)v < dataAreaEnd )
      {
        char* b = (char*)v;          // base of this variable memory 
        char* d = b + sizeof(var_t); // ptr to variable data area

        // is this variable available?
        if( v->refN == 0 && v->byteN > byteN )
        {
            
          v->refN = 1; // ATOMIC mark variable as unavailable - should set thread id NOT 1

          // if there is more than sizeof(var_t) extra data space ...          
          if( v->byteN > byteN + sizeof(var_t) )
          {
            // ... then split the space with another variable
            unsigned byte0N = v->byteN;

            // setup the split var_t
            var_t* v0 = (var_t*)(d + byteN);
            v0->byteN = byte0N - (byteN + sizeof(var_t));
            v0->refN  = 0;

            // update the leading var size - last
            v->byteN = byteN;  // ATOMIC release
          }
          
          return d;
        }

        // v is not available advance to the next var
        v = (var_t*)(d + v->byteN);
      }
      
      return nullptr;
    }
  }   
}


cw::rc_t cw::nbmem::create(  handle_t& h, unsigned blockByteN )
{
  rc_t rc = kOkRC;
  if((rc = destroy(h)) != kOkRC )
    return rc;

  nbmem_t* p     = mem::allocZ< nbmem_t >(1);  

  p->base       = _alloc_block( blockByteN );
  p->blockByteN = blockByteN;
  
  h.set(p);
    
  return rc;
}

cw::rc_t cw::nbmem::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
  if( !h.isValid() )
    return rc;

  nbmem_t* p = _handleToPtr(h);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();
  
  return rc;  
}

void* cw::nbmem::alloc( handle_t h, unsigned byteN )
{
  nbmem_t* p      = _handleToPtr(h);
  block_t* b      = p->base;
  void*    result = nullptr;

  // try to locate an available variable in the existing blocks
  for(; b!=nullptr; b=b->link)
    if((result = _alloc_var( b->var, byteN, b->mem + b->byteN )) != nullptr )
      break;

  // if no available var space was found
  if( b == nullptr )
  {
    // the new block size must be at least as large as the requested variable size
    unsigned blockByteN = std::max( p->blockByteN, byteN );

    // allocate a new block
    block_t* b = _alloc_block( blockByteN );

    // try the var allocation again
    result = _alloc_var( b->var, byteN, b->mem + b->byteN );

    // link in the new block
    b->link = p->base;
    
    p->base = b;  // ATOMIC
  }
  
  return result;
}

void  cw::nbmem::release(  handle_t h, void* v )
{
  nbmem_t* p = _handleToPtr(h);
  block_t* b = p->base;
  for(; b!=nullptr; b=b->link)
  {
    // if the memory to free is in this block
    if( (b->mem <= (char*)v) && ((char*)v < b->mem + (sizeof(block_t) + b->byteN)) )
    {
      // v points to data space - back up by the size of var_t to point to the block header
      var_t* var = static_cast<var_t*>(v) - 1;

      var->refN -= 1;   // ATOMIC 

      // check for too many frees
      if( var->refN < 0 )
      {
        var->refN = 0; // ATOMIC
        cwLogError(kInvalidOpRC,"An nbmem memory block was doube freed.");
      }
      
      return;
    }
  }

  cwLogError(kInvalidArgRC,"Invalid memory pointer passed to nbmem::release().");
}

void cw::nbmem::report( handle_t h )
{
  nbmem_t* p = _handleToPtr(h);  
  unsigned i = 0;
  block_t* b = p->base;
  for(; b!=nullptr; b=b->link)
  {
    printf("mem:%p byteN:%i var:%p link:%p\n",b->mem,b->byteN,b->var,b->link);
    
    var_t*   v        = b->var;
    char*    blockEnd = b->mem + sizeof(block_t) + b->byteN;
    
    while( (char*)v < blockEnd )
    {
      void* data = (v+1);

      if( v->refN > 0 )
      {
        unsigned* val = static_cast<unsigned *>(data);
        var_t*    var = static_cast<var_t*>(data) - 1;
        printf("%i %i bN:%i ref:%li 0x%p\n",i,*val, var->byteN, var->refN, val);
        
        i += 1;
      }

      v = (var_t*)( static_cast<char*>(data) + v->byteN);

    }
  }
}

cw::rc_t cw::nbmem::test()
{
  unsigned preallocMemN = 64;
  unsigned N            = 64;
  void*    varA[ N ];
  handle_t h;
  rc_t     rc;

  // create a nbmem mgr object
  if((rc = create(  h, preallocMemN )) == kOkRC )
  {
    // allocate N blocks of memory of length sizeof(unsigned)
    for(unsigned i=0; i<N; ++i)
    {
      unsigned* ip;
      if((ip = (unsigned*)alloc(h,sizeof(unsigned))) != nullptr )
      {
        *ip     = i;   // set the data value
        varA[i] = ip;  // keep a ptr to the data block for eventual release
        printf("%i alloc assign\n",i);
      }
      else
      {
        printf("alloc failed\n");
      }
    }

    // report the state of the nbmem mgr and data values
    report( h );

    // free each of the memory blocks which were allocate above
    for(unsigned i=0; i<N; ++i)
      release(h,varA[i]);

    
    destroy( h );
  }
  return rc;
}


namespace cw
{
  namespace nbmem
  {
    struct text_ctx_str;
    
    typedef struct nbm_thread_str
    {
      struct test_ctx_str* ctx;
      void**               varA;
      unsigned             varN;
      thread::handle_t     threadH;
    } nbm_thread_t;

    typedef struct test_ctx_str
    {
      nbm_thread_t* threadA;
      unsigned      threadN;
      handle_t      nbmH;
    } test_ctx_t;
    
    //
    bool _test_thread_func( void* arg )
    {
      nbm_thread_t* r = static_cast<nbm_thread_t*>(arg);

      // select a random variable index
      unsigned idx = lround((double)rand() * (r->varN-1) / RAND_MAX);
      cwAssert( 0 <= idx && idx < r->varN );

      // if the variable has not been allocated then allocate it ...
      if( r->varA[idx] == nullptr )
      {
        r->varA[idx] = alloc( r->ctx->nbmH, 10 );
      }
      else // ... otherwise free it
      {
        release( r->ctx->nbmH, r->varA[idx] );
        r->varA[idx] = nullptr;
      }

      return true;
    }
  }   
}

cw::rc_t cw::nbmem::test_multi_threaded()
{
  unsigned   preallocMemN = 1024;
  unsigned   threadN      = 10;
  unsigned   threadVarN   = 20;
  rc_t       rc           = kOkRC;
  test_ctx_t ctx;
 
  ctx.threadN = threadN;
  ctx.threadA = mem::allocZ<nbm_thread_t>(ctx.threadN);
  
  if((rc = create(  ctx.nbmH, preallocMemN )) != kOkRC )
    goto errLabel;

  
  // create ctx.threadN threads
  for(unsigned i=0; i<ctx.threadN; ++i)
  {
    ctx.threadA[i].ctx  = &ctx;
    ctx.threadA[i].varA = mem::allocZ<void*>(threadVarN);
    ctx.threadA[i].varN = threadVarN;
    
    if((rc = thread::create(ctx.threadA[i].threadH, _test_thread_func, ctx.threadA + i, "nb_mem" )) != kOkRC )
      break;
  }
  
  if( rc == kOkRC )
  {

    // start each thread
    for(unsigned i=0; i<ctx.threadN; ++i)
      thread::unpause(ctx.threadA[i].threadH);
    
    char c;
    
    while( c != 'q' )
    {

      c = (char)fgetc(stdin);
      fflush(stdin);

      switch( c )
      {
        case 'q':
          break;
      }     
    }
  }

 errLabel:
  
  // destroy each thread
  for(unsigned i=0; i<ctx.threadN; ++i)
    if( ctx.threadA[i].threadH.isValid() )
      thread::destroy( ctx.threadA[i].threadH );

  mem::release(ctx.threadA);
  
  return rc;
}


