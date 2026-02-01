#include <gtest/gtest.h>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwCommon.h" // For cwIsFlag
#include "cwLog.h"    // For cwLogWarning (if g_warn_on_alloc_fl is used)
#include <cstring>    // For strlen, strcmp, memcpy
#include <stdexcept>  // For potential exceptions in custom memory management

using namespace cw;
using namespace cw::mem;

// Define a test fixture for cwMem
class MemTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset any global state if necessary before each test
        clear_warn_on_alloc();
    }

    void TearDown() override {
        // Ensure all allocated memory is freed (though individual tests should do this)
    }

    // Helper to get raw allocated size (including cwMem header)
    // This directly accesses cwMem's internal structure for testing purposes.
    unsigned getRawAllocatedSize(void* p) {
        if (p == nullptr) return 0;
        return static_cast<unsigned*>(p)[-2];
    }
};

// Test for basic alloc and free
TEST_F(MemTest, BasicAllocAndFree) {
    void* p = alloc<char>(100); // Allocate 100 bytes
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(getRawAllocatedSize(p), 100 + 2 * sizeof(unsigned));
    release(p);
    EXPECT_EQ(p, nullptr);

    p = allocZ<char>(50); // Allocate 50 bytes and zero
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(getRawAllocatedSize(p), 50 + 2 * sizeof(unsigned));
    for (unsigned i = 0; i < 50; ++i) {
        EXPECT_EQ(static_cast<char*>(p)[i], 0);
    }
    release(p);
    EXPECT_EQ(p, nullptr);

    // Test zero-sized allocation (should return non-null and be freed safely)
    p = alloc<char>(0);
    ASSERT_NE(p, nullptr); // Should return a valid pointer for 0-byte allocation
    EXPECT_EQ(byteCount(p), 0); // Should only have header
    release(p);
    EXPECT_EQ(p, nullptr);
}

// Test for byteCount
TEST_F(MemTest, ByteCount) {
    char* p = alloc<char>(100);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(byteCount(p), 100);
    release(p);
    EXPECT_EQ(byteCount(nullptr), 0);

    // Test after resize
    p = alloc<char>(50);
    p = resize<char>(p, 100); // Expand
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(byteCount(p), 100);
    release(p);

    p = alloc<char>(100);
    p = resize<char>(p, 50); // Shrink - should not reallocate, return same ptr
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(byteCount(p), 100); // Size should remain old size
    release(p);
}

// Test allocStr
TEST_F(MemTest, AllocStr) {
    char* s = allocStr("Hello");
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "Hello");
    EXPECT_EQ(byteCount(s), strlen("Hello") + 1); // +1 for null terminator
    release(s);
    EXPECT_EQ(s, nullptr);

    s = allocStr("");
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "");
    EXPECT_EQ(byteCount(s), 1); // For null terminator
    release(s);

    s = allocStr(static_cast<char*>(nullptr));
    EXPECT_EQ(s, nullptr);
}

// Test duplStr
TEST_F(MemTest, DuplStr) {
    char* s = duplStr("World");
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "World");
    EXPECT_EQ(byteCount(s), strlen("World") + 1);
    release(s);

    s = duplStr("Test", 2); // Duplicating 2 chars + null
    ASSERT_NE(s, nullptr);
    EXPECT_STREQ(s, "Te");
    EXPECT_EQ(byteCount(s), 2 + 1);
    release(s);

    char* null_s = duplStr(static_cast<char*>(nullptr));
    EXPECT_EQ(null_s, nullptr);
}

// Test reallocStr
TEST_F(MemTest, ReallocStr) {
    char* s0 = allocStr("short");
    char* original_s0 = s0;
    ASSERT_NE(s0, nullptr);

    // Case 1: New string fits in existing allocation
    s0 = reallocStr(s0, "longer"); // "longer" (6+1) <= "short" (5+1) + header in terms of memory
    ASSERT_NE(s0, nullptr);
    EXPECT_STREQ(s0, "longer");
    // Should reuse the same memory block because new string is longer than original, but possibly within allocated size.
    // The previous bug was here. Now it should only reallocate if new string (with null) > allocated size
    // In this specific test, if original was "short" (5 chars), alloc was 5+1+header. "longer" is (6+1)+header.
    // It should reallocate, and the pointer should change.
    // However, if we test with "new" into "oldlongerstring", it should fit and return same pointer.
    
    // Let's reset and test carefully for pointer change
    release(s0);
    s0 = allocStr("verylongstring"); // Allocate a large enough buffer initially
    original_s0 = s0;
    
    s0 = reallocStr(s0, "short"); // Shrink content. Should not reallocate, pointer stays same.
    ASSERT_NE(s0, nullptr);
    EXPECT_STREQ(s0, "short");
    EXPECT_EQ(s0, original_s0); // Pointer should be the same
    
    // Test reallocation when new string is genuinely larger
    s0 = reallocStr(s0, "superduperlongstringthatwontfit");
    ASSERT_NE(s0, nullptr);
    EXPECT_STREQ(s0, "superduperlongstringthatwontfit");
    EXPECT_NE(s0, original_s0); // Pointer should change as realloc happened
    release(s0);

    // Test with s0 = nullptr
    s0 = reallocStr(static_cast<char*>(nullptr), "initial");
    ASSERT_NE(s0, nullptr);
    EXPECT_STREQ(s0, "initial");
    release(s0);

    // Test with s1 = nullptr
    s0 = allocStr("original");
    s0 = reallocStr(s0, (const char*)nullptr);
    EXPECT_EQ(s0, nullptr);
}

// Test appendStr
TEST_F(MemTest, AppendStr) {
    char* s0 = allocStr("Hello");
    s0 = appendStr(s0, " World");
    ASSERT_NE(s0, nullptr);
    EXPECT_STREQ(s0, "Hello World");
    release(s0);

    s0 = appendStr((char*)nullptr, "Initial");
    ASSERT_NE(s0, nullptr);
    EXPECT_STREQ(s0, "Initial");
    s0 = appendStr(s0, (const char*)nullptr); // Append nullptr
    EXPECT_STREQ(s0, "Initial");
    s0 = appendStr(s0, ""); // Append empty string
    EXPECT_STREQ(s0, "Initial");
    release(s0);

    s0 = allocStr("Base");
    s0 = appendStr(s0, " Add");
    s0 = appendStr(s0, " More");
    EXPECT_STREQ(s0, "Base Add More");
    release(s0);
}

// Test set_warn_on_alloc and clear_warn_on_alloc
TEST_F(MemTest, WarnOnAlloc) {
    // These tests primarily check that the functions don't crash.
    // Verifying log output would require more complex mocking/redirection of cwLog,
    // which is beyond the scope of a simple unit test for cwMem.
    set_warn_on_alloc();
    char* p = alloc<char>(10); // Should trigger a warning if logging is configured
    release(p);
    clear_warn_on_alloc();
    p = alloc<char>(10); // Should not trigger a warning
    release(p);
}

// Test for allocZ - zeroing memory
TEST_F(MemTest, AllocZZeroing) {
    char* p = allocZ<char>(20);
    ASSERT_NE(p, nullptr);
    for (unsigned i = 0; i < 20; ++i) {
        EXPECT_EQ(p[i], 0) << "Byte at index " << i << " was not zeroed.";
    }
    release(p);
}

// Test for resizeZ - zeroing only new part
TEST_F(MemTest, ResizeZZeroing) {
    char* p = alloc<char>(10);
    constexpr char AF = 0xAF;
    constexpr char BE = 0xBE;
    ASSERT_NE(p, nullptr);
    // Fill with non-zero to check selective zeroing
    for (unsigned i = 0; i < 10; ++i)
    {
        p[i] = AF;
    }
    
    // Resize to a larger size, zeroing only new part
    char* old_p = p;
    p = resizeZ(p, 20); // Expand to 20 bytes
    ASSERT_NE(p, nullptr);
    
    //EXPECT_EQ(p, old_p); // Should reuse if possible without real realloc
    
    // Check old part remains unchanged
    for (unsigned i = 0; i < 10; ++i)
    {
      EXPECT_EQ(p[i], AF) << "Old byte at index " << i << " was changed.";
    }
    
    // Check new part is zeroed
    for (unsigned i = 10; i < 20; ++i)
    {
        EXPECT_EQ(p[i], 0) << "New byte at index " << i << " was not zeroed.";
    }
    release(p);

    // Test resizeZ that forces realloc
    p = alloc<char>(5);
    for (unsigned i = 0; i < 5; ++i) {
        p[i] = BE;
    }
    old_p = p;
    p = resizeZ(p, 100); // Forces realloc, old content might not be preserved if new buffer.
    ASSERT_NE(p, nullptr);
    // If _alloc copies existing data then zeros the expanded part, this check is valid.
    // cwMem::_alloc does memcpy from p0_1, which is the entire block including header.
    // It then zeros (p)+p0N... so original content should be there.
    for (unsigned i = 0; i < 5; ++i) {
        EXPECT_EQ(p[i], BE) << "Old byte at index " << i << " was changed after forced realloc.";
    }
    for (unsigned i = 5; i < 100; ++i) {
        EXPECT_EQ(p[i], 0) << "New byte at index " << i << " was not zeroed after forced realloc.";
    }
    release(p);
}

// Test allocDupl
TEST_F(MemTest, AllocDupl) {
    const char* original_data = "raw bytes\0with\0nulls";
    unsigned original_len = strlen("raw bytes") + 1 + strlen("with") + 1 + strlen("nulls"); // Actual length including nulls

    void* p = allocDupl(original_data, original_len);
    ASSERT_NE(p, nullptr);    
    EXPECT_EQ(byteCount(p), original_len);
    EXPECT_EQ(memcmp(p, original_data, original_len), 0);
    release(p);

}
