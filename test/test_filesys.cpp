#include <gtest/gtest.h>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"

#include "cwTest.h"
#include "cwObject.h"

#include "cwFileSys.h"
#include "cwMem.h"
#include "cwText.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <algorithm>
#include <vector>
#include <string>


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

// Test fixture for filesystem tests that need a pre-made directory structure
class FileSysTestFixture : public ::testing::Test {
protected:
    char tempDir[32] = "/tmp/filesys_test_XXXXXX";
    std::string baseDir;
    std::string subDir;
    std::string file1;
    std::string hiddenFile;
    std::string file2;
    std::string hiddenSubFile;
    std::string linkToFile1;

    void SetUp() override {
        // Create a unique temporary directory
        char* dir = mkdtemp(tempDir);
        ASSERT_NE(dir, nullptr);
        baseDir = dir;

        // Create directory structure
        subDir = baseDir + "/sub_dir";
        ASSERT_EQ(mkdir(subDir.c_str(), 0755), 0);

        // Create files
        file1 = baseDir + "/file1.txt";
        std::ofstream(file1).close();

        hiddenFile = baseDir + "/.hidden_file";
        std::ofstream(hiddenFile).close();

        file2 = subDir + "/file2.txt";
        std::ofstream(file2).close();
        
        hiddenSubFile = subDir + "/.hidden_sub_file";
        std::ofstream(hiddenSubFile).close();

        // Create a symlink
        linkToFile1 = baseDir + "/link_to_file1";
        ASSERT_EQ(symlink(file1.c_str(), linkToFile1.c_str()), 0);
    }

    void TearDown() override {
        // Using a shell command for robust recursive deletion
        std::string command = "rm -rf " + baseDir;
        int result = system(command.c_str());
        ASSERT_EQ(result, 0);
    }

    // Helper to check if a name is in the dirEntries result
    bool findInDirEntries(const std::string& name, dirEntry_t* entries, unsigned count) {
        for (unsigned i = 0; i < count; ++i) {
            if (name == entries[i].name) {
                return true;
            }
        }
        return false;
    }
};

TEST_F(FileSysTestFixture, IsDirIsFileIsLink) {
    // Test isDir
    EXPECT_TRUE(isDir(baseDir.c_str()));
    EXPECT_TRUE(isDir(subDir.c_str()));
    EXPECT_FALSE(isDir(file1.c_str()));
    EXPECT_FALSE(isDir("non_existent_dir"));
    EXPECT_FALSE(isDir(nullptr));

    // Test isFile
    EXPECT_TRUE(isFile(file1.c_str()));
    EXPECT_FALSE(isFile(baseDir.c_str()));
    EXPECT_TRUE(isFile(linkToFile1.c_str())); // isFile follows symlinks
    EXPECT_TRUE(isFile(linkToFile1.c_str())); 
    EXPECT_FALSE(isFile("non_existent_file"));
    EXPECT_FALSE(isFile(nullptr));

    // Test isLink
    EXPECT_TRUE(isLink(linkToFile1.c_str()));
    EXPECT_FALSE(isLink(file1.c_str()));
    EXPECT_FALSE(isLink(subDir.c_str()));
    EXPECT_FALSE(isLink("non_existent_link"));
    EXPECT_FALSE(isLink(nullptr));
}

TEST_F(FileSysTestFixture, MakeFn) {
    char* fn1 = makeFn("/dir", "file", "ext", nullptr);
    EXPECT_STREQ(fn1, "/dir/file.ext");
    mem::release(fn1);

    char* fn2 = makeFn("/dir/", "file", "ext", nullptr);
    EXPECT_STREQ(fn2, "/dir/file.ext");
    mem::release(fn2);
    
    char* fn3 = makeFn("dir", "file", nullptr, nullptr);
    EXPECT_STREQ(fn3, "dir/file");
    mem::release(fn3);

    char* fn4 = makeFn(nullptr, "file", "ext", nullptr);
    EXPECT_STREQ(fn4, "file.ext");
    mem::release(fn4);

    char* fn5 = makeFn("/dir", "file", "ext", "sub1", "sub2", nullptr);
    EXPECT_STREQ(fn5, "/dir/sub1/sub2/file.ext");
    mem::release(fn5);
}

TEST_F(FileSysTestFixture, MakeVersionedFn) {
    std::string vFile1 = baseDir + "/vfile_0.log";
    std::ofstream(vFile1).close();
    std::string vFile2 = baseDir + "/vfile_1.log";
    std::ofstream(vFile2).close();

    char* next_fn = makeVersionedFn(baseDir.c_str(), "vfile", "log", nullptr);
    EXPECT_STREQ(next_fn, (baseDir + "/vfile_2.log").c_str());

    mem::release(next_fn);
}

TEST_F(FileSysTestFixture, MakeAndRmDir) {
    std::string newDir = baseDir + "/new_dir";
    
    // Make dir
    EXPECT_EQ(makeDir(newDir.c_str()), kOkRC);
    EXPECT_TRUE(isDir(newDir.c_str()));

    // Make dir that already exists (should be ok)
    EXPECT_EQ(makeDir(newDir.c_str()), kOkRC);
    
    // Remove dir
    EXPECT_EQ(rmDir(newDir.c_str()), kOkRC);
    EXPECT_FALSE(isDir(newDir.c_str()));
    
    // Remove non-existent dir (should be ok)
    EXPECT_EQ(rmDir(newDir.c_str()), kOkRC);
}

TEST_F(FileSysTestFixture, MakeVersionedDirectory) {
    char* dir1 = makeVersionedDirectory(baseDir.c_str(), "versioned_dir");
    EXPECT_STREQ(dir1, (baseDir + "/versioned_dir_0").c_str());
    EXPECT_TRUE(isDir(dir1));

    char* dir2 = makeVersionedDirectory(baseDir.c_str(), "versioned_dir");
    EXPECT_STREQ(dir2, (baseDir + "/versioned_dir_1").c_str());
    EXPECT_TRUE(isDir(dir2));

    mem::release(dir1);
    mem::release(dir2);
}

TEST_F(FileSysTestFixture, PathPartsExtended) {
    pathPart_t* pp1 = pathParts("/a/b/c/file.tar.gz");
    ASSERT_NE(pp1, nullptr);
    EXPECT_STREQ(pp1->dirStr, "/a/b/c");
    EXPECT_STREQ(pp1->fnStr, "file");
    EXPECT_STREQ(pp1->extStr, "tar.gz");
    mem::release(pp1);

    pathPart_t* pp2 = pathParts("file_no_ext");
    ASSERT_NE(pp2, nullptr);
    EXPECT_STREQ(pp2->dirStr, ".");
    EXPECT_STREQ(pp2->fnStr, "file_no_ext");
    EXPECT_EQ(pp2->extStr, nullptr);
    mem::release(pp2);

    pathPart_t* pp3 = pathParts("/a/b/c/");
    ASSERT_NE(pp3, nullptr);
    EXPECT_STREQ(pp3->dirStr, "/a/b/c/");
    EXPECT_EQ(pp3->fnStr, nullptr);
    EXPECT_EQ(pp3->extStr, nullptr);
    mem::release(pp3);
}

TEST_F(FileSysTestFixture, ReplaceFunctions) {
    const char* original = "/path/to/file.txt";

    char* newDir = replaceDirectory(original, "/new/path");
    EXPECT_STREQ(newDir, "/new/path/file.txt");
    mem::release(newDir);

    char* newFn = replaceFilename(original, "newfile");
    EXPECT_STREQ(newFn, "/path/to/newfile.txt");
    mem::release(newFn);

    char* newExt = replaceExtension(original, "md");
    EXPECT_STREQ(newExt, "/path/to/file.md");
    mem::release(newExt);
}

TEST_F(FileSysTestFixture, DirEntries_FilesOnly) {
    unsigned count = 0;
    dirEntry_t* entries = dirEntries(baseDir.c_str(), kFileFsFl, &count);
    ASSERT_NE(entries, nullptr);

    EXPECT_EQ(count, 1);
    EXPECT_TRUE(findInDirEntries("file1.txt", entries, count));
    
    mem::release(entries);
}

TEST_F(FileSysTestFixture, DirEntries_DirsOnly) {
    unsigned count = 0;
    dirEntry_t* entries = dirEntries(baseDir.c_str(), kDirFsFl, &count);
    ASSERT_NE(entries, nullptr);

    EXPECT_EQ(count, 1);
    EXPECT_TRUE(findInDirEntries("sub_dir", entries, count));
    
    mem::release(entries);
}

TEST_F(FileSysTestFixture, DirEntries_LinksOnly) {
    unsigned count = 0;
    dirEntry_t* entries = dirEntries(baseDir.c_str(), kLinkFsFl, &count);
    ASSERT_NE(entries, nullptr);

    EXPECT_EQ(count, 1);
    EXPECT_TRUE(findInDirEntries("link_to_file1", entries, count));
    
    mem::release(entries);
}

TEST_F(FileSysTestFixture, DirEntries_IncludeHidden) {
    unsigned count = 0;
    unsigned flags = kFileFsFl | kInvisibleFsFl;
    dirEntry_t* entries = dirEntries(baseDir.c_str(), flags, &count);
    ASSERT_NE(entries, nullptr);

    EXPECT_EQ(count, 2);
    EXPECT_TRUE(findInDirEntries("file1.txt", entries, count));
    EXPECT_TRUE(findInDirEntries(".hidden_file", entries, count));
    
    mem::release(entries);
}

TEST_F(FileSysTestFixture, DirEntries_Recurse) {
    unsigned count = 0;
    unsigned flags = kFileFsFl | kDirFsFl | kRecurseFsFl;
    dirEntry_t* entries = dirEntries(baseDir.c_str(), flags, &count);
    ASSERT_NE(entries, nullptr);

    // Expecting file1.txt from baseDir and file2.txt from sub_dir
    EXPECT_EQ(count, 3);
    EXPECT_TRUE(findInDirEntries("file1.txt", entries, count));
    EXPECT_TRUE(findInDirEntries("file2.txt", entries, count));
    
    mem::release(entries);
}

TEST_F(FileSysTestFixture, DirEntries_RecurseWithFullPath) {
    unsigned count = 0;
    unsigned flags = kFileFsFl | kDirFsFl | kRecurseFsFl | kFullPathFsFl;
    dirEntry_t* entries = dirEntries(baseDir.c_str(), flags, &count);
    ASSERT_NE(entries, nullptr);

    EXPECT_EQ(count, 3);

    bool file_1_fl = false;
    bool file_2_fl = false;
    
    // The find helper won't work with full paths, so we check manually
    for(unsigned i=0; i<count; ++i)
    {
      if(textIsEqual(entries[i].name,file1.c_str()) )
        file_1_fl = true;
      if( textIsEqual(entries[i].name,file2.c_str()) )
        file_2_fl = true;
        
    }

    EXPECT_TRUE(file_1_fl);
    EXPECT_TRUE(file_2_fl);
    
    mem::release(entries);
}

TEST_F(FileSysTestFixture, DirEntries_RecurseAndHidden) {
    unsigned count = 0;
    unsigned flags = kFileFsFl | kDirFsFl | kRecurseFsFl | kInvisibleFsFl;
    dirEntry_t* entries = dirEntries(baseDir.c_str(), flags, &count);
    ASSERT_NE(entries, nullptr);

    EXPECT_EQ(count, 5);
    EXPECT_TRUE(findInDirEntries("file1.txt", entries, count));
    EXPECT_TRUE(findInDirEntries(".hidden_file", entries, count));
    EXPECT_TRUE(findInDirEntries("file2.txt", entries, count));
    EXPECT_TRUE(findInDirEntries(".hidden_sub_file", entries, count));
    
    mem::release(entries);
}
