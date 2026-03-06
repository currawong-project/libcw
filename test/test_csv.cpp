#include <gtest/gtest.h>
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwFile.h"
#include "cwObject.h"
#include "cwCsv.h"

using namespace cw;
using namespace cw::csv;

// Helper to create a CSV file for testing
static rc_t createCsvFile(const char* filename, const char* content) {
    file::handle_t h;
    rc_t rc = file::open(h, filename, file::kWriteFl);
    if (rc != kOkRC) return rc;
    rc = file::write(h, content, strlen(content));
    if (rc != kOkRC) { file::close(h); return rc; }
    return file::close(h);
}

class CsvTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_filename = "test_temp.csv";
    }

    void TearDown() override {
        remove(test_filename);
    }

    const char* test_filename;
};

TEST_F(CsvTest, BasicOperations) {
    const char* content = "id,name,value\n1,test,3.14\n2,example,2.71\n";
    ASSERT_EQ(createCsvFile(test_filename, content), kOkRC);

    handle_t h;
    ASSERT_EQ(create(h, test_filename), kOkRC);
    EXPECT_TRUE(h.isValid());

    EXPECT_EQ(col_count(h), 3u);
    EXPECT_STREQ(col_title(h, 0), "id");
    EXPECT_STREQ(col_title(h, 1), "name");
    EXPECT_STREQ(col_title(h, 2), "value");

    EXPECT_EQ(title_col_index(h, "id"), 0u);
    EXPECT_EQ(title_col_index(h, "name"), 1u);
    EXPECT_EQ(title_col_index(h, "value"), 2u);
    EXPECT_EQ(title_col_index(h, "nonexistent"), kInvalidIdx);

    EXPECT_TRUE(has_field(h, "id"));
    EXPECT_FALSE(has_field(h, "price"));

    unsigned line_cnt = 0;
    ASSERT_EQ(line_count(h, line_cnt), kOkRC);
    EXPECT_EQ(line_cnt, 3u); // Header + 2 data lines

    ASSERT_EQ(destroy(h), kOkRC);
    EXPECT_FALSE(h.isValid());
}

TEST_F(CsvTest, RequiredColumns) {
    const char* content = "id,name,value\n1,test,3.14\n";
    ASSERT_EQ(createCsvFile(test_filename, content), kOkRC);

    handle_t h;
    const char* required[] = {"id", "value"};
    EXPECT_EQ(create(h, test_filename, required, 2u), kOkRC);
    destroy(h);

    const char* missing[] = {"id", "missing_col"};
    EXPECT_EQ(create(h, test_filename, missing, 2u), kEleNotFoundRC);
}

TEST_F(CsvTest, NavigationAndIteration) {
    const char* content = "col1\nval1\nval2\nval3";
    ASSERT_EQ(createCsvFile(test_filename, content), kOkRC);

    handle_t h;
    ASSERT_EQ(create(h, test_filename), kOkRC);

    EXPECT_EQ(cur_line_index(h), 0u); // Header is index 0

    ASSERT_EQ(next_line(h), kOkRC);
    EXPECT_EQ(cur_line_index(h), 1u);
    const char* val = nullptr;
    parse_field(h, 0u, val);
    EXPECT_STREQ(val, "val1");

    ASSERT_EQ(next_line(h), kOkRC);
    EXPECT_EQ(cur_line_index(h), 2u);
    parse_field(h, 0u, val);
    EXPECT_STREQ(val, "val2");

    ASSERT_EQ(next_line(h), kOkRC);
    EXPECT_EQ(cur_line_index(h), 3u);
    
    ASSERT_EQ(next_line(h), kEofRC);

    // Test Rewind
    ASSERT_EQ(rewind(h), kOkRC);
    EXPECT_EQ(cur_line_index(h), 0u);
    ASSERT_EQ(next_line(h), kOkRC);
    parse_field(h, 0u, val);
    EXPECT_STREQ(val, "val1");

    destroy(h);
}

TEST_F(CsvTest, TypeParsing) {
    const char* content = "int_val,double_val,bool_val,str_val\n"
                          "10,3.14,true,hello\n"
                          "-5,0.001,false,world\n";
    ASSERT_EQ(createCsvFile(test_filename, content), kOkRC);

    handle_t h;
    ASSERT_EQ(create(h, test_filename), kOkRC);

    // Row 1
    ASSERT_EQ(next_line(h), kOkRC);
    int i_val;
    double d_val;
    bool b_val;
    const char* s_val;

    EXPECT_EQ(parse_field(h, "int_val", i_val), kOkRC);
    EXPECT_EQ(i_val, 10);
    EXPECT_EQ(parse_field(h, "double_val", d_val), kOkRC);
    EXPECT_DOUBLE_EQ(d_val, 3.14);
    EXPECT_EQ(parse_field(h, "bool_val", b_val), kOkRC);
    EXPECT_TRUE(b_val);
    EXPECT_EQ(parse_field(h, "str_val", s_val), kOkRC);
    EXPECT_STREQ(s_val, "hello");

    // Row 2
    ASSERT_EQ(next_line(h), kOkRC);
    EXPECT_EQ(parse_field(h, 0u, i_val), kOkRC);
    EXPECT_EQ(i_val, -5);
    EXPECT_EQ(parse_field(h, 1u, d_val), kOkRC);
    EXPECT_DOUBLE_EQ(d_val, 0.001);
    EXPECT_EQ(parse_field(h, 2u, b_val), kOkRC);
    EXPECT_FALSE(b_val);
    EXPECT_EQ(parse_field(h, 3u, s_val), kOkRC);
    EXPECT_STREQ(s_val, "world");

    destroy(h);
}

TEST_F(CsvTest, VariadicGetV) {
    const char* content = "id,name,score\n1,Alice,95.5\n";
    ASSERT_EQ(createCsvFile(test_filename, content), kOkRC);

    handle_t h;
    ASSERT_EQ(create(h, test_filename), kOkRC);
    ASSERT_EQ(next_line(h), kOkRC);

    int id = 0;
    const char* name = nullptr;
    double score = 0.0;

    ASSERT_EQ(getv(h, "id", id, "name", name, "score", score), kOkRC);
    EXPECT_EQ(id, 1);
    EXPECT_STREQ(name, "Alice");
    EXPECT_DOUBLE_EQ(score, 95.5);

    destroy(h);
}

TEST_F(CsvTest, QuotedFieldsAndCommas) {
    const char* content = "col1,col2,col3\n"
                          "\"value, with, commas\",simple,\"quoted \"\"quote\"\"\"\n";
    ASSERT_EQ(createCsvFile(test_filename, content), kOkRC);

    handle_t h;
    ASSERT_EQ(create(h, test_filename), kOkRC);
    ASSERT_EQ(next_line(h), kOkRC);

    const char *v1, *v2, *v3;
    parse_field(h, 0u, v1);
    parse_field(h, 1u, v2);
    parse_field(h, 2u, v3);

    // Note: The current parser implementation does NOT strip quotes from quoted fields.
    EXPECT_STREQ(v1, "\"value, with, commas\"");
    EXPECT_STREQ(v2, "simple");
    EXPECT_STREQ(v3, "\"quoted \"\"quote\"\"\"");
    
    destroy(h);
}

TEST_F(CsvTest, SparseAndEmptyFields) {
    const char* content = "a,b,c\n1,,3\n4,5\n";
    ASSERT_EQ(createCsvFile(test_filename, content), kOkRC);

    handle_t h;
    ASSERT_EQ(create(h, test_filename), kOkRC);

    // Row 1: 1,,3
    ASSERT_EQ(next_line(h), kOkRC);
    int val_a=0, val_c=0;
    const char* val_b = "initial";
    EXPECT_EQ(parse_field(h, "a", val_a), kOkRC);
    EXPECT_EQ(parse_field(h, "b", val_b), kOkRC);
    EXPECT_EQ(parse_field(h, "c", val_c), kOkRC);
    EXPECT_EQ(val_a, 1);
    EXPECT_STREQ(val_b, ""); // Empty field returns empty string
    EXPECT_EQ(val_c, 3);

    // Row 2: 4,5 (missing third column)
    ASSERT_EQ(next_line(h), kOkRC);
    EXPECT_EQ(parse_field(h, 0u, val_a), kOkRC);
    EXPECT_EQ(parse_field(h, 1u, val_a), kOkRC); // reuse val_a for simplicity
    EXPECT_EQ(parse_field(h, 2u, val_c), kInvalidArgRC); // Column 2 is invalid for this row

    destroy(h);
}

TEST_F(CsvTest, FieldCharCount) {
    const char* content = "a,b\nhello,world\n";
    ASSERT_EQ(createCsvFile(test_filename, content), kOkRC);

    handle_t h;
    ASSERT_EQ(create(h, test_filename), kOkRC);
    ASSERT_EQ(next_line(h), kOkRC);

    unsigned count = 0;
    EXPECT_EQ(field_char_count(h, 0u, count), kOkRC);
    EXPECT_EQ(count, 5u);
    EXPECT_EQ(field_char_count(h, 1u, count), kOkRC);
    EXPECT_EQ(count, 5u);

    destroy(h);
}
