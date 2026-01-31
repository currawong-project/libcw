#include "gtest/gtest.h"

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwTime.h"

TEST(TimeTest, GetAndCurrentTime) {
    cw::time::spec_t t1, t2;
    cw::time::get(t1);
    usleep(1000);
    t2 = cw::time::current_time();
    EXPECT_TRUE(cw::time::isLT(t1, t2));
}

TEST(TimeTest, Normalize) {
    cw::time::spec_t t = {1, 1000000000};
    cw::time::normalize(t);
    EXPECT_EQ(t.tv_sec, 2);
    EXPECT_EQ(t.tv_nsec, 0);

    //t = {1, -1};
    //cw::time::normalize(t);
    //EXPECT_EQ(t.tv_sec, 0);
    //EXPECT_EQ(t.tv_nsec, 999999999);
}

TEST(TimeTest, Accumulate) {
    cw::time::spec_t acc = {0, 0};
    cw::time::spec_t dur = {1, 500000000};
    cw::time::accumulate(acc, dur);
    EXPECT_EQ(acc.tv_sec, 1);
    EXPECT_EQ(acc.tv_nsec, 500000000);
    cw::time::accumulate(acc, dur);
    EXPECT_EQ(acc.tv_sec, 3);
    EXPECT_EQ(acc.tv_nsec, 0);
}

TEST(TimeTest, AccumulateElapsed) {
    cw::time::spec_t acc = {0, 0};
    cw::time::spec_t t0 = {1, 0};
    cw::time::spec_t t1 = {2, 500000000};
    cw::time::accumulate_elapsed(acc, t0, t1);
    EXPECT_EQ(acc.tv_sec, 1);
    EXPECT_EQ(acc.tv_nsec, 500000000);
}

TEST(TimeTest, AccumulateElapsedCurrent) {
    cw::time::spec_t acc = {0, 0};
    cw::time::spec_t t0;
    cw::time::get(t0);
    usleep(1000);
    cw::time::accumulate_elapsed_current(acc, t0);
    EXPECT_TRUE(acc.tv_sec > 0 || acc.tv_nsec > 0);
}

TEST(TimeTest, Seconds) {
    cw::time::spec_t t = {1, 500000000};
    EXPECT_DOUBLE_EQ(cw::time::seconds(t), 1.5);
}

TEST(TimeTest, Elapsed) {
    cw::time::spec_t t0 = {1, 0};
    cw::time::spec_t t1 = {2, 500000000};

    EXPECT_EQ(cw::time::elapsedMicros(t0, t1), 1500000);
    EXPECT_EQ(cw::time::elapsedMs(t0, t1), 1500);
    EXPECT_DOUBLE_EQ(cw::time::elapsedSecs(t0, t1), 1.5);
}

TEST(TimeTest, ElapsedNow) {
    cw::time::spec_t t0;
    cw::time::get(t0);
    usleep(1000);
    EXPECT_GE(cw::time::elapsedMicros(t0), 1000);
    EXPECT_GE(cw::time::elapsedMs(t0), 1);
    EXPECT_GE(cw::time::elapsedSecs(t0), 0.001);
}

TEST(TimeTest, AbsElapsedMicros) {
    cw::time::spec_t t0 = {1, 0};
    cw::time::spec_t t1 = {2, 500000000};
    EXPECT_EQ(cw::time::absElapsedMicros(t0, t1), 1500000);
    EXPECT_EQ(cw::time::absElapsedMicros(t1, t0), 1500000);
}

TEST(TimeTest, DiffMicros) {
    cw::time::spec_t t0 = {1, 0};
    cw::time::spec_t t1 = {2, 500000000};
    EXPECT_EQ(cw::time::diffMicros(t0, t1), 1500000);
    EXPECT_EQ(cw::time::diffMicros(t1, t0), -1500000);
}

TEST(TimeTest, Comparisons) {
    cw::time::spec_t t0 = {1, 0};
    cw::time::spec_t t1 = {1, 0};
    cw::time::spec_t t2 = {2, 0};

    EXPECT_TRUE(cw::time::isLTE(t0, t1));
    EXPECT_TRUE(cw::time::isLTE(t0, t2));
    EXPECT_FALSE(cw::time::isLTE(t2, t0));

    EXPECT_FALSE(cw::time::isLT(t0, t1));
    EXPECT_TRUE(cw::time::isLT(t0, t2));
    EXPECT_FALSE(cw::time::isLT(t2, t0));

    EXPECT_TRUE(cw::time::isGTE(t0, t1));
    EXPECT_FALSE(cw::time::isGTE(t0, t2));
    EXPECT_TRUE(cw::time::isGTE(t2, t0));

    EXPECT_FALSE(cw::time::isGT(t0, t1));
    EXPECT_FALSE(cw::time::isGT(t0, t2));
    EXPECT_TRUE(cw::time::isGT(t2, t0));

    EXPECT_TRUE(cw::time::isEqual(t0, t1));
    EXPECT_FALSE(cw::time::isEqual(t0, t2));
}

TEST(TimeTest, Zero) {
    cw::time::spec_t t = {0, 0};
    EXPECT_TRUE(cw::time::isZero(t));
    cw::time::setZero(t);
    EXPECT_TRUE(cw::time::isZero(t));
    t = {1,1};
    EXPECT_FALSE(cw::time::isZero(t));
    cw::time::setZero(t);
    EXPECT_TRUE(cw::time::isZero(t));
}

TEST(TimeTest, Now) {
    cw::time::spec_t t1;
    cw::time::now(t1);
    usleep(1000);
    cw::time::spec_t t2;
    cw::time::now(t2);
    EXPECT_TRUE(cw::time::isLT(t1, t2));
}

TEST(TimeTest, SubtractMicros) {
    cw::time::spec_t t = {2, 500000000};
    cw::time::subtractMicros(t, 500000);
    EXPECT_EQ(t.tv_sec, 2);
    EXPECT_EQ(t.tv_nsec, 0);
    cw::time::subtractMicros(t, 1000000);
    EXPECT_EQ(t.tv_sec, 1);
    EXPECT_EQ(t.tv_nsec, 0);
}

TEST(TimeTest, Advance) {
    cw::time::spec_t t = {1, 0};
    cw::time::advanceMicros(t, 500000);
    EXPECT_EQ(t.tv_sec, 1);
    EXPECT_EQ(t.tv_nsec, 500000000);
    cw::time::advanceMs(t, 500);
    EXPECT_EQ(t.tv_sec, 2);
    EXPECT_EQ(t.tv_nsec, 0);
}

TEST(TimeTest, FutureMs) {
    cw::time::spec_t t_future;
    cw::time::futureMs(t_future, 100);
    cw::time::spec_t t_now;
    cw::time::get(t_now);
    EXPECT_TRUE(cw::time::isGTE(t_future, t_now));
    EXPECT_GE(cw::time::diffMicros(t_now, t_future), 100000 - 1000 /* allow for 1ms jitter */);
}

TEST(TimeTest, Conversions) {
    cw::time::spec_t ts;
    cw::time::fracSecondsToSpec(ts, 1.5);
    EXPECT_EQ(ts.tv_sec, 1);
    EXPECT_EQ(ts.tv_nsec, 500000000);

    cw::time::secondsToSpec(ts, 2);
    EXPECT_EQ(ts.tv_sec, 2);
    EXPECT_EQ(ts.tv_nsec, 0);

    EXPECT_DOUBLE_EQ(cw::time::specToSeconds(ts), 2.0);
    EXPECT_EQ(cw::time::specToMicroseconds(ts), 2000000);

    cw::time::millisecondsToSpec(ts, 1500);
    EXPECT_EQ(ts.tv_sec, 1);
    EXPECT_EQ(ts.tv_nsec, 500000000);

    cw::time::microsecondsToSpec(ts, 2500000);
    EXPECT_EQ(ts.tv_sec, 2);
    EXPECT_EQ(ts.tv_nsec, 500000000);

    ts = cw::time::microsecondsToSpec(3500000);
    EXPECT_EQ(ts.tv_sec, 3);
    EXPECT_EQ(ts.tv_nsec, 500000000);
}

TEST(TimeTest, FormatDateTime) {
    char buf[128];
    unsigned len = cw::time::formatDateTime(buf, sizeof(buf), false);
    EXPECT_GT(len, 0);
    EXPECT_LT(len, sizeof(buf));
    // Example format: 12:34:56.789
    EXPECT_EQ(buf[2], ':');
    EXPECT_EQ(buf[5], ':');
    EXPECT_EQ(buf[8], '.');

    len = cw::time::formatDateTime(buf, sizeof(buf), true);
    EXPECT_GT(len, 0);
    EXPECT_LT(len, sizeof(buf));
    // Example format: 2024:01:26 12:34:56.789
    EXPECT_EQ(buf[4], ':');
    EXPECT_EQ(buf[7], ':');
    EXPECT_EQ(buf[10], ' ');
}

// The test function in cwTime is a placeholder in this context,
// but we can call it for completeness.
TEST(TimeTest, TestRunner) {
    cw::test::test_args_t args = {};
    EXPECT_EQ(cw::time::test(args), cw::kOkRC);
}
