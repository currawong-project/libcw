#include <gtest/gtest.h>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"

#include "cwTest.h"
#include "cwObject.h"

#include "cwFileSys.h"
#include "cwMem.h"
#include "cwText.h"

using namespace cw;
using namespace cw::filesys;


TEST(FileSysTest, PathParts)
{
  const char* my_fn = "dir1/dir2/file.ext";
  
  // parse the parts of 'my_fn'
  filesys::pathPart_t* pp = filesys::pathParts(my_fn);

  EXPECT_STREQ(pp->dirStr,"dir1/dir2");
  EXPECT_STREQ(pp->fnStr,"file");
  EXPECT_STREQ(pp->extStr,"ext");

  // reconstruct the file name from the parts
  char* fn = filesys::makeFn( pp->dirStr, pp->fnStr, pp->extStr, nullptr );

  EXPECT_STREQ(fn, my_fn);
  
  mem::release(pp);
  mem::release(fn);
}

TEST(FileSysTest, ExpandPath)
{
  
  const char myPath[] = "~/src/foo";
  const char* home = getenv("HOME");

  char* fn = filesys::makeFn( getenv("HOME"), "src/foo", nullptr, nullptr );

  // expand a filename that has a tilde prefix 
  char* expPath = filesys::expandPath(myPath);

  EXPECT_STREQ(expPath,fn);

  mem::release(fn);
  mem::release(expPath);
}
