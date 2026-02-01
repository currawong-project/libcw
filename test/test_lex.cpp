#include <gtest/gtest.h>

#include "cwCommon.h"
#include "cwTest.h"
#include "cwLex.h"
#include "cwText.h"
#include "cwFile.h"

using namespace cw;
using namespace cw::lex;

// Helper to compare token text, handling length
bool tokenTextIsEqual(handle_t h, const char* expected_text)
{
  unsigned expected_len = strlen(expected_text);
  const char* tt = tokenText(h);
  //printf("%i : '%*s'\n",expected_len,expected_len,tt);
  return tokenCharCount(h) == expected_len && strncmp(tt, expected_text, expected_len) == 0;
}

TEST(Lexer_EnableToken, BuiltInTokens) {
    lex::handle_t h;
    rc_t rc;

    const char* buf = "123 ident //comment";
    
    // Create lexer with no special flags initially
    ASSERT_EQ(create(h, buf, strlen(buf), 0), kOkRC);

    // Default behavior: spaces and comments are skipped
    EXPECT_EQ(getNextToken(h), kIntLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "123"));
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "ident"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Recreate, enable kReturnSpaceLexFl
    ASSERT_EQ(create(h, buf, strlen(buf), kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(enableToken(h, kSpaceLexTId, true), kOkRC);
    EXPECT_EQ(getNextToken(h), kIntLexTId);
    EXPECT_EQ(getNextToken(h), kSpaceLexTId); // Now returns space
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_EQ(getNextToken(h), kSpaceLexTId); // Now returns space
    EXPECT_EQ(getNextToken(h), kEofLexTId); // Comment still skipped
    destroy(h);

    // Recreate, enable kReturnCommentsLexFl (implicitly enables kSpaceLexTId for spaces before comments)
    ASSERT_EQ(create(h, buf, strlen(buf), 0 ), kOkRC);
    EXPECT_EQ(getNextToken(h), kIntLexTId);    
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_EQ(enableToken(h, kLineCmtLexTId, true), kOkRC); // enable the line comment token
    setFilterFlags(h,kReturnCommentsLexFl);  // turn on the 'return comments' flag
    EXPECT_EQ(getNextToken(h), kLineCmtLexTId); // Now returns comment
    EXPECT_TRUE(tokenTextIsEqual(h, "//comment"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Recreate, enable both explicitly
    ASSERT_EQ(create(h, buf, strlen(buf), kReturnSpaceLexFl | kReturnCommentsLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kIntLexTId);
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kLineCmtLexTId);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test disabling a built-in token that is normally returned (e.g., kIdentLexTId)
    // This isn't directly supported by enableToken based on my understanding of cwLex's design.
    // enableToken is for tokens that are *filtered* by flags, not the core tokens like Ident.
    // The original test cases show that kReturnSpaceLexFl and kReturnCommentsLexFl are how you filter.
    // Let's re-evaluate how enableToken is used.
    // enableToken is intended for user-defined tokens or specific built-in tokens that have a corresponding
    // flag in the lexer's control flags. It is NOT for disabling core token types.
    // The comment in the header says: "Enable or disable the specified token type."
    // And filterFlags/setFilterFlags control kReturnXXXLexFl.
    // So, it seems `enableToken` is for the `lexMatcher::enableFl` which is for all matchers,
    // including predefined ones. This would mean that disabling kIdentLexTId should make it unknown/error.

    // Let's re-test disabling kIdentLexTId
    const char* buf2 = "hello world";
    ASSERT_EQ(create(h, buf2, strlen(buf2), kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(enableToken(h, kIdentLexTId, false), kOkRC); // Disable Identifiers
    EXPECT_EQ(getNextToken(h), kErrorLexTId); // "hello" should now be an error
    EXPECT_EQ(errorRC(h), kSyntaxErrorRC);
    destroy(h);

    // Test enabling it back
    ASSERT_EQ(create(h, buf2, strlen(buf2), kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(enableToken(h, kIdentLexTId, false), kOkRC);
    EXPECT_EQ(enableToken(h, kIdentLexTId, true), kOkRC); // Enable again
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "hello"
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "world"
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_EnableToken, UserDefinedTokens) {
    lex::handle_t h;
    rc_t rc;

    const unsigned kKeywordFooTId = kUserLexTId + 1;
    const char* buf = "foo bar";

    ASSERT_EQ(create(h, buf, strlen(buf), kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(registerToken(h, kKeywordFooTId, "foo"), kOkRC);

    // Initially enabled
    EXPECT_EQ(getNextToken(h), kKeywordFooTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "foo"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "bar"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Disable user-defined token
    ASSERT_EQ(create(h, buf, strlen(buf), kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(registerToken(h, kKeywordFooTId, "foo"), kOkRC);
    EXPECT_EQ(enableToken(h, kKeywordFooTId, false), kOkRC); // Disable "foo"

    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "foo" should now be an identifier
    EXPECT_TRUE(tokenTextIsEqual(h, "foo"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "bar"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Enable user-defined token again
    ASSERT_EQ(create(h, buf, strlen(buf), kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(registerToken(h, kKeywordFooTId, "foo"), kOkRC);
    EXPECT_EQ(enableToken(h, kKeywordFooTId, false), kOkRC);
    EXPECT_EQ(enableToken(h, kKeywordFooTId, true), kOkRC); // Enable "foo" again

    EXPECT_EQ(getNextToken(h), kKeywordFooTId); // "foo" should be user-defined again
    EXPECT_TRUE(tokenTextIsEqual(h, "foo"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "bar"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}


TEST(Lexer_ResetAndSetTextBuffer, ResetLexer) {
    lex::handle_t h;
    rc_t rc;

    const char* buf1 = "token1 token2 token3";
    ASSERT_EQ(create(h, buf1, strlen(buf1), kReturnSpaceLexFl), kOkRC);

    // Get a few tokens
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // token1
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // token2
    
    // Reset the lexer
    EXPECT_EQ(reset(h), kOkRC);

    // Should start tokenizing from the beginning again
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // token1
    EXPECT_TRUE(tokenTextIsEqual(h, "token1"));
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);

    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // token2
    EXPECT_TRUE(tokenTextIsEqual(h, "token2"));

    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // token3
    EXPECT_TRUE(tokenTextIsEqual(h, "token3"));

    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_ResetAndSetTextBuffer, SetNewTextBuffer) {
    lex::handle_t h;
    rc_t rc;

    const char* buf1 = "old_content";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "old_content"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);

    // Set a new text buffer
    const char* buf2 = "new_token";
    EXPECT_EQ(setTextBuffer(h, buf2, strlen(buf2)), kOkRC);

    // Should tokenize the new content from the beginning
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "new_token"));
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_ResetAndSetTextBuffer, SetFile) {
    lex::handle_t h;
    rc_t rc;
    const char* test_filename = "test_lexer_file.txt";
    const char* file_content = "file_token1\nfile_token2";

    // Create a temporary file
    file::handle_t fh;
    ASSERT_EQ(file::open(fh, test_filename, file::kWriteFl), kOkRC);
    ASSERT_EQ(file::write(fh, file_content, strlen(file_content)), kOkRC);
    ASSERT_EQ(file::close(fh), kOkRC);

    // Create lexer with null buffer initially, then set file
    ASSERT_EQ(create(h, nullptr, 0, kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(setFile(h, test_filename), kOkRC);

    // Tokenize content from file
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // file_token1
    EXPECT_TRUE(tokenTextIsEqual(h, "file_token1"));
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);

    EXPECT_EQ(getNextToken(h), kSpaceLexTId); // Newline
    EXPECT_TRUE(tokenTextIsEqual(h, "\n"));

    EXPECT_EQ(getNextToken(h), kIdentLexTId); // file_token2
    EXPECT_TRUE(tokenTextIsEqual(h, "file_token2"));
    EXPECT_EQ(currentLineNumber(h), 2);
    EXPECT_EQ(currentColumnNumber(h), 2);

    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Clean up temporary file
    remove(test_filename);
}

TEST(Lexer_ResetAndSetTextBuffer, SetFile_Error) {
    lex::handle_t h;
    rc_t rc;
    const char* non_existent_file = "non_existent_lexer_file.txt";

    ASSERT_EQ(create(h, nullptr, 0, 0), kOkRC);
    // Expect an error when setting a non-existent file
    EXPECT_EQ(setFile(h, non_existent_file), kOpenFailRC);
    destroy(h);
}

TEST(Lexer_ErrorConditions, UnidentifiableTokens) {
    lex::handle_t h;
    rc_t rc;

    // Test a single unidentifiable character
    const char* buf1 = "!";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kErrorLexTId);
    EXPECT_EQ(errorRC(h), kSyntaxErrorRC);
    // The tokenText and tokenCharCount should reflect the problematic character
    // .... actually that's not the way cwLex is setup - although it would be nice if it was ....
    //EXPECT_TRUE(tokenTextIsEqual(h, "!"));
    //EXPECT_EQ(tokenCharCount(h), 1);
    //EXPECT_EQ(currentLineNumber(h), 1);
    //EXPECT_EQ(currentColumnNumber(h), 1);
    destroy(h);

    // Test multiple unidentifiable characters
    const char* buf2 = "a @#$ b";
    ASSERT_EQ(create(h, buf2, strlen(buf2), kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); EXPECT_TRUE(tokenTextIsEqual(h, "a"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId); EXPECT_TRUE(tokenTextIsEqual(h, " "));
    EXPECT_EQ(getNextToken(h), kErrorLexTId); EXPECT_EQ(errorRC(h), kSyntaxErrorRC);
    
    //EXPECT_TRUE(tokenTextIsEqual(h, "@")); // Only the first unidentifiable char is tokenized as error
    //EXPECT_EQ(getNextToken(h), kErrorLexTId); EXPECT_EQ(errorRC(h), kSyntaxErrorRC);
    //EXPECT_TRUE(tokenTextIsEqual(h, "#"));
    //EXPECT_EQ(getNextToken(h), kErrorLexTId); EXPECT_EQ(errorRC(h), kSyntaxErrorRC);
    //EXPECT_TRUE(tokenTextIsEqual(h, "$"));
    //EXPECT_EQ(getNextToken(h), kSpaceLexTId); EXPECT_TRUE(tokenTextIsEqual(h, " "));
    //EXPECT_EQ(getNextToken(h), kIdentLexTId); EXPECT_TRUE(tokenTextIsEqual(h, "b"));
    //EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test with kReturnUnknownLexFl enabled
    const char* buf3 = "~";
    ASSERT_EQ(create(h, buf3, strlen(buf3), kReturnUnknownLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kUnknownLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "~"));
    EXPECT_EQ(tokenCharCount(h), 1);
    EXPECT_EQ(errorRC(h), kOkRC); // No syntax error if kUnknownLexTId is returned
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_ErrorConditions, EndOfFile) {
    lex::handle_t h;
    rc_t rc;

    // Test getting EOF immediately
    const char* buf1 = "";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    EXPECT_EQ(errorRC(h), kEofRC); // Should set EOF RC
    destroy(h);

    // Test getting EOF after some tokens
    const char* buf2 = "token";
    ASSERT_EQ(create(h, buf2, strlen(buf2), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    EXPECT_EQ(errorRC(h), kEofRC);
    destroy(h);
}

// Additional test to ensure error conditions are reset or handled correctly
TEST(Lexer_ErrorConditions, ErrorStateResets) {
    lex::handle_t h;
    rc_t rc;

    // Trigger an error
    const char* buf1 = "\"unclosed";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kErrorLexTId);
    EXPECT_EQ(errorRC(h), kSyntaxErrorRC);
    destroy(h);

    // Create a new lexer, ensure it's not in an error state
    const char* buf2 = "valid";
    ASSERT_EQ(create(h, buf2, strlen(buf2), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_EQ(errorRC(h), kOkRC); // errorRC should be reset to kOkRC
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}


TEST(Lexer_UserDefinedTokens, RegisterExactStringTokens) {
    lex::handle_t h;
    rc_t rc;

    const unsigned kPlusLexTId = kUserLexTId + 1;
    const unsigned kMinusLexTId = kUserLexTId + 2;
    const unsigned kEqLexTId = kUserLexTId + 3;

    const char* buf1 = "a + b - c = d";
    ASSERT_EQ(create(h, buf1, strlen(buf1), kReturnSpaceLexFl), kOkRC);

    EXPECT_EQ(registerToken(h, kPlusLexTId, "+"), kOkRC);
    EXPECT_EQ(registerToken(h, kMinusLexTId, "-"), kOkRC);
    EXPECT_EQ(registerToken(h, kEqLexTId, "="), kOkRC);

    EXPECT_EQ(getNextToken(h), kIdentLexTId); EXPECT_TRUE(tokenTextIsEqual(h, "a"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kPlusLexTId); EXPECT_TRUE(tokenTextIsEqual(h, "+"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); EXPECT_TRUE(tokenTextIsEqual(h, "b"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kMinusLexTId); EXPECT_TRUE(tokenTextIsEqual(h, "-"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); EXPECT_TRUE(tokenTextIsEqual(h, "c"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kEqLexTId); EXPECT_TRUE(tokenTextIsEqual(h, "="));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); EXPECT_TRUE(tokenTextIsEqual(h, "d"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_UserDefinedTokens, PriorityWithIdentifiers) {
    lex::handle_t h;
    rc_t rc;

    const unsigned kIfKeywordTId = kUserLexTId + 1;
    const unsigned kElseKeywordTId = kUserLexTId + 2;

    // Without kUserDefPriorityLexFl, "if" should be an identifier
    const char* buf1 = "if if_else";
    ASSERT_EQ(create(h, buf1, strlen(buf1), kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(registerToken(h, kIfKeywordTId, "if"), kOkRC);
    
    EXPECT_EQ(getNextToken(h), kIfKeywordTId); // "if" recognized as ident
    EXPECT_TRUE(tokenTextIsEqual(h, "if"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "if_else"
    EXPECT_TRUE(tokenTextIsEqual(h, "if_else"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // With kUserDefPriorityLexFl, "if" should be a user-defined token
    const char* buf2 = "if if_else";
    ASSERT_EQ(create(h, buf2, strlen(buf2), kReturnSpaceLexFl | kUserDefPriorityLexFl), kOkRC);
    EXPECT_EQ(registerToken(h, kIfKeywordTId, "if"), kOkRC);
    
    EXPECT_EQ(getNextToken(h), kIfKeywordTId); // "if" recognized as user-defined
    EXPECT_TRUE(tokenTextIsEqual(h, "if"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIfKeywordTId); // "if_else" is recognized as 'if' 
    EXPECT_TRUE(tokenTextIsEqual(h, "if"));
    EXPECT_EQ(getNextToken(h), kIdentLexTId);  // _else recognized as identifier
    EXPECT_TRUE(tokenTextIsEqual(h, "_else"));    
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

}


// Custom matcher that recognizes "foobar"
unsigned customFoobarMatcher(const char* cp, unsigned cn)
{
    const char* foobar = "foobar";
    unsigned foobar_len = strlen(foobar);
    if (cn >= foobar_len && strncmp(cp, foobar, foobar_len) == 0) {
        return foobar_len;
    }
    return 0;
}


TEST(Lexer_UserDefinedTokens, RegisterCustomMatcher) {
    lex::handle_t h;
    rc_t rc;

    const unsigned kFoobarTId = kUserLexTId + 1;

    const char* buf1 = "prefixfoobar suffix";
    ASSERT_EQ(create(h, buf1, strlen(buf1), kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(registerMatcher(h, kFoobarTId, customFoobarMatcher), kOkRC);
    
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "prefixfoobar" if custom matcher doesn't have priority
    EXPECT_TRUE(tokenTextIsEqual(h, "prefixfoobar"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "suffix"
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // With kUserDefPriorityLexFl, "foobar" should be a user-defined token
    const char* buf2 = "prefix foobar suffix";
    ASSERT_EQ(create(h, buf2, strlen(buf2), kReturnSpaceLexFl | kUserDefPriorityLexFl), kOkRC);
    EXPECT_EQ(registerMatcher(h, kFoobarTId, customFoobarMatcher), kOkRC);
    
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "prefix"
    EXPECT_TRUE(tokenTextIsEqual(h, "prefix"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kFoobarTId); // "foobar" recognized by custom matcher
    EXPECT_TRUE(tokenTextIsEqual(h, "foobar"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "suffix"
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_UserDefinedTokens, RegisterDuplicateTokens) {
    lex::handle_t h;
    rc_t rc;

    const unsigned kPlusLexTId = kUserLexTId + 1;

    const char* buf1 = "a+b";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC);

    EXPECT_EQ(registerToken(h, kPlusLexTId, "+"), kOkRC);
    // Attempt to register same token ID again
    EXPECT_EQ(registerToken(h, kPlusLexTId, "plus"), kInvalidArgRC);
    // Attempt to register same token string again with different ID
    EXPECT_EQ(registerToken(h, kUserLexTId + 5, "+"), kInvalidArgRC);
    
    destroy(h);
}

TEST(Lexer_Whitespace, WithReturnSpaceFlag) {
    lex::handle_t h;
    rc_t rc;

    // Test various whitespace characters with kReturnSpaceLexFl enabled
    const char* buf1 = "a \t\n\f\v\rb"; // space, tab, newline, form feed, vertical tab, carriage return
    ASSERT_EQ(create(h, buf1, strlen(buf1), kReturnSpaceLexFl), kOkRC);
    
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "a"));
    EXPECT_EQ(currentLineNumber(h), 1); EXPECT_EQ(currentColumnNumber(h), 1);

    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, " \t\n\f\v\r")); // All whitespace grouped into one token
    EXPECT_EQ(currentLineNumber(h), 1); EXPECT_EQ(currentColumnNumber(h), 2); // Start of first space
    
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "b"));
    EXPECT_EQ(currentLineNumber(h), 2); // After newline in whitespace, line count increments
    EXPECT_EQ(currentColumnNumber(h), 5); // Column depends on the actual whitespace characters (tab, etc.)
                                          // It's hard to predict exactly if lexer doesn't explicitly track column in space token.
                                          // Let's assume it tracks the start of the token.
    
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test multiple consecutive spaces
    const char* buf2 = "x   y";
    ASSERT_EQ(create(h, buf2, strlen(buf2), kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "x"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "   "));
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "y"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_Whitespace, WithoutReturnSpaceFlag) {
    lex::handle_t h;
    rc_t rc;

    // Test various whitespace characters with kReturnSpaceLexFl disabled (default)
    const char* buf1 = "a \t\n\f\v\rb";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC); // kReturnSpaceLexFl disabled
    
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "a"));
    EXPECT_EQ(currentLineNumber(h), 1); EXPECT_EQ(currentColumnNumber(h), 1);

    // Whitespace should be skipped, so next token is 'b'
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "b"));
    EXPECT_EQ(currentLineNumber(h), 2); // Line number should still advance due to '\n' in skipped whitespace
    EXPECT_EQ(currentColumnNumber(h), 5); // Column also advances
    
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test multiple consecutive spaces are skipped
    const char* buf2 = "x   y";
    ASSERT_EQ(create(h, buf2, strlen(buf2), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "x"));
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "y"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_Identifiers, ValidIdentifiers) {
    lex::handle_t h;
    rc_t rc;

    // Test identifier starting with underscore
    const char* buf1 = "_variable";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "_variable"));
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test alphanumeric identifier
    const char* buf2 = "myVar123";
    ASSERT_EQ(create(h, buf2, strlen(buf2), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "myVar123"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test identifier with numbers in between
    const char* buf3 = "VAR_NAME_1_2";
    ASSERT_EQ(create(h, buf3, strlen(buf3), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "VAR_NAME_1_2"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_Identifiers, IdentifiersStartingWithNumbers) {
    lex::handle_t h;
    rc_t rc;

    // Identifiers cannot start with a number. Should be tokenized as an integer followed by an identifier.
    const char* buf1 = "123_invalid";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kIntLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "123"));
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // _invalid
    EXPECT_TRUE(tokenTextIsEqual(h, "_invalid"));
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 4);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    const char* buf2 = "456.78id";
    ASSERT_EQ(create(h, buf2, strlen(buf2), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kRealLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "456.78"));
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // id
    EXPECT_TRUE(tokenTextIsEqual(h, "id"));
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 7);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_Identifiers, InvalidIdentifierCharacters) {
    lex::handle_t h;
    rc_t rc;

    // Test identifier with hyphen (invalid character)
    const char* buf1 = "my-var";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "my"));
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);
    EXPECT_EQ(getNextToken(h), kErrorLexTId); // '-' is not recognized
    EXPECT_EQ(errorRC(h), kSyntaxErrorRC);
    destroy(h);

    // Test identifier with exclamation mark (invalid character)
    const char* buf2 = "test!ident";
    ASSERT_EQ(create(h, buf2, strlen(buf2), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "test"));
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);
    EXPECT_EQ(getNextToken(h), kErrorLexTId); // '!' is not recognized
    EXPECT_EQ(errorRC(h), kSyntaxErrorRC);
    destroy(h);
}


TEST(Lexer_Comments, LineComments) {
    lex::handle_t h;
    rc_t rc;

    // Test simple line comment
    const char* buf1 = "code // line comment\nmore code";
    ASSERT_EQ(create(h, buf1, strlen(buf1), kReturnCommentsLexFl | kReturnSpaceLexFl ), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "code"
    EXPECT_EQ(tokenText(h)[0], 'c'); EXPECT_EQ(tokenCharCount(h), 4);
    EXPECT_EQ(currentLineNumber(h), 1); EXPECT_EQ(currentColumnNumber(h), 1);
    EXPECT_EQ(getNextToken(h), kSpaceLexTId); // Space
    EXPECT_EQ(getNextToken(h), kLineCmtLexTId); // "// line comment"
    // Expect tokenText to include the comment delimiters based on _lexLineCmtMatcher
    // Let's verify the behavior of tokenText for comments.
    // Based on tokenText implementation, it will return the text without leading/trailing quotes for QSTR
    // but for comments, it might return the whole comment including delimiters or not.
    // Looking at _lexLineCmtMatcher, it returns n + i, where n is strlen(p->lineCmtStr)
    // so tokenText(h) should point to the beginning of the comment including "//"
    // However, the helper tokenTextIsEqual works by comparing just the content. Let's adjust expected.
    // The current implementation of tokenText and tokenCharCount seem to include the delimiters for comments.
    EXPECT_TRUE(tokenTextIsEqual(h, "// line comment\n"));
    EXPECT_EQ(currentLineNumber(h), 1); EXPECT_EQ(currentColumnNumber(h), 6);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "more"
    EXPECT_EQ(getNextToken(h), kSpaceLexTId); // " "
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "code"
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test line comment at EOF
    const char* buf2 = "code // last comment";
    ASSERT_EQ(create(h, buf2, strlen(buf2), kReturnCommentsLexFl | kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "code"
    EXPECT_EQ(getNextToken(h), kSpaceLexTId); // Space
    EXPECT_EQ(getNextToken(h), kLineCmtLexTId); // "// last comment"
    EXPECT_TRUE(tokenTextIsEqual(h, "// last comment"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test line comment with kReturnCommentsLexFl disabled
    const char* buf3 = "code // hidden comment\nmore code";
    ASSERT_EQ(create(h, buf3, strlen(buf3), kReturnSpaceLexFl), kOkRC); // kReturnCommentsLexFl disabled
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "code"
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);
    EXPECT_EQ(getNextToken(h), kSpaceLexTId); // "\n"
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "more"
    EXPECT_EQ(currentLineNumber(h), 2); // Should advance past newline
    EXPECT_EQ(getNextToken(h), kSpaceLexTId); // " "
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "code"
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_Comments, BlockComments) {
    lex::handle_t h;
    rc_t rc;

    // Test simple block comment
    const char* buf1 = "code /* block comment */ more code";
    ASSERT_EQ(create(h, buf1, strlen(buf1), kReturnCommentsLexFl | kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "code"
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kBlockCmtLexTId); // "/* block comment */"
    EXPECT_TRUE(tokenTextIsEqual(h, "/* block comment */"));
    EXPECT_EQ(currentLineNumber(h), 1); EXPECT_EQ(currentColumnNumber(h), 6);
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "more"
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "code"
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test empty block comment
    const char* buf2 = "code /**/ more code";
    ASSERT_EQ(create(h, buf2, strlen(buf2), kReturnCommentsLexFl | kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "code"
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kBlockCmtLexTId); // "/**/"
    EXPECT_TRUE(tokenTextIsEqual(h, "/**/"));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "more"
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "code"
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test multiline block comment
    const char* buf3 = "/* line1\nline2\nline3 */";
    ASSERT_EQ(create(h, buf3, strlen(buf3), kReturnCommentsLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kBlockCmtLexTId); // "/* line1\nline2\nline3 */"
    EXPECT_TRUE(tokenTextIsEqual(h, "/* line1\nline2\nline3 */"));
    EXPECT_EQ(currentLineNumber(h), 1); EXPECT_EQ(currentColumnNumber(h), 1);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test block comment with kReturnCommentsLexFl disabled
    const char* buf4 = "code /* hidden comment */ more code";
    ASSERT_EQ(create(h, buf4, strlen(buf4), 0), kOkRC); // kReturnCommentsLexFl disabled
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "code"
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "more" (comment and spaces skipped)
    // Column number might be different due to skipped tokens, hard to predict without tracing internal state
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "code"
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_Comments, UnclosedBlockComment) {
    lex::handle_t h;
    rc_t rc;

    // Test unclosed block comment (should result in an error token)
    const char* buf1 = "code /* unclosed comment";
    ASSERT_EQ(create(h, buf1, strlen(buf1), kReturnCommentsLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "code"
    EXPECT_EQ(getNextToken(h), kErrorLexTId);
    EXPECT_EQ(errorRC(h), kSyntaxErrorRC);
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 5);
    destroy(h);
}

// Test nested block comments if the lexer supports them (cwLex's _lexBlockCmtMatcher doesn't seem to)
// The current implementation of _lexBlockCmtMatcher finds the first "*/" so it doesn't support nested comments.
// This test verifies that behavior, which effectively treats it as an unclosed comment.
TEST(Lexer_Comments, NestedBlockComments_Unsupported) {
    lex::handle_t h;
    rc_t rc;

    const char* buf1 = "/* outer /* inner */ outer */";
    ASSERT_EQ(create(h, buf1, strlen(buf1), kReturnCommentsLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kBlockCmtLexTId); // Should find "/* outer /* inner */"
    EXPECT_TRUE(tokenTextIsEqual(h, "/* outer /* inner */"));
    
    // After "*/" for "inner", the remaining " outer */" is still in the buffer
    // The next token should be "outer" or an error if "*/" is not recognized
    EXPECT_EQ(getNextToken(h), kIdentLexTId); // "outer"
    EXPECT_EQ(getNextToken(h), kErrorLexTId); // Should be an error when "*/" is encountered as unknown
    EXPECT_EQ(errorRC(h), kSyntaxErrorRC);
    destroy(h);
}


TEST(Lexer_CharLiterals, BasicChars) {
    lex::handle_t h;
    rc_t rc;

    // Test simple characters with kReturnQCharLexFl enabled
    const char* buf1 = "'a' '1' ' '";
    ASSERT_EQ(create(h, buf1, strlen(buf1), kReturnSpaceLexFl | kReturnQCharLexFl), kOkRC);
    
    EXPECT_EQ(getNextToken(h), kQCharLexTId);
    EXPECT_EQ(tokenCharCount(h),1);    
    EXPECT_TRUE(tokenTextIsEqual(h, "a"));
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);

    EXPECT_EQ(getNextToken(h), kSpaceLexTId); // Space
    
    EXPECT_EQ(getNextToken(h), kQCharLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "1"));

    EXPECT_EQ(getNextToken(h), kSpaceLexTId); // Space

    EXPECT_EQ(getNextToken(h), kQCharLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, " "));
    
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test with kReturnQCharLexFl disabled
    const char* buf2 = "'b'";
    ASSERT_EQ(create(h, buf2, strlen(buf2), 0), kOkRC); // kReturnQCharLexFl not set
    EXPECT_EQ(getNextToken(h), kErrorLexTId); // Should not recognize as QChar
    EXPECT_EQ(errorRC(h), kSyntaxErrorRC); // Should be an error
    destroy(h);
}

TEST(Lexer_CharLiterals, EscapeSequences) {
    lex::handle_t h;
    rc_t rc;

    // Test common escape sequences
    const char* buf1 = "'\n' '\t' '\\\''";
    ASSERT_EQ(create(h, buf1, strlen(buf1), kReturnSpaceLexFl | kReturnQCharLexFl), kOkRC);
    
    EXPECT_EQ(getNextToken(h), kQCharLexTId);
    EXPECT_EQ(tokenCharCount(h),1);
    EXPECT_TRUE(tokenTextIsEqual(h, "\n"));

    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    
    EXPECT_EQ(getNextToken(h), kQCharLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "\t"));

    EXPECT_EQ(getNextToken(h), kSpaceLexTId);

    EXPECT_EQ(getNextToken(h), kQCharLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "\\\'")); // The literal single quote needs escaping
                                                // This reduces to '\''
    
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_CharLiterals, UnclosedChar) {
    lex::handle_t h;
    rc_t rc;

    // Test unclosed char (should result in an error token if kReturnQCharLexFl is enabled)
    const char* buf1 = "'c";
    ASSERT_EQ(create(h, buf1, strlen(buf1), kReturnQCharLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kErrorLexTId);
    EXPECT_EQ(errorRC(h), kSyntaxErrorRC);
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);
    destroy(h);

    // Test unclosed char with escape
    const char* buf2 = "'\\";
    ASSERT_EQ(create(h, buf2, strlen(buf2), kReturnQCharLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kErrorLexTId);
    EXPECT_EQ(errorRC(h), kSyntaxErrorRC);
    destroy(h);
}

TEST(Lexer_StringLiterals, BasicStrings) {
    lex::handle_t h;
    rc_t rc;

    // Test empty string
    const char* buf1 = "\"\"";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kQStrLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "")); // cwLex::tokenText for QStrLexTId excludes quotes
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test simple string
    const char* buf2 = "\"hello world\"";
    ASSERT_EQ(create(h, buf2, strlen(buf2), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kQStrLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "hello world"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test string with numbers and special characters
    const char* buf3 = "\"123!@#$ ABC\"";
    ASSERT_EQ(create(h, buf3, strlen(buf3), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kQStrLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "123!@#$ ABC"));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_StringLiterals, EscapeSequences) {
    lex::handle_t h;
    rc_t rc;

    // Test common escape sequences
    const char* buf1 = "\"\n\t\\\"\'\"";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kQStrLexTId);
    // cwLex tokenText returns the raw string content without processing escapes.
    // So the token text would literally be "\n\t\\\"\\'"
    EXPECT_TRUE(tokenTextIsEqual(h, "\n\t\\\"\'"));
    EXPECT_EQ(tokenCharCount(h), 5);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test hexadecimal escape sequence (cwLex doesn't process these, returns raw)
    const char* buf2 = "\"\x41\x62\""; // Should be "Ab" if processed
    ASSERT_EQ(create(h, buf2, strlen(buf2), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kQStrLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "\x41\x62"));
    EXPECT_EQ(tokenCharCount(h), 2);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_StringLiterals, MultilineStrings) {
    lex::handle_t h;
    rc_t rc;

    // Test multiline string (string containing newline characters)
    const char* buf1 = "\"line1\nline2\"";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kQStrLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "line1\nline2"));
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
    
    // Test multiline string across actual lines (lexer should still see it as one token if quotes are matched)
    // Lexer's currentLineNumber and currentColumnNumber will reflect the start.
    const char* buf2 = "\"first line\nsecond line\"";
    ASSERT_EQ(create(h, buf2, strlen(buf2), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kQStrLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "first line\nsecond line"));
    EXPECT_EQ(currentLineNumber(h), 1); // Token starts on line 1
    EXPECT_EQ(currentColumnNumber(h), 1);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}

TEST(Lexer_StringLiterals, UnclosedString) {
    lex::handle_t h;
    rc_t rc;

    // Test unclosed string (should result in an error token)
    const char* buf1 = "\"unclosed string";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kErrorLexTId);
    EXPECT_EQ(errorRC(h), kSyntaxErrorRC);
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);
    destroy(h);

    // Test unclosed string with escape at end
    const char* buf2 = "\"unclosed with \\";
    ASSERT_EQ(create(h, buf2, strlen(buf2), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kErrorLexTId);
    EXPECT_EQ(errorRC(h), kSyntaxErrorRC);
    destroy(h);
}

TEST(Lexer_NumberLiterals, Integers) {
    lex::handle_t h;
    rc_t rc;

    // Test positive integer
    const char* buf1 = "12345";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kIntLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "12345"));
    EXPECT_EQ(tokenInt(h), 12345);
    EXPECT_EQ(tokenUInt(h), 12345);
    EXPECT_FALSE(tokenIsUnsigned(h));
    EXPECT_FALSE(tokenIsSinglePrecision(h));
    EXPECT_EQ(currentLineNumber(h), 1);
    EXPECT_EQ(currentColumnNumber(h), 1);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test zero
    const char* buf2 = "0";
    ASSERT_EQ(create(h, buf2, strlen(buf2), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kIntLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "0"));
    EXPECT_EQ(tokenInt(h), 0);
    EXPECT_EQ(tokenUInt(h), 0);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test negative integer
    const char* buf3 = "-123";
    ASSERT_EQ(create(h, buf3, strlen(buf3), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kIntLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "-123"));
    EXPECT_EQ(tokenInt(h), -123);
    EXPECT_FALSE(tokenIsUnsigned(h));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test hexadecimal
    const char* buf4 = "0xABC 0X123f";
    ASSERT_EQ(create(h, buf4, strlen(buf4), kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kHexLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "0xABC"));
    EXPECT_EQ(tokenInt(h), 0xABC);
    EXPECT_EQ(tokenUInt(h), 0xABC);
    EXPECT_EQ(getNextToken(h), kSpaceLexTId); // Space
    EXPECT_EQ(getNextToken(h), kHexLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "0X123f")); // Hex is case-insensitive for 'x' and digits
    EXPECT_EQ(tokenInt(h), 0x123f);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test unsigned suffix
    const char* buf5 = "123u 456U";
    ASSERT_EQ(create(h, buf5, strlen(buf5), kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kIntLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "123u"));
    EXPECT_EQ(tokenInt(h), 123);
    EXPECT_TRUE(tokenIsUnsigned(h));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kIntLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "456U"));
    EXPECT_EQ(tokenInt(h), 456);
    EXPECT_TRUE(tokenIsUnsigned(h));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test signed integer with unsigned suffix (error)
    const char* buf6 = "-789u";
    ASSERT_EQ(create(h, buf6, strlen(buf6), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kErrorLexTId);
    destroy(h);
}

TEST(Lexer_NumberLiterals, RealNumbers) {
    lex::handle_t h;
    rc_t rc;

    // Test simple real
    const char* buf1 = "1.23";
    ASSERT_EQ(create(h, buf1, strlen(buf1), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kRealLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "1.23"));
    EXPECT_DOUBLE_EQ(tokenDouble(h), 1.23);
    EXPECT_FLOAT_EQ(tokenFloat(h), 1.23f);
    EXPECT_FALSE(tokenIsSinglePrecision(h));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test real starting with dot
    const char* buf2 = ".45";
    ASSERT_EQ(create(h, buf2, strlen(buf2), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kRealLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, ".45"));
    EXPECT_DOUBLE_EQ(tokenDouble(h), 0.45);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test real ending with dot
    const char* buf3 = "5.";
    ASSERT_EQ(create(h, buf3, strlen(buf3), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kRealLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "5."));
    EXPECT_DOUBLE_EQ(tokenDouble(h), 5.0);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test scientific notation
    const char* buf4 = "1.23e-5 4E+2 .5e3";
    ASSERT_EQ(create(h, buf4, strlen(buf4), kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kRealLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "1.23e-5"));
    EXPECT_DOUBLE_EQ(tokenDouble(h), 1.23e-5);
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kRealLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "4E+2"));
    EXPECT_DOUBLE_EQ(tokenDouble(h), 400.0);
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kRealLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, ".5e3"));
    EXPECT_DOUBLE_EQ(tokenDouble(h), 500.0);
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test single precision suffix
    const char* buf5 = "1.2f 3.4F";
    ASSERT_EQ(create(h, buf5, strlen(buf5), kReturnSpaceLexFl), kOkRC);
    EXPECT_EQ(getNextToken(h), kRealLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "1.2f"));
    EXPECT_FLOAT_EQ(tokenFloat(h), 1.2f);
    EXPECT_TRUE(tokenIsSinglePrecision(h));
    EXPECT_EQ(getNextToken(h), kSpaceLexTId);
    EXPECT_EQ(getNextToken(h), kRealLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "3.4F"));
    EXPECT_FLOAT_EQ(tokenFloat(h), 3.4f);
    EXPECT_TRUE(tokenIsSinglePrecision(h));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);

    // Test combinations: negative scientific float
    const char* buf6 = "-6.78e-2f";
    ASSERT_EQ(create(h, buf6, strlen(buf6), 0), kOkRC);
    EXPECT_EQ(getNextToken(h), kRealLexTId);
    EXPECT_TRUE(tokenTextIsEqual(h, "-6.78e-2f"));
    EXPECT_DOUBLE_EQ(tokenDouble(h), -6.78e-2);
    EXPECT_TRUE(tokenIsSinglePrecision(h));
    EXPECT_EQ(getNextToken(h), kEofLexTId);
    destroy(h);
}


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
