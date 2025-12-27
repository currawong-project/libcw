


int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  // GoogleTest takes ownership of the pointer, so use `new`.
  //::testing::AddGlobalTestEnvironment(new GlobalEnvironment); 
  return RUN_ALL_TESTS();
}
