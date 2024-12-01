//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwB23Tree.h"

cw::rc_t   cw::b23::test( const object_t* cfg )
{
  rc_t rc = kOkRC;

  /*
  typedef struct tree<unsigned,char*,kInvalidIdx> tree_t;

  tree_t* t = create<unsigned,char*,kInvalidIdx>(4);

  destroy(t);
  */

  typedef struct tree_str<unsigned,const char*,kInvalidIdx> tree_t;

  tree_t t;
  typedef struct kv_str
  {
    unsigned k;
    const char* v;
  } kv_t;

  kv_t kvA[] = { {0,"zero"}, {1,"one"}, {2,"two"}, {3,"three"}, {4,"four"}, {5,"five"},
                 {6,"siz"}, {7,"seven"}, {8,"eight"}, {9,"nine"},  {10,"ten"}, {11,"eleven"},
                 {12,"twelve"}, {13,"thirt"}, {14,"fourt"}, {15,"fift"}, {16,"sixt"} };

  unsigned kvN = sizeof(kvA)/sizeof(kvA[0]);
  
  t.create(4);

  for(unsigned i=0; i<kvN; ++i)
  {
    t.insert(kvA[i].k,kvA[i].v);
    t.print();
  }

  t.destroy();
  
  return rc;
}

