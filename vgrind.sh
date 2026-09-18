# export LD_LIBRARY_PATH=~/sdk/libwebsockets/build/out/lib
# valgrind --leak-check=yes --log-file=vg0.txt --track-origins=yes build/debug/bin/caw exec ~/src/cw_proj/rehear_prep/pgm.cfg recd_ops
valgrind --leak-check=yes --log-file=vg0.txt --track-origins=yes build/debug/test/test_main --gtest_filter=RecdTest.RecdArrayInherit --verbose


