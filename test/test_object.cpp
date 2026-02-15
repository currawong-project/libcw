#include <gtest/gtest.h>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTest.h"

#include "cwObject.h"

using namespace cw;

class ObjectTest : public testing::Test
{
protected:
  ObjectTest(){}
  
  virtual ~ObjectTest(){}

  virtual void SetUp() override
  {
    
    const char    s [] = "{ a:1, b:2, c:[ 1.23, 4.56 ], d:true, e:false, f:true }";
    ASSERT_EQ( objectFromString(s,_o), kOkRC );    
  }

  virtual void TearDown() override
  {
    if( _o != nullptr )
      _o->free();
  }


  object_t* _o = nullptr;
  
};

TEST_F(ObjectTest, ParseTest )
{
  int v;
  int a = 0;
  int b = 0;
  const object_t* c = nullptr;
  bool d,e,f;
  
  
  ASSERT_NE( _o, nullptr );
  
  ASSERT_EQ(_o->get("b",v),kOkRC);
  EXPECT_EQ(v,2);


  ASSERT_EQ(_o->getv("a",a,"b",b),kOkRC);
  EXPECT_EQ(a,1);
  EXPECT_EQ(b,2);
  

  ASSERT_EQ(_o->readv("a",0,a,
                      "b",0,b,
                      "c",kOptFl | kListTId,c,
                      "d",0,d,
                      "e",0,e,
                      "f",0,f), kOkRC );
  
  EXPECT_EQ(a,1);
  EXPECT_EQ(b,2);
  EXPECT_EQ(d,true);
  EXPECT_EQ(e,false);
  EXPECT_EQ(f,true);

  // test that readv() fails if a requested field is not found
  EXPECT_NE(_o->readv("g",0,a), kOkRC );

  // get the getv_opt() does not fail a requested field is not found
  EXPECT_EQ(_o->getv_opt("g",a), kOkRC );
  
}

TEST_F(ObjectTest, PrintAndDuplTest  )
{
  int a,b;
  const object_t* c = nullptr;
  bool d,e,f;
  object_t* oo = nullptr;
  
  const unsigned bufN = 128;
  char buf[bufN];
  const char* s = " { \"a\" : 1, \"b\" : 2, \"c\" :  [ 1.230000, 4.560000 ] , \"d\" : true, \"e\" : false, \"f\" : true } ";

  ASSERT_EQ(_o->readv("a",0,a,
                      "b",0,b,
                      "c",kOptFl | kListTId,c,
                      "d",0,d,
                      "e",0,e,
                      "f",0,f), kOkRC );
  
  unsigned i = _o->to_string(buf,bufN);
  ASSERT_LT(i,bufN);
  ASSERT_STREQ(buf,s);

  // duplicate the tree
  ASSERT_NE(oo = _o->duplicate(),nullptr);

  // verify that the tree is the identical by comparing it's string output
  i = oo->to_string(buf,bufN);
  ASSERT_LT(i,bufN);
  ASSERT_STREQ(buf,s);
  oo->free();
  
}

// Test programmatic creation of different object types
TEST_F(ObjectTest, CreationTest)
{
    // Test dictionary creation
    object_t* dict = newDictObject();
    ASSERT_NE(dict, nullptr);
    EXPECT_TRUE(dict->is_dict());
    EXPECT_EQ(dict->child_count(), 0);

    // A list may not be a child of a dict
    object_t* list = newListObject(dict);
    ASSERT_EQ(list, nullptr);
    ASSERT_EQ(dict->child_count(), 0);

    
    // Test list creation
    list = newListObject(nullptr);
    ASSERT_NE(list, nullptr);
    EXPECT_TRUE(list->is_list());
    EXPECT_EQ(list->child_count(), 0);

    // Test primitive creation
    object_t* num = newObject((int32_t)123, list);
    ASSERT_NE(num, nullptr);
    EXPECT_TRUE(num->is_numeric());
    EXPECT_EQ(list->child_count(), 1);

    int32_t num_val;
    ASSERT_EQ(num->value(num_val), kOkRC);
    EXPECT_EQ(num_val, 123);

    // Test string creation
    object_t* str = newObject("hello", list);
    ASSERT_NE(str, nullptr);
    EXPECT_TRUE(str->is_string());
    EXPECT_EQ(list->child_count(), 2);
    const char* str_val;
    ASSERT_EQ(str->value(str_val), kOkRC);
    EXPECT_STREQ(str_val, "hello");

    // Test pair creation
    object_t* pair_val_node = newObject(true);
    object_t* pair = newPairObject("my_pair", pair_val_node, dict);
    ASSERT_NE(pair, nullptr);
    // newPairObject returns the value node, so we check its parent
    ASSERT_NE(pair->parent, nullptr);
    EXPECT_TRUE(pair->parent->is_pair());
    EXPECT_EQ(dict->child_count(), 1);
    
    const object_t* found_pair_val = dict->find("my_pair");
    ASSERT_NE(found_pair_val, nullptr);
    bool bool_val;
    ASSERT_EQ(found_pair_val->value(bool_val), kOkRC);
    EXPECT_EQ(bool_val, true);

    pair = newPairObject("my_list", list, dict);
    ASSERT_NE(pair, nullptr);
    EXPECT_EQ(dict->child_count(), 2);

    dict->free();
}

// Test modifying objects after creation
TEST_F(ObjectTest, ModificationTest)
{
    object_t* root = newDictObject();
    newPairObject("version", (int32_t)1, root);
    object_t* settings = newDictObject();
    newPairObject("settings", settings, root);
    newPairObject("enabled", false, settings);
    
    // Test set_value
    object_t* version_val = root->find("version");
    ASSERT_NE(version_val, nullptr);
    int32_t ver;
    version_val->value(ver);
    EXPECT_EQ(ver, 1);
    version_val->set_value((int32_t)2);
    version_val->value(ver);
    EXPECT_EQ(ver, 2);
    
    // Test set
    ASSERT_EQ(root->set("version", (int32_t)3), kOkRC);
    version_val->value(ver);
    EXPECT_EQ(ver, 3);

    // Test append_child
    object_t* enabled_val = settings->find("enabled");
    ASSERT_NE(enabled_val, nullptr);
    
    EXPECT_EQ(settings->child_count(), 1);
   
    object_t* new_child = newObject("new_val");
    // cannot append to a value node
    EXPECT_NE(enabled_val->append_child(new_child), kOkRC);
    
    // can append to a container
    //object_t* themes = newListObject(settings);
    object_t* themes = newListObject(nullptr);
   
    newPairObject("themes", themes, settings);
    
    EXPECT_EQ(themes->append_child(new_child), kOkRC);
    EXPECT_EQ(themes->child_count(), 1);
    
    // Test unlink
    EXPECT_EQ(settings->child_count(), 2);
    object_t* themes_pair = settings->child_ele(1);
    themes_pair->unlink();
    EXPECT_EQ(settings->child_count(), 1);
    EXPECT_EQ(themes_pair->parent, nullptr);
    themes_pair->free();
    
    root->free();
}

// Test traversal and data access
TEST_F(ObjectTest, TraversalAndAccessTest)
{
    // _o is: { a:1, b:2, c:[ 1.23, 4.56 ], d:true, e:false, f:true }
    ASSERT_NE(_o, nullptr);

    // Test child_ele and pair_label/pair_value
    object_t* first_child = _o->child_ele(0); // pair a:1
    ASSERT_NE(first_child, nullptr);
    EXPECT_TRUE(first_child->is_pair());
    EXPECT_STREQ(first_child->pair_label(), "a");
    int32_t val_a;
    ASSERT_EQ(first_child->pair_value()->value(val_a), kOkRC);
    EXPECT_EQ(val_a, 1);
    
    // Test next_child_ele
    object_t* second_child = _o->next_child_ele(first_child); // pair b:2
    ASSERT_NE(second_child, nullptr);
    EXPECT_STREQ(second_child->pair_label(), "b");

    // Test find on list
    const object_t* list_c = _o->find("c");
    ASSERT_NE(list_c, nullptr);
    EXPECT_TRUE(list_c->is_list());
    EXPECT_EQ(list_c->child_count(), 2);

    const object_t* first_list_ele = list_c->child_ele(0);
    double val_d;
    ASSERT_EQ(first_list_ele->value(val_d), kOkRC);
    EXPECT_DOUBLE_EQ(val_d, 1.23);

    // Test find non-existent
    EXPECT_EQ(_o->find("non-existent"), nullptr);
}

// Test serialization and parsing roundtrip
TEST_F(ObjectTest, RoundtripTest)
{
    const char* original_str = "{ \"name\" : \"test\", \"values\" :  [ 1,  [ \"nested\", true ] , 3.000000 ]  } ";
    object_t* obj = nullptr;
    
    ASSERT_EQ(objectFromString(original_str, obj), kOkRC);
    ASSERT_NE(obj, nullptr);
    
    char* generated_str = obj->to_string();
    ASSERT_NE(generated_str, nullptr);
    
    // The generated string may have different spacing, so we parse it again
    // and compare the new object's string representation to ensure consistency.
    object_t* obj2 = nullptr;
    ASSERT_EQ(objectFromString(generated_str, obj2), kOkRC);
    ASSERT_NE(obj2, nullptr);
    
    char* generated_str2 = obj2->to_string();
    EXPECT_STREQ(generated_str, generated_str2);

    obj->free();
    obj2->free();
    mem::release(generated_str);
    mem::release(generated_str2);
}

// Test various error conditions
TEST_F(ObjectTest, ErrorHandling)
{
    object_t* obj = nullptr;

    // Test parsing malformed strings
    EXPECT_NE(objectFromString("{ a: 1, ", obj), kOkRC);
    EXPECT_EQ(obj, nullptr);
    EXPECT_NE(objectFromString("[1, 2", obj), kOkRC);
    EXPECT_EQ(obj, nullptr);
    EXPECT_NE(objectFromString("{ key: }", obj), kOkRC);
    EXPECT_EQ(obj, nullptr);
    //EXPECT_NE(objectFromString("{ [ foo ] }", obj), kOkRC);
    //EXPECT_EQ(obj, nullptr);

    // Test 'readv' with an unknown field
    const char* s = "{ a:1, b:2, c:3 }";
    ASSERT_EQ(objectFromString(s, obj), kOkRC);
    int a,b;
    // This should fail because 'c' is in the object but not in the readv list.
    EXPECT_NE(obj->readv("a", 0, a, "b", 0, b), kOkRC);
    obj->free();

    // Test 'get' for a required field that doesn't exist
    ASSERT_EQ(objectFromString("{a:1}", obj), kOkRC);
    int z;
    EXPECT_NE(obj->get("z", z), kOkRC);

    // Test 'append_child' to a non-container
    object_t* val_node = obj->find("a");
    ASSERT_NE(val_node, nullptr);
    EXPECT_FALSE(val_node->is_container());
    object_t* new_child = newObject(123);
    EXPECT_NE(val_node->append_child(new_child), kOkRC);
    new_child->free(); // Must free since it wasn't added
    
    obj->free();
}

// Test string escape sequence parsing
TEST_F(ObjectTest, StringEscapeTest)
{
    object_t* obj = nullptr;
    const char* val = nullptr;

    // Test standard escape sequences
    ASSERT_EQ(objectFromString("{ \"key\": \"a\\nb\\rc\\td\\fb\" }", obj), kOkRC);
    ASSERT_NE(obj, nullptr);
    const object_t* key_val = obj->find("key");
    ASSERT_NE(key_val, nullptr);
    ASSERT_EQ(key_val->value(val), kOkRC);
    EXPECT_STREQ(val, "a\nb\rc\td\fb");
    obj->free();
    obj = nullptr;

    // Test quote, backslash, and slash
    ASSERT_EQ(objectFromString("{ \"key\": \"\\\" \\\\ \\/\" }", obj), kOkRC);
    ASSERT_NE(obj, nullptr);
    key_val = obj->find("key");
    ASSERT_NE(key_val, nullptr);
    ASSERT_EQ(key_val->value(val), kOkRC);
    EXPECT_STREQ(val, "\" \\ /");
    obj->free();
    obj = nullptr;

    // Test mixed string
    ASSERT_EQ(objectFromString("{ \"key\": \"first \\\"a\\nb\\\" last\" }", obj), kOkRC);
    ASSERT_NE(obj, nullptr);
    key_val = obj->find("key");
    ASSERT_NE(key_val, nullptr);
    ASSERT_EQ(key_val->value(val), kOkRC);
    EXPECT_STREQ(val, "first \"a\nb\" last");
    obj->free();
    obj = nullptr;

    // Test behavior with unknown escape sequence
    ASSERT_EQ(objectFromString("{ \"key\": \"unknown \\z end\" }", obj), kOkRC);
    ASSERT_NE(obj, nullptr);
    key_val = obj->find("key");
    ASSERT_NE(key_val, nullptr);
    ASSERT_EQ(key_val->value(val), kOkRC);
    EXPECT_STREQ(val, "unknown \\z end");
    obj->free();
    obj = nullptr;
}

